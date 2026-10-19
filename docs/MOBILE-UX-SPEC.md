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
┌──────────────────────────────────────────────────────────────────────────────────┐
│[☰][$ 12500][⚡▮▮▮▯▯][★][◻][◻][◻] powers …            [chat][beacon] ┌────────┐  │ top strip, 8 mm
│                                                                     │ minimap│  │ (map only with
│[1 ·5]                                                               └────────┘  │  a radar)
│[+ 2 ]                       the battlefield, full height                        │
│[ALL ]                                                                           │
│[IDLE]                                                 ┌───────┐ ┌─────────┐     │ info row: queue
│                                                       │ queue │ │ portrait│     │ (while producing)
│                                                       └───────┘ └─────────┘     │ and portrait
│                                                       ┌─────────────────────┐   │ drawer: 3 columns
│                                                       │   ◻     ◻     ◻     │   │ of 4:3 tiles,
│                                                       │   ◻     ◻     ◻     │   │ bottom-anchored
└───────────────────────────────────────────────────────┴─────────────────────┴───┘
```

Every size comes from the display density (§3), in `GameClient/AndroidHud.h`: top strip 8 mm; ☰
10 × 8 mm; money pill 20 mm; power meter 18 × 2.6 mm; ★, chat and beacon 8 mm squares; powers
tiles 5.5–7 mm; minimap 20 × 14 mm; left-column tiles 8.5 mm; **command tiles 10.5 mm wide and
4:3** (the shape of the cameo art), shrunk only so that a full drawer (5 rows plus the portrait
panel) clears the minimap — on the POCO F7 Pro (15.5 px/mm) that gives 9.8 × 7.4 mm (152 × 114 px);
upgrade/veterancy cameos 2.8 mm; production-queue tiles 3.3 mm; gaps 0.6 mm; panel padding 0.7 mm;
hairlines 0.15 mm.

**One visual language.** Every HUD surface uses the same palette (`AndroidHud::Palette`): a dark
translucent bed, one muted-gold hairline, warm text with a shadow; a pressed or active control gets
the bright gold line. Panels have the same bed and hairline, slightly more opaque. No EA artwork is
drawn except the engine's own button images inside the tiles. **Nothing is shown without content**:
a panel, a tile, a column slot or a strip button exists only while it has something to say.

**Top strip (always, one translucent band across the top):**
- **☰ menu** at the left (10 × 8 mm): the pause / ESC menu.
- **Money** in a pill: `MoneyDisplay` on one line at a 3.0 mm font (the 4:3 bar's font wrapped the
  `$` onto its own line).
- **Power meter**: `PowerWindow` (the engine's green/yellow/red bar) in a dark trough that is
  visible even before anything produces power.
- **★ Generals star** (`ButtonGeneral`, 8 mm, on a bed): opens the promotion/science screen; the
  engine flashes it when points are available. The experience bar itself is dropped.
- **Powers row** (`GenPowersShortcutBar*.wnd`): one bed per power the engine shows, 5.5–7 mm tiles
  with the engine's ready clock and count, in what is left of the strip (≈ 70 mm in single player,
  ≈ 54 mm in multiplayer).
- **Chat** and **beacon** (8 mm, multiplayer only: the engine's own LAN/Internet test). In single
  player neither exists and the powers row takes the room.

**Minimap (top-right, 20 × 14 mm):** the engine's radar window (`LeftHUD`) moved and resized, on a
bed with a hairline, **only while the player has a radar** (`rts::localPlayerHasRadar()`); the
"base under attack" glow (`WinUAttack`) only while the engine flashes it. Tap → jump the camera,
or send the selected units (`LeftHUDInput`).

**Left column (left thumb), packed from the top, 8.5 mm tiles, each present only with content:**
- **A control group** that holds units: its number large, the unit count in a small badge. Tap =
  select (`MSG_META_SELECT_TEAMn`; the engine centres the camera on a quick second tap), **hold
  ≈ ⅓ s (haptic) = assign the current selection** (`MSG_META_CREATE_TEAMn`).
- **One "+" slot** (hairline only, with the slot's number) for the first empty group, shown only
  while something is selected: hold = assign the selection to it. A tap does nothing.
- **ALL**: select every unit (`MSG_META_SELECT_ALL`).
- **IDLE**: only while a worker is idle: select the next idle worker and centre on it
  (`MSG_META_SELECT_NEXT_IDLE_WORKER`).
With no groups and nothing selected the column is just **ALL** under the strip.

**Right drawer (right thumb):** slides in from the right edge in 160 ms (ease-out) when the engine
shows one of its context panels, i.e. only while something with commands is selected, and vanishes
with the selection.
- **Command grid**: `ButtonCommand01..14` as 4:3 tiles in **3 columns**, bottom-anchored at the
  right edge. **The visible commands are packed into the first cells** (bottom-left first, left to
  right, then up): no holes, and the drawer is exactly as tall as the commands need (1–5 rows). The
  engine decides what each tile shows and already hides unused slots, clocks not-ready powers and
  checks active toggles.
- **On every tile**: a hairline frame; a **cost pill** (`$800`) for structures, units and upgrades
  (`ThingTemplate::calcCostToBuild`, `UpgradeTemplate::calcCostToBuild`), red when the player
  cannot pay it now; a **lock badge** when the command is locked (prerequisites); locked tiles are
  **dimmed in colour, not greyscale** (`W3DPushButton`, Android branch).
- **Name label**: the name of the tile under the finger (the last tile touched: the synthesized
  mouse stays where the finger lifted) in a label beside the drawer, at the tile's row, so it never
  covers a tile. Mobile tooltips: no hover exists.
- **Info row**, directly above the rows in use: the **portrait panel** (portrait + the visible
  upgrade/veterancy cameos, 3 per row) right-aligned, and — only while something is in production —
  the **production queue panel** beside it (`ButtonQueue01..09`, 2 rows of 5; tap an item to
  cancel; the first shows its progress). Each is its own panel with its own bed; an empty queue is
  not shown.
- The **under-construction** (cancel + percent), **OCL-timer** (sell + countdown) and **beacon**
  panels occupy the grid's bottom row(s) when the engine shows them.
- The drawer's panels **block taps on their empty parts**, and the touch layer treats the drawer's
  footprint and the strip band as dead zones: a finger that lands there on no control is swallowed,
  never passed to the battlefield.

**Gone on phones:** the bar artwork (parked off-screen), the collapse button (`ButtonLarge`), the
bar's own idle-worker and options buttons (replaced by the left column and ☰), the experience bar,
the radar frame when there is no radar.

### 6.4 Mechanics

- **Hit-testing facts the layout is built on** (`GameWindowManager::winProcessMouseEvent`,
  `GameWindow::winPointInChild`): the first child whose rectangle contains the point wins;
  descendants are reachable only through ancestors whose rectangles contain them; a final window
  with `WIN_STATUS_NO_INPUT` hands the tap to the battlefield; and a message every window in the
  parent chain ignores reaches the battlefield too. So the first HUD build leaked taps: the bar's
  artwork windows, still at their PC positions in the bottom 20 % band, were hit before the tiles
  and were no-input → the tap became a context order on the ground.
- **Layout pass in `ControlBar`** (Android only, `ControlBarPhoneLayout.cpp`), whenever the bar is
  (re)built, a faction scheme is applied or the stage changes: the root `ControlBarParent` spans the
  display, keeps `NO_INPUT` (the one intended hole) and gets a draw function that paints the strip
  band and the beds; its input function blocks the messages children ignore. Every artwork window
  is **parked**: see-through, zero size, off-screen (never drawn, never hit). Panels (`CommandWindow`,
  `ProductionQueueWindow`, `WinUnitSelected`, `UnderConstructionWindow`, `OCLTimerWindow`,
  `BeaconWindow`) paint their bed and block input (`GameWinBlockInput`); containers (`RightHUD`,
  the powers bar and its per-button parents) are see-through and block input; functional leaves are
  placed by screen rectangle. A window found to hold HUD controls unexpectedly becomes a container
  covering its parent, so its controls stay reachable.
- **Drawer pass every frame**: the engine's visibility flags (which commands, portrait, cameos,
  queue items, the small panels, radar, glow, hidden) are packed into one signature; the drawer is
  laid out again only when it changes or while it slides. The slide offsets the panels' x by
  `(1 − t)³ × distance` for 160 ms from the moment the drawer opens. Panels with nothing to show are
  parked and paint nothing.
- **Backdrop and overlay**: the root's draw paints under the bar's windows (strip, money pill,
  power trough, beds, minimap frame); `W3DInGameUI` paints over them after `winRepaint()`: the
  overlay buttons, tile frames, cost pills, lock badges and the name label. Presence and geometry
  come from `ControlBar::phoneOverlays()` / `phoneMetrics()`, the same calls the touch layer uses,
  so draw, hit-test and layout cannot drift apart.
- **Touch layer**: overlay buttons are armed on press and fire on release over them (groups: hold =
  assign); the drawer footprint and the strip band swallow taps that find no window; everything else
  is unchanged from Phase A.
- **Placing a structure**: a tap on the battlefield while a structure is pending anchors and places
  it in one step through the engine's own placement checks (`AndroidPlacementTap`, the same
  construct order the PC sends), after moving the ghost there; camera gestures stay live while
  placing, a long press is not a box-select, a two-finger tap cancels. The PC's popup description is
  not shown (the name label replaces it).
- The **tactical view is always full height** on Android (`setScaledViewportHeight` → full), and
  `ShowControlBar` / `HideControlBar` / the powers-bar animation are immediate.
- Nothing allocates per frame or per event: text objects (labels, badges, cost pills, the name) are
  created once and re-set only when their value changes; the passes read a few dozen flags.

### 6.5 Later steps (not in the first HUD PR)

- **Icons without EA art in the repository** (the owner's rule: never ship EA art). Three routes,
  in order of effort: (a) the HUD's own controls are already drawn from primitives (☰, +, lock, the
  badges and pills); the strip can follow with a power bolt and a star outline so no bar art is
  needed anywhere in the chrome; (b) **better runtime rendering of the player's own cameos**: the
  64 × 48 cameo textures are upscaled 2.4× on the tile — sample them with a sharpening filter, keep
  colour with the lock badge (done), names and costs (done); (c) **3D thumbnails at runtime**:
  render each `ThingTemplate`'s W3D model once into a small texture (render-to-texture through the
  asset manager, cached per template, a fixed three-quarter camera and key light) and use it as the
  tile image. The data comes from the player's own installation, so nothing EA-made is shipped.
- **Building placement**: a ghost that follows the finger with ✓ / ✗ / rotate buttons near the
  right thumb, edge-pan while placing (today: tap a structure tile, then tap the ground; two-finger
  tap cancels).
- Collapsible minimap; tappable "base under attack" toasts that jump the camera.
- Command wheel on long-press with a selection (attack-move, force fire, guard, stop, waypoint).
- A left-handed mirror option, haptics on orders and placement, thicker selection rings and bigger
  health bars, an order ground marker.
- Phone-sized rank/science screen (it opens over the battlefield but is laid out for 4:3) — Phase B's
  UI-size setting and smart touch cover it.

## 7. Menus and every other screen

- **Smart touch (hit-area expansion)** — Phase B-1, in the touch layer, for every screen outside a
  battle and for the pause menu in a battle. When a finger lands on no interactive control, the
  press moves to the nearest enabled gadget (button, check box, radio, slider, list, text entry,
  tab, dropdown) within **4 mm**, to the point of it nearest the finger; when it lands on a
  dropdown's text, the press moves to the dropdown's arrow, so a whole row opens its list. The shift
  stays on the rest of that press, so a slider grabbed from 2 mm away follows the finger. A modal
  dialog limits the search to itself, as the hit test does. No screen layout is touched, so every
  PC screen is covered at once.
- **UI size setting — dropped.** The `.wnd` layouts are authored on an 800 × 600 canvas that the
  engine already stretches to the whole display (`GameWindowManagerScript.cpp`: × 3.0 horizontally,
  × 1.8 vertically on 2400 × 1080), so there is no room to make the canvas bigger: a scale would push
  the edge controls off the screen, and bigger fonts would not fit their 43 px rows. Smart touch
  gives those rows the reach of ~10 mm targets instead, and the Quick Battle screen bypasses the
  densest screen.
- **Quick Battle screen** — Phase B-2. One phone-first screen with big cards: army/general, map,
  opponents (count and difficulty), starting cash, then Start. It fills the engine's skirmish setup
  (`TheSkirmishGameInfo`) and starts the game the way the Skirmish screen's Start does. The full
  Skirmish screen stays reachable for power users.
- **Text:** a minimum on-screen text height of ≈ 2 mm where the HUD draws its own text (overlay
  labels are 1.9–3.6 mm); the PC screens keep their fonts.

## 8. Feedback and comfort

- Haptics (Android `Vibrator`/`HapticFeedbackConstants` via JNI) for: select, order
  acknowledged, long‑press threshold, placement, error. Respect a setting.
- Keep the 60 FPS cap. Touch processing must not allocate per event at 120 Hz.
- A left‑handed option that mirrors the left and right HUD columns.

## 9. Known small bugs to fix along the way

- Solo Play menu shows `MISSING: 'GUI:CustomMission'` (a string missing from the retail CSF).
  **Fixed (B-1):** on Android a missing label reads as a name made from its key ("Custom Mission").
- Skirmish colour column shows `???` instead of colour names. **Fixed (B-1):** that is the engine's
  label for "any colour"; on Android it reads "Random", like the random faction.
- DXVK logs `Unimplemented render state D3DRS_PATCHSEGMENTS` every frame, so `ccg_log.txt` grows
  ~13 KB/s (~46 MB per hour). **Fixed (B-1):** the state (N-patch tessellation, never rendered by
  DXVK) is not set on Android.

## 10. Delivery: phases, each a PR with green CI

**Phase A: camera and selection gestures** (biggest win, smallest risk)
- 1‑finger grab pan with fling, anchored pinch zoom, wider zoom range, closer default camera.
- Long‑press box‑select with edge‑pan, double‑tap same‑type select, 2‑finger tap deselect.
- Tap = context order when something is selected.
- Accept when: on the phone, the terrain stays under the finger while panning at min and max
  zoom; a fling glides and stops on touch; pinch zooms toward the fingers; selecting a group
  takes one long‑press‑drag; nothing regresses in menus.

**Phase B: menus usable by finger**
- B-1: smart touch everywhere and the §9 bugs. B-2: the Quick Battle screen. The UI size setting is
  dropped (§7).
- Accept when every Skirmish/Options control can be hit first try with a thumb, and a skirmish can
  be started from Quick Battle in a handful of taps.

**Phase C-1: smart HUD** (pulled ahead of Phase B at the owner's request; design in §6)
- Full-height battlefield; the control bar's real windows re-laid-out into the top strip, minimap,
  powers row and right drawer; left column with control groups, "+", ALL and IDLE; ☰ in the strip.
- Second pass after the first phone test: every HUD control wins the hit test over the battlefield
  (artwork parked, panels block, dead zones); one palette; show-with-content everywhere; packed
  4:3 tiles with costs, locks and a name label; the drawer slides in; minimap only with a radar.
- Accept when: a full skirmish (including a general with powers) can be played with no bar on
  screen while nothing is selected; a tap on any tile never reaches the ground; building, training,
  upgrades, powers, promotion, sell/repair, rally point and the minimap all work from the new
  positions; every tappable HUD control is ≥ 8 mm wide and ≥ 7 mm tall (queue items excepted).

**Phase C-2: HUD polish** — placement flow with ✓/✗/rotate, command wheel, collapsible minimap,
toasts, haptics on orders, visual feedback, group badges, left‑handed option (§6.5).

For each PR: describe exactly what to try on the phone. The owner's local session installs the
CI APK on the POCO F7 Pro, plays, and reports back with screenshots and logs.
