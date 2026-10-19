# The phone HUD (smart HUD)

Design: [MOBILE-UX-SPEC.md §6](../MOBILE-UX-SPEC.md). This page is how it is built.

## Symptom

The original command bar is a 4:3 strip laid out for a mouse. It is always on screen, takes the
bottom 20 % of the view (`ViewportHeightScale = 0.8`), and on a 20:9 phone reads as a third of the
usable height, mostly empty: the command grid is blank whenever nothing is selected, which is most
of a match. Its buttons are ≈ 5 mm targets.

## Approach: re-lay-out the real windows

The engine populates all of the bar's functions into a fixed set of `GameWindow` buttons from
`CommandSet` data (`ControlBar.wnd:ButtonCommand01..14`, `ButtonQueue01..09`, the portrait, the
radar window `LeftHUD`, `MoneyDisplay`, `PowerWindow`, `ButtonGeneral`, the general's
`GenPowersShortcutBar*.wnd` …) and processes their clicks itself (`ControlBar::processCommandUI`
reads the `CommandButton` bound to the clicked window). The `.wnd` layout files are EA game data and
cannot be shipped modified. So on Android the bar's **own windows are moved and resized at runtime**,
and nothing about commands is re-implemented: every faction, general, mod and the multiplayer
command stream work exactly as on the PC.

`ControlBarPhoneLayout.cpp` (Android only, `#if defined(__ANDROID__)`):

- **Layout pass** `ControlBar::applyPhoneLayout()` runs from `ControlBar::update()` whenever
  `m_phoneLayoutDirty` is set. The engine's own repositioning points mark it: `init()`,
  `switchControlBarStage()` (which every faction scheme set ends in — `ControlBarScheme::init`
  moves the money/power/buttons to their PC places) and `initSpecialPowershortcutBar()`.
- The root `ControlBarParent` is resized to the whole display and made `WIN_STATUS_SEE_THRU |
  WIN_STATUS_NO_INPUT`. Hit-testing ignores see-through but **discards no-input windows**
  (`GameWindowManager::getWindowUnderCursor`), so a tap on an empty area falls through to the
  battlefield; drawing does not clip children to their parent. Every window in the tree that is
  neither a functional leaf nor a panel gets the same two flags (the bar artwork, markers and
  containers stop drawing and stop taking input; neither flag affects children). The observer panels
  are left alone.
- **Leaves** are placed by screen rectangle (`placePhoneWindow` converts to the parent-relative
  position the window system stores). Static texts get a readable font via
  `GadgetStaticTextSetFont` (the `.wnd` fonts are PC-sized).
- **Panels** (`CommandWindow`, `ProductionQueueWindow`, `WinUnitSelected`, `UnderConstructionWindow`,
  `OCLTimerWindow`, `BeaconWindow`) get a draw function that paints a translucent panel behind their
  children and an input function that swallows the taps that miss a tile.
- **Drawer** `ControlBar::updatePhoneDrawer()` runs every frame: it counts the command grid rows in
  use (the engine hides unused slots), and when that changes it moves the grid panel, the tiles, and
  the info area (portrait + upgrade cameos, or the production queue — the engine already swaps those
  two) so the drawer grows upward from the bottom-right corner. Command slot *i* fills column `i % 4`
  of row `i / 4` **from the bottom**, so the first commands sit under the right thumb.
- Dropped on phones (hidden and parked off-screen): the collapse button `ButtonLarge`, the bar's
  own `ButtonIdleWorker` and `ButtonOptions` (replaced by the overlays), `GeneralsExp` and
  `ExpBarForeground` (the flashing ★ signals a promotion).
- `setScaledViewportHeight()` is the full height on Android, and `ShowControlBar` /
  `HideControlBar` / `animateSpecialPowerShortcut` are immediate: the slide animations capture a
  window's position at registration and would fight the layout pass.

## Geometry: one header

`GameClient/AndroidHud.h` computes the whole layout from the display size and the physical density
(`TheAndroidPixelsPerMm`, published by the touch layer at start-up): tile 9.5 mm, gap 0.6 mm, top
strip 8 mm, minimap 20 × 14 mm, a 4 × 4 command grid, a 2 × 5 production queue of 5.5 mm tiles. The
tile shrinks only if the left column or the full drawer would not fit the display. Three users read
it, so draw, hit-test and layout cannot drift apart: the layout pass, the overlay drawing in
`W3DInGameUI.cpp`, and the overlay hit-testing in `SDL3GameEngine.cpp`.

## Overlay buttons

The ☰ menu and the left column (control groups 1–4, ALL, IDLE) are not `GameWindow`s: they are
drawn in `W3DInGameUI::draw()` after the window system and hit-tested in the touch layer. A finger
arms the button it lands on; release over it fires; a group button fires its long-press action
instead after 350 ms (haptic tick). Actions go through the same message stream the PC hotkeys use:
`MSG_META_SELECT_TEAMn` (the engine centres the camera on a quick second select),
`MSG_META_CREATE_TEAMn`, `MSG_META_SELECT_ALL`, `MSG_META_SELECT_NEXT_IDLE_WORKER`. Group tiles are
dimmed while empty (`Player::getHotkeySquad`), IDLE while nobody is idle (`getIdleWorkerCount`).

## What is not in this step

Placement flow with ✓/✗/rotate, the command wheel, a collapsible minimap, toasts, group badges and
the left-handed option — see spec §6.5 / Phase C-2.
