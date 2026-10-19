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

## 6. Battle HUD: the smart HUD (design, landscape, two thumbs)

### 6.1 The problem, measured

The original command bar is a 4:3 strip that is always on screen and takes the bottom 20 % of the
view (`ViewportHeightScale = 0.8`), with the battlefield squeezed above it. On the phone it reads
as a third of the usable height, it is dense with 5 mm targets, and most of it is empty most of the
time: the command grid is blank whenever nothing is selected, which is most of a match.

Modern mobile strategy games (Company of Heroes mobile, Total War mobile, Red Alert iOS, Northgard,
Bad North) all converge on the same answer: **the battlefield gets the whole screen; controls appear
when and where they are needed and sit in the thumb zones; nothing that is rarely used is permanent.**

### 6.2 What the owner must still be able to do

Everything the bar does stays reachable, through the engine's own data and code paths so every
faction, general and mod keeps working: build structures (dozer/worker), train units and buy
upgrades (factory, with the production queue and cancel), unit abilities, generals' powers (the
`GenPowersShortcutBar` of the chosen general, e.g. Super Weapon's or Laser's), promotions
(rank/science screen), sell and repair, rally points, evacuate/stop for garrisons, cancel
construction, beacons and chat in multiplayer, the radar/minimap with its attack glow, money and
power. The engine populates all of these into a fixed set of `GameWindow` buttons
(`ControlBar.wnd:ButtonCommand01..14`, `ButtonQueue01..09`, …) from `CommandSet` data and processes
their clicks itself. **The HUD therefore re-lays-out those real windows at runtime**; it does not
re-implement commands. (The `.wnd` layout files are EA game data and cannot be shipped modified.)

### 6.3 Layout

```
┌────────────────────────────────────────────────────────────────────────────────┐
│[☰] [$ 12,500] [⚡▮▮▮▮▯▯] [★]   [⚡ powers: ◻ ◻ ◻ … ]              ┌──────────┐│ top strip
│[1]                                                                 │ minimap  ││
│[2]                                                                 └──────────┘│
│[3]                       the battlefield, full height          ┌──────────────┐│
│[4]                                                             │ portrait|queue││ drawer
│                                                                │  ◻   ◻   ◻   ││ (only while
│[ALL]                                                           │  ◻   ◻   ◻   ││  something
│[IDLE]                                                          │  ◻   ◻   ◻   ││  is selected)
└────────────────────────────────────────────────────────────────┴──────────────┘┘
```

All sizes come from the display density (§3): a **tile is 9.5 mm**, gaps 0.6 mm, the top strip
≈ 6 mm, the minimap 23 × 17 mm. On the POCO F7 Pro (15.5 px/mm) a tile is 147 px.

**Top strip (always, translucent band, left thumb reach):**
- **☰ menu** — the existing Android overlay button, enlarged to 9 mm (it was 3.6 mm tall).
- **Money** (`MoneyDisplay`, bigger font) and the **power bar** (`PowerWindow`, the engine's own
  green/yellow/red bar). Both are small and read-only, so they may sit top-centre-left.
- **★ Generals star** (`ButtonGeneral`): opens the promotion/science screen; the engine flashes it
  when points are available. The experience bar itself is dropped (the flash is the signal).
- **Powers row** (`GenPowersShortcutBar*.wnd`, up to 11 tiles of 7.5 mm): shown by the engine only
  when the player has a power; each tile shows the engine's ready clock and multiplier. Top-centre
  is acceptable because powers are used occasionally and always need a target tap afterwards.
- **Chat/diplomacy** and **beacon** buttons: multiplayer only, small, at the end of the strip.

**Minimap (top-right, 23 × 17 mm):** the engine's radar window (`LeftHUD`) moved and resized; it
keeps its own aspect letterboxing, its tap → jump camera / move / attack-move logic (`LeftHUDInput`)
and the "base under attack" glow (`WinUAttack`). A later step makes it collapsible.

**Left column (left thumb): 4 control groups, ALL, IDLE** — Android overlay buttons (like ☰):
- **Group 1–4**: tap = select the group (`MSG_META_SELECT_TEAMn`; the engine centres the camera on
  a second quick tap), **long-press (haptic) = assign the current selection**
  (`MSG_META_CREATE_TEAMn`). A group tile is dimmed while empty.
