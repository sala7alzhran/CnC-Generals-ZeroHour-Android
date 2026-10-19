# Touch controls

*Command & Conquer: Generals* is a mouse+keyboard RTS. This port implements a full multi‑touch
control layer so it's playable on a phone. It lives in the Android input path of
`SDL3GameEngine.cpp` (all under `#if defined(__ANDROID__)`), with a tiny Java bridge in
`ZerohourActivity.java`. The design it implements is [MOBILE-UX-SPEC.md](../MOBILE-UX-SPEC.md);
this page is Phase A of that spec (camera and selection gestures).

## Gesture map

In a battle, with the finger on the battlefield ("camera mode"):

| Gesture | What the layer does |
|---|---|
| 1‑finger drag | **grab pan**: the terrain point under the finger stays under it; release flings |
| tap | left click (select; with a selection on the ground/enemy: context order) |
| double tap (≤ 300 ms, ≤ 4 mm apart) | left double click (`SelectionXlat` selects all of that type on screen) |
| long‑press (350 ms, haptic tick), then drag | left drag anchored at the press point → rubber‑band select, with edge pan |
| 2‑finger pinch | zoom anchored under the pinch midpoint, soft limits |
| 2‑finger drag | the legacy scaled pan (`View::userScrollBy`), kept for comparison |
| 2‑finger tap | right click (deselect / cancel placement) |
| tap **☰** (top‑left, in‑game) | open pause/ESC menu (`ToggleQuitMenu`) |
| tap **▶▶** (top‑right, in a video) | skip cutscene (synthesizes `KEY_ESC`) |

Everywhere else ("classic mode": menus, any opaque HUD window such as the command bar or the
minimap, while a building is being placed, cutscenes, the pause menu) a finger is a mouse:
tap = left click, drag = left drag, 2‑finger tap = right click. The decision is made where the
finger comes down, with the same window test `W3DView::pickDrawable` uses.

## Why the gestures mean what they mean

The engine's **default mouse mode** already does what a phone wants: a left click on your own unit
selects it, a left click on the ground or an enemy with a selection gives the context order, and a
short right click deselects (or cancels a GUI command / placement first). So a tap is simply a left
click and a 2‑finger tap a right click. The "alternate mouse" option would swap those meanings
under the gestures, so `GlobalData.cpp` pins it off on Android.

## Camera implementation

- **Grab pan.** When the finger has travelled more than the tap slop, the terrain point under it
  is picked (`View::screenToTerrain`, falling back to the plane at the camera pivot's ground
  height). Every rendered frame the point under the finger is re‑picked on the plane at the
  anchor's height (`View::screenToWorldAtZ`) with the previous frame's camera transform and the
  pivot is shifted by the difference (`View::userSetPosition` + `forceRedraw`). A camera
  translation moves the point under a pixel by exactly that amount, so one correction per frame
  is exact and the speed is right at every zoom. Nothing goes through `scrollBy`'s
  `SCROLL_RESOLUTION` conversion any more.
- **Fling.** Finger velocity is averaged over the last 80 ms from a fixed ring of samples (fed on
  motion events and once per frame, so a finger that stops reads as zero). On release the
  velocity is projected onto the anchor plane to get a world velocity; the camera keeps it with
  exponential decay (τ = 300 ms) until it falls below a finger‑equivalent 0.4 mm/s, a new touch
  lands, or the view clips the pivot back into the map constraints (the glide stops at the edge
  instead of pushing against it).
- **Pinch.** Height above ground = start height / pinch scale, computed in ln(height) so in and
  out feel symmetric, low‑passed (τ = 60 ms) to smooth 120 Hz jitter, and set immediately with
  `View::userSetZoom` (`W3DView::setZoom` sets desired and actual height together, so the engine's
  slow `CameraAdjustSpeed` easing does not lag the fingers). Past a limit the excess is
  rubber‑banded (asymptotic, at most ≈ 20 %) and springs back on release (τ = 120 ms). The engine's
  own clamp in `setHeightAboveGround` is not involved; the limits come from new Android‑only
  getters on `View`.
