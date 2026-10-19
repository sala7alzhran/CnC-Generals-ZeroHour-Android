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

### 6.3 Target layout (the owner's approved concept, 19 Oct 2026)

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ (☰)  ( $  99,000   ⚡▮▮▮▮▯ )                                      (★2)  ┌─────────┐ │ strip: round menu,
│                                                                          │ minimap │ │ one resource pill;
│ (IDLE·2)                                                                 └─────────┘ │ rank badge + map
│ (ARMY)                         the battlefield, full height            (◎) (◎) (◎)  │ powers: round, ring
│ ( + )                                                                Particle 4:32  │ superweapon timers
│ (1·5)                                                                                │ groups once created
│                                                   ┌────────────────────────────────┐ │
│                                                   │ [img] Dozer ▬▬▬▬▬  BASE DEF SUP │ │ chip + tabs
│                                                   │ [tile] [tile] [tile] [tile]     │ │ ≤ 2 rows × 4
│                                                   │ [tile] [tile] [tile] [tile]     │ │ name + price
│                                                   └────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────────────────┘
```

One visual language everywhere: dark translucent panels, a thin gold outline (0.15 mm), rounded
corners (r ≈ 1.2 mm; drawn as a rect stack plus arc segments — the renderer has no rounded
primitive), one font (the game's bold Arial at 1.6 / 2.2 / 3.0 / 3.6 mm), warm text with a shadow,
bright gold for pressed or available, muted for locked. The centre of the screen stays clear. All
sizes in mm from the display density (`AndroidHud.h`). No EA artwork is shipped or drawn except the
player's own cameo images inside tiles and the real radar.

**Top-left.** A **round ☰ button** (10 mm) and **one resource pill** (≈ 38 × 8 mm): a gold `$` coin
glyph, the money with a thousands separator, a ⚡ glyph and a **power bar** (14 × 2 mm; green when
production covers consumption, amber inside the engine's yellow range, red in deficit). Money and
power are read from the viewed player's `Money` and `Energy` and drawn by the HUD; the engine's
`MoneyDisplay` and `PowerWindow` are hidden.

**Top-right.** The **minimap** (20 × 14 mm) in a thin gold rounded frame, only while the player has
a radar (`rts::localPlayerHasRadar`); the **rank badge** beside it (9 mm circle: star glyph and the
unspent promotion points, `Player::getSciencePurchasePoints()`, pulsing while > 0; it is the engine's
`ButtonGeneral` with our draw function, so a tap opens the promotion screen as on the PC); under the
map the **general's powers as round buttons** (9 mm, up to 3 per row) with a **cooldown ring**
(`Player::findMostReadyShortcutSpecialPowerOfType` → readiness percent; the engine's shortcut
buttons with our draw function, the cameo inset to 70 %). **Superweapon countdowns** move under the
powers (Android override of `SuperweaponCountdownPosition`), **named mission timers** under the
left column (`NamedTimerCountdownPosition`).

**Left column** (left thumb, round 10 mm buttons, packed from the top, nothing without content):
**IDLE** with the real worker picture (the idle worker's cameo) and a count badge, only while a
worker is idle; **ARMY** (people glyph; select all); **+** (assign the selection to the first free
group; only while something is selected); **numbered groups** (number + unit-count badge) only once
created: tap = select, quick second tap = centre, hold = reassign.

**Bottom-right drawer** (right thumb), only while the selection has commands:
- **Selected-unit chip**: portrait (8 mm), name (`ThingTemplate::getDisplayName`), health bar
  (`BodyModule`), and the unit count for a multi-selection.
- **Tabs** for builder menus: **BASE / DEFENSE / SUPPORT** by the structure's KindOf (BASE:
  `FS_POWER`, `FS_FACTORY`, `FS_BARRACKS`, `FS_SUPPLY_CENTER`, `FS_AIRFIELD`, `FS_INTERNET_CENTER`,
  `COMMANDCENTER`; DEFENSE: `FS_BASE_DEFENSE`; SUPPORT: `FS_TECHNOLOGY`, `FS_ADVANCED_TECH`,
  `FS_SUPERWEAPON`, `FS_SUPPLY_DROPZONE`, `FS_BLACK_MARKET`, `FS_STRATEGY_CENTER`, `FS_FAKE`, the
  rest). Any other menu with more than 8 commands gets a **MORE** page. Within a page the PC's
  command-set order is kept. Tiles not on the active page are parked off-screen by the layout, not
  hidden, so the engine's own population and enable/disable logic keeps running on them.
- **Tiles**: at most **2 rows × 4 columns**, 9.5 mm wide: the cameo (4:3) above a two-line **name**
  (1.6 mm) and the **price** (gold; red when unaffordable). Available = gold outline; locked (the
  engine disabled the button) = muted picture + padlock; recharging powers keep the engine's clock.
- **Production queue**: a row of 3.5 mm thumbnails under the chip while producing, the first with a
  progress ring; tap = cancel (`ButtonQueue01..09`).
- **Under construction / OCL timer / beacon** panels take the tile area: one big cancel or sell
  button with the progress text.

### 6.4 HUD states

| # | State (engine fact) | What the HUD shows | Verified |
|---|---|---|---|
| S0 | Nothing selected (`CB_CONTEXT_NONE`) | strip, minimap if radar, powers if any, rank badge, left column (IDLE only if idle, ARMY, groups); no drawer | code + simulation (column packing) |
| S1 | One unit selected (`CB_CONTEXT_COMMAND`) | chip; its command tiles (abilities, stop, guard, attack-move, switch weapon, select-all-of-type…) | code (tiles are the engine's buttons); device |
| S2 | Builder selected: USA/China dozer, GLA worker, Laser/Super-weapon dozers | chip; BASE / DEFENSE / SUPPORT tabs; structure tiles with name and price | code (KindOf tab rule); device per army |
| S3 | Production structure selected | chip; unit / upgrade tiles with price; queue row while producing; sell, rally point | code; device per army |
| S4 | Structure under construction (`CB_CONTEXT_UNDER_CONSTRUCTION`) | chip; cancel + percent | code; device |
| S5 | Garrisoned structure / transport / tunnel (`CB_CONTEXT_STRUCTURE_INVENTORY`) | chip; one tile per occupant (exit), evacuate | code; device (GLA tunnel, USA Chinook, China Helix) |
| S6 | Multi-selection (`CB_CONTEXT_MULTI_SELECT`) | chip "N units"; common commands only | code; device |
| S7 | Placement pending (`getPendingPlaceType`) | ghost follows the finger; tap = place (`AndroidPlacementTap`); drag/pinch still move the camera; two-finger tap cancels | simulation + code; device |
| S8 | Targeting (special power, attack-move, guard, rally, combat drop, hijack, car bomb, sabotage, beacon) | the engine's cursor hint; tap = target (`MSG_MOUSE_LEFT_CLICK` with a point region); two-finger tap cancels (`GUICommandTranslator`, `SelectionXlat`) | code (same messages as the PC); device |
| S9 | Superweapon charging / ready | countdown under the powers; the shortcut's ring fills; tap → target tap | code; device |
| S10 | Promotion points available | rank badge pulses with the count; tap opens the promotion screen (PC layout; smart touch) | code; device |
| S11 | Multiplayer | chat and beacon buttons in the strip; beacon panel in the drawer | code; later |
| S12 | Observer / replay | the engine's observer windows unchanged | out of scope |
| S13 | Base under attack | minimap frame glows (`WinUAttack`); C-2: tappable toast | code; device |
| S14 | Idle worker exists | IDLE button with worker picture and count | code (idle list via an Android accessor on `InGameUI`); device |
| S15 | OCL timer structure (`CB_CONTEXT_OCL_TIMER`) | countdown + sell in the drawer | code; device |

### 6.5 Command types → HUD

Every `GUICommandType` the control bar can put on a button (`ControlBar.h`). The tile is always the
engine's own button, so the click handling (`ControlBar::processCommandUI`) is the PC's; what the
HUD adds is where it sits, how it looks, and the touch flow around it.

| Command type | Where / how on the phone | Verified | Status |
|---|---|---|---|
| `DOZER_CONSTRUCT` | builder drawer, tabbed; price; tap → placement (S7) | simulation (tap flow), code | placement fix in step 1; tabs in step 2 |
| `DOZER_CONSTRUCT_CANCEL` | under-construction panel (S4) | code | device |
| `UNIT_BUILD` | factory drawer; price; queue row | code | device |
| `CANCEL_UNIT_BUILD`, `CANCEL_UPGRADE` | queue thumbnails | code | device |
| `PLAYER_UPGRADE`, `OBJECT_UPGRADE` | drawer tile; price; lock while prerequisites are missing; check while owned | code | device |
| `ATTACK_MOVE`, `GUARD`, `GUARD_WITHOUT_PURSUIT`, `GUARD_FLYING_UNITS_ONLY` | unit drawer; then a target tap (S8) | code | device |
| `STOP` | unit drawer | code | device |
| `WAYPOINTS` | needs Alt-click on the PC: not exposed on the phone (C-2 command wheel) | — | deferred |
| `EXIT_CONTAINER`, `EVACUATE`, `EXECUTE_RAILED_TRANSPORT` | occupant tiles / evacuate (S5) | code | device |
| `SET_RALLY_POINT` | structure drawer; target tap | code | device |
| `SELL` | structure drawer | code | device |
| `FIRE_WEAPON` | unit drawer; target tap | code | device |
| `SPECIAL_POWER` (from a unit or structure) | drawer tile with the engine's clock; target tap if it needs one | code | device |
| `SPECIAL_POWER_FROM_SHORTCUT` | powers row: round button with cooldown ring; target tap if needed | code | device per general |
| `SPECIAL_POWER_CONSTRUCT`, `…_FROM_SHORTCUT` (sneak attack) | placement flow; `AndroidPlacementTap` takes the special-power branch | code | device (GLA) |
| `PURCHASE_SCIENCE` | the promotion screen opened by the rank badge (PC layout, smart touch) | code | device |
| `HACK_INTERNET` | hacker drawer toggle | code | device (China) |
| `TOGGLE_OVERCHARGE` | nuclear reactor drawer toggle | code | device (China) |
| `COMBATDROP` | Chinook drawer; target building tap | code | device (USA) |
| `SWITCH_WEAPON` | unit drawer toggle | code | device |
| `GUICOMMANDMODE_HIJACK_VEHICLE`, `…CONVERT_TO_CARBOMB`, `…SABOTAGE_BUILDING`, `…PICK_UP_PRISONER` | unit drawer; then a target tap | code | device (GLA) |
| `GUICOMMANDMODE_PLACE_BEACON`, `BEACON_DELETE` | strip beacon button / beacon panel (multiplayer) | code | later |
| `SELECT_ALL_UNITS_OF_TYPE` | drawer tile; also a double tap on a unit | code + simulation (double tap) | device |
| `POW_RETURN_TO_PRISON` | not used by retail Zero Hour | — | n/a |

### 6.6 Armies and generals

All twelve playable armies drive the same code paths above; the only per-army differences the HUD
must respect are the worker type, which menus get tabs, which powers fill the powers row, and the
superweapon. The command sets come from the player's own game data at runtime (`CommandSet.ini`,
`CommandButton.ini`), so nothing here is hard-coded per army.

| Army | Worker (S2) | HUD-relevant specifics | Powers row (typical) | Superweapon |
|---|---|---|---|---|
| USA | Dozer | Chinook combat drop (S8), strategy-centre plans, supply drop zone, Patriot/Fire Base in DEFENSE | A-10 strike, spy drone, paradrop, fuel-air bomb, spectre gunship, emergency repair, leaflet drop | Particle Cannon |
| USA Super Weapon | Dozer | cheaper Particle Cannon, EMP Patriot, alpha aurora | as USA + more particle cannons | Particle Cannon |
| USA Laser | Dozer | laser turret in DEFENSE, laser tanks/avenger; power-hungry base: the power bar matters | as USA | Particle Cannon |
| USA Air Force | Dozer | airfield-centred army; carpet bomb; no tanks | as USA + carpet bomb, more A-10s | Particle Cannon |
| China | Dozer, Hacker (`HACK_INTERNET`) | nuclear reactor overcharge toggle, propaganda centre, speaker tower, mines upgrade, Helix transport (S5) | artillery barrage, cluster mines, cash hack, EMP pulse, frenzy, carpet bomb | Nuclear Missile |
| China Tank | Dozer, Hacker | Emperor overlord, tank drops | as China | Nuclear Missile |
| China Infantry | Dozer, Hacker | mini-gunners, attack outposts, fortified bunkers (S5) | as China + infantry drops | Nuclear Missile |
| China Nuke | Dozer, Hacker | nuclear reactor overcharge, nuke cannons, nuclear tank shells upgrade | as China | Nuclear Missile |
| GLA | Worker (builds and gathers) | tunnel network (S5), hijacker / terrorist / saboteur target modes, black market upgrades, demo trap, arms dealer | rebel ambush, cash bounty, anthrax bomb, GPS scrambler, sneak attack (`SPECIAL_POWER_CONSTRUCT`), emergency repair | Scud Storm |
| GLA Toxin | Worker | toxin tractors, anthrax beta/gamma upgrades | as GLA + anthrax bomb | Scud Storm |
| GLA Demolition | Worker | demo traps, suicide bikes/combat cycles, advanced demo trap | as GLA | Scud Storm |
| GLA Stealth | Worker | camouflage everywhere, GPS scrambler, hijackers | as GLA + GPS scrambler | Scud Storm |

Verification without a device: the tile is the engine's button for every one of these, the
placement tap is simulated, the column and drawer geometry are simulated, and the tab rule is a pure
function of KindOf flags. What only a device can confirm is the look at real density and that each
army's data-driven menus fit the 2-row pages (the counts above are from the retail game; the MORE
page catches anything larger).

**Per-army phone checklist** (for the local session; the same seven steps for each of the twelve,
plus the army's own line):

1. Start a skirmish as the army. S0: strip, ARMY, no drawer; minimap appears once the radar exists.
2. Select the worker → BASE / DEFENSE / SUPPORT tabs; build power, barracks, supply, factory; each
   placement completes and money drops; try one illegal spot (red ghost, no charge).
3. Select the factory → train two units; the queue row shows progress; cancel one; set a rally point.
4. Buy one upgrade; the tile shows the price, then the owned state.
5. Earn a promotion → the rank badge pulses with the count → buy a science (PC screen, smart touch).
6. Use the first power when its ring completes (target tap where needed); build the superweapon
   when affordable and fire it from the powers row.
7. Groups: select units, hold **+** → group 1 appears; tap / double-tap / hold to reassign; IDLE shows
   the worker picture and count while a worker idles.
- USA: Chinook combat drop into a building; strategy centre plan toggles. China: hacker hack-internet
  toggle; nuclear reactor overcharge; Helix load/unload (S5). GLA: worker gathers supplies between
  builds; tunnel network garrison and evacuate (S5); hijacker and terrorist target modes; sneak
  attack placement. Laser: laser turret in DEFENSE and the power bar going amber. Air Force: carpet
  bomb target tap. Super Weapon: EMP Patriot in DEFENSE. Tank: Emperor. Infantry: bunker garrison.
  Nuke: overcharge then the nuke countdown. Toxin: anthrax upgrade. Demolition: demo trap placement.
  Stealth: GPS scrambler power.

### 6.7 Mechanics

- **Hit-testing facts the layout is built on** (`GameWindowManager::winProcessMouseEvent`,
  `GameWindow::winPointInChild`): the first child whose rectangle contains the point wins;
  descendants are reachable only through ancestors whose rectangles contain them; a final window
  with `WIN_STATUS_NO_INPUT` hands the tap to the battlefield; and a message every window in the
  parent chain ignores reaches the battlefield too. The first HUD build leaked taps because the bar's
  artwork windows, still at their PC positions in the bottom 20 % band, were hit before the tiles.
- **Layout pass in `ControlBar`** (Android only, `ControlBarPhoneLayout.cpp`), whenever the bar is
  (re)built, a faction scheme is applied or the stage changes: the root `ControlBarParent` spans the
  display, keeps `NO_INPUT` (the one intended hole) and gets a draw function that paints the backdrop;
  its input function blocks the messages children ignore. Every artwork window is **parked**:
  see-through, zero size, off-screen. Panels paint their bed and block input (`GameWinBlockInput`);
  containers are see-through and block input; functional leaves are placed by screen rectangle. The
  engine's buttons that the target re-skins (rank badge, powers, tiles) keep their input and system
  functions and get an Android draw function; the chip, pill, tabs and column are drawn by the HUD.
- **Drawer pass every frame**, from the top of `ControlBar::update()` (which returns early while
  nothing is selected): the engine's visibility flags are packed into one signature; the drawer is
  laid out again only when it changes or while it slides in (160 ms, `(1 − t)³`). Pages: tiles that
  are not on the active tab are parked by this pass, never hidden, so the engine keeps populating and
  enabling them.
- **Backdrop and overlay**: the root's draw paints under the bar's windows; `W3DInGameUI` paints over
  them after `winRepaint()`. Presence and geometry come from `ControlBar::phoneOverlays()` /
  `phoneMetrics()`, the same calls the touch layer uses, so draw, hit-test and layout cannot drift.
- **Touch layer**: overlay buttons are armed on press and fire on release over them (groups: hold =
  assign); the drawer footprint and the strip band swallow taps that find no window.
- **Placing a structure**: a tap on the battlefield while a structure is pending anchors and places
  it in one step through the engine's own placement checks (`AndroidPlacementTap`, the same
  construct order the PC sends), after moving the ghost there; camera gestures stay live while
  placing, a long press is not a box-select, a two-finger tap cancels. The PC's popup description is
  not shown (the name on the tile replaces it).
- The **tactical view is always full height** on Android, and `ShowControlBar` / `HideControlBar` /
  the powers-bar animation are immediate.
- Nothing allocates per frame or per event: text objects are created once and re-set only when their
  value changes; the passes read a few dozen flags.

### 6.8 Later steps (Phase C-2)

- **Placement flow**: ✓ / ✗ / rotate buttons near the right thumb and edge-pan while placing (today:
  tap to place, two-finger tap to cancel).
- **Icons without EA art in the repository**: the HUD's own controls are drawn from primitives; next
  the player's own cameos get a sharper upscale, and **3D thumbnails** rendered at runtime from each
  template's model (render-to-texture, cached per template) can replace the cameo on the tile.
- Tappable "base under attack" toasts; a command wheel on long-press (attack-move, force fire,
  guard, stop, waypoints); a left-handed mirror option; haptics on orders; thicker selection rings
  and bigger health bars; an order ground marker; a phone-sized promotion screen.

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
- Third pass, after the second phone test, as two PRs: **step 1** placement from a tap
  (`AndroidPlacementTap`), the drawer pass at the top of `update()`, no `&` in names; **step 2** the
  visual redesign to the approved target (§6.3): round menu button and one resource pill, rank badge
  and round powers with cooldown rings beside and under the framed minimap, round left column with
  the real worker picture, the drawer with chip, BASE / DEFENSE / SUPPORT tabs and ≤ 2 rows of named,
  priced tiles. Planned in §6.4–6.6 (states, command types, all twelve armies) before coding.
- Accept when: a full skirmish (including a general with powers) can be played with no bar on
  screen while nothing is selected; a tap on any tile never reaches the ground; building, training,
  upgrades, powers, promotion, sell/repair, rally point and the minimap all work from the new
  positions; every tappable HUD control is ≥ 8 mm wide and ≥ 7 mm tall (queue items excepted).

**Phase C-2: HUD polish** — placement flow with ✓/✗/rotate, command wheel, collapsible minimap,
toasts, haptics on orders, visual feedback, 3D thumbnails, left‑handed option (§6.8).

For each PR: describe exactly what to try on the phone. The owner's local session installs the
CI APK on the POCO F7 Pro, plays, and reports back with screenshots and logs.