- **ALL**: select every unit (`MSG_META_SELECT_ALL`).
- **IDLE**: select the next idle worker/dozer and centre on it (`MSG_META_SELECT_NEXT_IDLE_WORKER`);
  dimmed when none is idle (`getIdleWorkerCount()`).

**Right drawer (right thumb): the command tiles.** Visible only while the engine's context switch
shows one of its context panels (`CommandWindow`, `ProductionQueueWindow`, `UnderConstructionWindow`,
`OCLTimerWindow`, `BeaconWindow`), i.e. only while something is selected that has commands:
- `ButtonCommand01..14` become **9.5 mm tiles in a 3-column grid**, bottom-anchored at the right
  edge (slot order left→right, top→bottom, so slot 1 is where the PC's first button is). The
  engine already decides what each tile shows — unit, structure, upgrade, ability, sell, repair,
  rally point, stop/evacuate for garrisons, common commands for a mixed selection — and already
  greys unaffordable, clocks not-ready, checks active and hides unused slots. Button art is
  stretched to the tile by the renderer.
- Above the grid, the **portrait** (`CameoWindow` + the 5 veterancy/upgrade cameos) *or* the
  **production queue** (`ButtonQueue01..09`, 2 rows of 5.5 mm tiles, first one with the progress
  clock); the engine already swaps those two while producing.
- The under-construction (cancel + percent), OCL-timer and beacon panels occupy the same area.
- The drawer panels get a translucent dark background with the faction's border colour, drawn by
  a custom draw function on the panel window, and swallow taps on their empty parts so a unit
  under the drawer is never hit by accident.

**Gone on phones:** the bar artwork (background/foreground/top/grid draws go see-through), the
collapse button (`ButtonLarge`), the bar's own idle-worker and options buttons (replaced by the left
column and ☰), the experience bar.

### 6.4 Mechanics

- A **layout pass in `ControlBar`** (Android only, `ControlBarPhoneLayout.cpp`) runs whenever the
  bar is (re)built, a faction scheme is applied or the stage changes (those are the moments the
  engine itself repositions windows): the root `ControlBarParent` is resized to the whole display
  and made see-through + no-input so empty areas pass taps to the battlefield; every window of the
  tree is made see-through + no-input except the functional leaves, which are placed by the table
  above; the five panel windows get the panel draw/input functions. The layout code reads
  `TheAndroidPixelsPerMm`, published by the touch layer at start-up.
- The **tactical view is always full height** on Android (`setScaledViewportHeight` → full), and
  `ShowControlBar`/`HideControlBar` are immediate (no slide animation that would fight the pass).
- The **left column and ☰** are overlays: drawn in `W3DInGameUI` after the window system, hit-tested
  in the touch layer, with geometry from one shared header (`AndroidHud.h`) so draw, hit-test and
  layout can never drift apart.
- Nothing allocates per frame; the pass costs a few dozen `winSetPosition`/`winSetSize` calls and
  runs only when dirty.

### 6.5 Later steps (not in the first HUD PR)

- **Building placement**: a ghost that follows the finger with ✓ / ✗ / rotate buttons near the
  right thumb, edge-pan while placing (today: tap a structure tile, then tap the ground; two-finger
  tap cancels).
- Collapsible minimap; tappable "base under attack" toasts that jump the camera.
- Command wheel on long-press with a selection (attack-move, force fire, guard, stop, waypoint).
- Group tile badges (unit count), a left-handed mirror option, haptics on orders and placement,
  thicker selection rings and bigger health bars, an order ground marker.
- Phone-sized rank/science screen (it opens over the battlefield but is laid out for 4:3) — Phase B's
  UI-size setting and smart touch cover it.

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

**Phase C-1: smart HUD** (pulled ahead of Phase B at the owner's request; design in §6)
- Full-height battlefield; the control bar's real windows re-laid-out into the top strip, minimap,
  powers row and right drawer; left column with control groups, ALL and IDLE; bigger ☰.
- Accept when: a full skirmish (including a general with powers) can be played with no bar on
  screen while nothing is selected; building, training, upgrades, powers, promotion, sell/repair,
  rally point and the minimap all work from the new positions; every HUD control is ≥ 9 mm.

**Phase C-2: HUD polish** — placement flow with ✓/✗/rotate, command wheel, collapsible minimap,
toasts, haptics on orders, visual feedback, group badges, left‑handed option (§6.5).

For each PR: describe exactly what to try on the phone. The owner's local session installs the
CI APK on the POCO F7 Pro, plays, and reports back with screenshots and logs.
