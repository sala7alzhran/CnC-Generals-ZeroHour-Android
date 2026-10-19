# Mobile UX spec: touch controls and phone-sized UI

This is the target design for making Zero Hour comfortable on a phone. It replaces the current
touch scheme in [CONTROLS.md](CONTROLS.md) once implemented. Everything Android-specific stays
behind `#if defined(__ANDROID__)` so the desktop build is unchanged.

## 1. What the owner reported, and what we measured

First real play session on the owner's phone (POCO F7 Pro, 6.67", 2400×1080 logical,
~15.5 px/mm on the short side):

| Element | Size on screen | Comfortable for a finger |
|---|---|---|
| Rows and dropdown arrows on the Skirmish setup screen | ≈ 43 px ≈ 2.8 mm | 9 mm (≈ 140 px) |
| Small red buttons left of the command bar | ≈ 36 px ≈ 2.3 mm | 9 mm |
| Command/build buttons in the bar | ≈ 80 px ≈ 5 mm | 9 mm |
| An infantry unit at the default camera height | ≈ 25 px ≈ 1.6 mm | — |

The owner's words: menus and buttons are too small to use, and the camera feels slow; they
weren't sure how much they could zoom.

Causes found in the code (`SDL3GameEngine.cpp` touch layer, `W3DView::scrollBy`,
`GlobalData`):

- **Pan is not direct manipulation.** 2‑finger centroid delta × `PAN_K` × slider goes into
  `userScrollBy`, which converts through `SCROLL_RESOLUTION = 250`. The terrain does not stay
  under the fingers, so it feels slow at some zoom levels and jumpy at others.
- **No inertia.** The camera stops dead when the fingers lift.
- **Panning needs two fingers.** One finger is reserved for box‑select.
- **Narrow zoom range.** `MinCameraHeight`/`MaxCameraHeight` default to 100/300 and pinch maps
  linearly to height, not anchored at the pinch point.
- **The UI is laid out for a 4:3 PC monitor and a mouse.** It scales with the screen, so a lower
  render resolution does not make it bigger. On a phone everything is ~3× too small to tap.

## 2. Reference games: what to borrow

- **Feral's mobile ports (Company of Heroes, Total War: Rome/Empire).** These are the best
  PC‑RTS→phone adaptations. Camera drag/pinch/twist "exactly as you'd expect". Tap to select,
  further taps add to the selection. A pop‑up command wheel for complex orders, with a command
  panel as an alternative. Squad/unit cards so you don't have to hit tiny soldiers.
- **C&C Red Alert (iPhone).** Box‑select is an explicit mode, not something that fights the
  camera for the same finger.
- **PC RTS convention.** Tapping a control group selects it; tapping it again quickly also
  centres the camera. A repeated tap must not change meaning based on hidden state.
- **Modern phone apps (maps, 2025–2030 games).** 1:1 grab‑to‑pan with fling inertia, zoom
  anchored under the fingers, haptic ticks on state changes, ≥ 48 dp targets, and controls in
  the thumb zones of a two‑handed landscape grip.

## 3. Design principles

1. **The map follows the finger.** Every camera gesture is direct manipulation with inertia.
2. **Thumbs, not a mouse.** Left thumb: camera and selection helpers. Right thumb: orders and
   building. Nothing that's used often goes top‑centre.