- **Pinch anchoring.** The camera sits at pivot + D·h (`W3DView::buildCameraPosition`), so a
  height change scales the view about the pivot: the ground point under a pixel moves to
  C + (Q − C)·h₁/h₀. Shifting the pivot by (Q − C)·(1 − h₁/h₀) cancels that exactly, independent
  of the legacy pan applied in the same frame.
- **Limits and start zoom.** `W3DView::setCameraHeightAboveGroundLimitsToDefault` uses absolute
  limits on Android (70 → 550) instead of the desktop aspect‑ratio compensation, which on a 20:9
  phone raised both limits by 1.9× and made an infantry unit 1.6 mm tall at the closest zoom.
  `setZoomToMax` starts a battle at 67 % of the maximum.
- **Edge pan.** While a box is open, a finger within 6 mm of an edge pans at 0.45 screen heights
  per second (× the camera‑speed slider), converted to world units through the pivot's ground
  plane at the screen centre.
- **Once per frame.** `SDL3GameEngine::update()` runs `touchFrame()` between the event pump and
  `GameEngine::update()`. `pollSDL3Events()` also runs mid‑frame from `serviceWindowsOS()`, so
  anything that integrates time or moves the camera must not live there.

## Units, density and haptics

Every threshold is in millimetres: tap slop 2.5 mm, double‑tap 4 mm, edge‑pan band 6 mm, fling
start 12 mm/s. `ZerohourActivity.getPixelsPerMm()` returns `DisplayMetrics.xdpi / 25.4`, falling
back to the logical density bucket when the panel reports an implausible value
(`TouchMetrics.pixelsPerMm`, unit‑tested); the engine calls it once over JNI
(`SDL_GetAndroidJNIEnv`, `SDL_GetAndroidActivity`) and falls back to SDL's display scale, then to
"a phone is ≈ 68 mm wide". `ZerohourActivity.hapticTick(int)` plays a short predefined vibration
(tick / click / heavy click) and is used for the long‑press arm.

## Design details that still apply

- **SDL's built‑in touch→mouse synthesis is disabled** (`SDL_HINT_TOUCH_MOUSE_EVENTS=0`,
  `SDL_HINT_MOUSE_TOUCH_EVENTS=0`) so the port can distinguish one finger from two. This hint
  **must be set before `SDL_InitSubSystem`** (in `SDL3Main.cpp`) — setting it after init does not
  reliably take effect.
- **1‑vs‑2 finger disambiguation (classic mode).** The first finger's left‑button‑down is deferred
  a few ms so a second finger can arrive first; that way a 2‑finger gesture never emits a spurious
  left click. In camera mode nothing is sent until the gesture is known (a tap clicks on release).
- **Synthesized clicks need a minimum press + motion.** A raw down‑then‑up synthesized click did
  *not* register on latching GUI buttons: the WindowManager needs the press **sustained by motion
  events across frames**, like a real held finger, plus a minimum press duration (~150 ms). Both
  are required — implemented with a time‑based deferred‑up queue that also emits a keep‑alive
  motion every frame while a press is pending. A pending up is flushed before a new press of the
  same button (the second tap of a fast double tap).
- **Double click** is signalled with `clicks = 2` on the synthesized down; `SDL3Mouse` turns that
  into `MBS_DoubleClick`.
- **Synthesized drags stay 4 px inside the screen** so the engine's own 3 px screen‑edge scroll
  never fights the touch layer's edge pan.
- **On‑screen buttons.** The ☰ (menu) and ▶▶ (skip) buttons are drawn by the engine
  (`W3DInGameUI.cpp`) and hit‑tested in the touch layer. The menu button is only shown while
  actually playing; the skip button only while a video/cutscene is on screen.

## Sensitivity slider

**Options → Scroll Speed** persists `m_keyboardScrollFactor` and is repurposed on Android as the
camera‑speed multiplier: it scales fling strength, edge‑pan speed and the legacy 2‑finger pan.
The grab pan and the pinch are direct manipulation and take no multiplier.