3. **Every target ≥ 9 mm.** Size from the real display density (Android `DisplayMetrics.xdpi`
   via JNI, or SDL's display content scale), not fixed pixels, so it holds on other phones.
4. **Don't make people hit soldiers.** Provide whole‑army, same‑type and group selection so
   precise unit taps are rarely needed.
5. **One gesture, one meaning.** Gestures never change meaning based on invisible state.
6. **Feedback for everything.** Light haptic on select/order/placement, a visible marker where
   an order lands, and a clear error buzz.
7. **Reuse the engine's own commands.** Route through `GameMessage` meta‑commands and the
   existing `ControlBar`/`InGameUI` code paths so multiplayer stays deterministic. Look for the
   meta‑messages the PC hotkeys already send: select all, select matching units, create/select/
   view team N.

## 4. Gesture map (target)

| Gesture | In a battle | In menus |
|---|---|---|
| 1‑finger drag | **Pan camera**, 1:1 grab, fling on release | Scroll lists / move sliders |
| Tap own unit/building | Select it (replaces selection) | Press button (smart‑touch, §7) |
| Double‑tap own unit | Select all units of that type **on screen** | — |
| Tap ground / enemy with a selection | Context order: move / attack / garrison… (= right click) | — |
| Long‑press (≈ 350 ms, haptic tick) then drag | **Box‑select**, with edge‑pan while dragging near the edges | — |
| Long‑press on ground with a selection | Command wheel: attack‑move, force fire, guard, stop, waypoint | — |
| 2‑finger pinch | Zoom, anchored at the pinch midpoint, smooth with soft limits | — |
| 2‑finger tap | Deselect all | Back |
| Tap minimap | Jump camera; drag on minimap pans | — |

Notes:
- Tap vs drag threshold should be in mm (≈ 2.5 mm), not pixels.
- Double‑tap window ≈ 300 ms and ≤ 4 mm apart.
- Don't remove the "2‑finger drag pans" behaviour all at once: keep it working too, so the
  owner can compare.

## 5. Camera spec

- **Grab pan.** At touch‑down, ray‑cast the finger to terrain (reuse whatever `InGameUI` uses
  for mouse picking, e.g. the view's screen‑to‑terrain call). Each frame, move the camera so the
  same terrain point stays under the finger. This makes speed correct at every zoom level and
  makes `PAN_K` and the slider unnecessary for pan.
- **Fling.** Track finger velocity over the last ~80 ms. On release, keep scrolling at that
  velocity with exponential decay (time constant ≈ 300 ms). Any new touch stops it.
- **Pinch zoom.** Map pinch scale ratio to camera height multiplicatively
  (`height = startHeight / scale`), and keep the terrain point under the pinch midpoint fixed.
  Ease into the limits instead of hard clamping.
- **Range.** On Android widen `MinCameraHeight`/`MaxCameraHeight` (try ≈ 70 → 450). Check that
  terrain draw distance, fog and shroud still cover the screen at max height and that frame rate
  holds. Start battles a bit closer than the PC default (units ≈ 1.5× bigger).
- **Edge pan.** While box‑selecting or placing a building, a finger within ≈ 6 mm of a screen edge
  pans the camera at a steady speed.
- **Settings.** Keep one "Camera speed" multiplier for fling strength and edge‑pan speed only.

## 6. Battle HUD (target layout, landscape, two thumbs)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ [☰]  $10000  ⚡power                              [minimap ▣] (collapsible)│
│                                                                          │
│ [1]                                                                      │
│ [2]   control groups: tap = select,          the battlefield              │
│ [3]   tap again quickly = centre camera,                                  │
│ [+]   long-press = assign current selection                               │
│                                                       ┌──────────────────┐│
│ [ALL ARMY]  [IDLE]                                    │ build / command  ││
│                                                       │ drawer (big tiles)││
└───────────────────────────────────────────────────────┴──────────────────┘┘
```

- **Left edge (left thumb):** 3–5 control‑group buttons plus "+" to make a new group, a
  **Select whole army** button, and an **Idle units/dozers** button (cycles and centres the
  camera).
- **Right edge (right thumb): the command drawer.** Replace the 4:3 command bar as the primary
  surface on phones with a drawer of large tiles (≥ 9 mm, ideally 12 mm). The tiles show what the
  current selection can do: build structures (dozer), train units (factory), abilities (unit).
  Tabs on top when the selection is mixed. Each tile shows its cost, a progress ring and a queue
  count. It's driven by the same `CommandButton` data the original bar uses, so it stays in sync
  with every faction and general.
- **Building placement.** Tap a structure tile, then a ghost follows the finger. Lift to place
  (red if invalid), with ✓ / ✗ / rotate buttons near the right thumb. Edge‑pan is active while
  placing.
- **The original command bar:** hide it on phones once the drawer covers its functions. Until
  then, at least enlarge its hit areas (§7).
- **Top strip:** money, power, menu button and a collapsible bigger minimap. Notifications
  ("Base under attack") become tappable toasts that jump the camera.
- **Visuals:** thicker selection rings and bigger health bars on Android, and a short ground
  marker where an order was given.

## 7. Menus and every other screen

- **Smart touch (hit‑area expansion).** When a tap misses every control, pick the nearest
  interactive control within ≈ 4 mm and press that. A whole dropdown row counts as tappable, not
  only its arrow. One change in the touch→click layer fixes every PC screen at once.
- **UI size setting.** Small / Medium / Large (default Large on phones). Implement it as a
  scale on the engine's window layout (laid out in a virtual canvas, so the 4:3 canvas can be
  made bigger relative to the screen and centred or anchored). If that turns out too invasive,
  fall back to scaling the touch‑relevant screens only.
- **Quick Battle screen.** One phone‑first screen with big cards: army/general, map (with
  preview), opponent difficulty and count, starting cash, then Start. It feeds the existing
  skirmish setup underneath. Keep the full Skirmish screen reachable for power users.
- **Text:** a minimum on‑screen text height of ≈ 2.2 mm.

## 8. Feedback and comfort

- Haptics (Android `Vibrator`/`HapticFeedbackConstants` via JNI) for: select, order
  acknowledged, long‑press threshold, placement, error. Respect a setting.
- Keep the 60 FPS cap. Touch processing must not allocate per event at 120 Hz.
- A left‑handed option that mirrors the left and right HUD columns.

## 9. Known small bugs to fix along the way

- Solo Play menu shows `MISSING: 'GUI:CustomMission'` (a string missing from the retail CSF).
  Provide a fallback label or hide the entry.
- Skirmish colour column shows `???` instead of colour names.
- DXVK logs `Unimplemented render state D3DRS_PATCHSEGMENTS` every frame, so `ccg_log.txt` grows
  ~13 KB/s (~46 MB per hour). Rate‑limit that warning or lower the default log level in release.

## 10. Delivery: phases, each a PR with green CI

**Phase A: camera and selection gestures** (biggest win, smallest risk)
- 1‑finger grab pan with fling, anchored pinch zoom, wider zoom range, closer default camera.
- Long‑press box‑select with edge‑pan, double‑tap same‑type select, 2‑finger tap deselect.
- Tap = context order when something is selected.
- Accept when: on the phone, the terrain stays under the finger while panning at min and max
  zoom; a fling glides and stops on touch; pinch zooms toward the fingers; selecting a group
  takes one long‑press‑drag; nothing regresses in menus.

**Phase B: menus usable by finger**
- Smart touch everywhere, the UI size setting, the Quick Battle screen, and the §9 bugs.
- Accept when every Skirmish/Options control can be hit first try with a thumb.

**Phase C: phone HUD**
- Control groups, select‑army and idle buttons, the command/build drawer, placement flow,
  command wheel, haptics, visual feedback, and the left‑handed option.
- Accept when a full skirmish can be played without touching the original command bar.

For each PR: describe exactly what to try on the phone. The owner's local session installs the
CI APK on the POCO F7 Pro, plays, and reports back with screenshots and logs.
