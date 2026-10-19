# The phone HUD (smart HUD)

Design: [MOBILE-UX-SPEC.md §6](../MOBILE-UX-SPEC.md). This page is how it is built.

## Symptom

The original command bar is a 4:3 strip laid out for a mouse. It is always on screen, takes the
bottom 20 % of the view (`ViewportHeightScale = 0.8`), and on a 20:9 phone reads as a third of the
usable height, mostly empty: the command grid is blank whenever nothing is selected, which is most
of a match. Its buttons are ≈ 5 mm targets.

The first phone build of the new HUD then showed two more problems: **taps on the HUD's tiles fell
through to the battlefield** (a tap on a build tile sent the dozer there as a context order), and
the look was a set of unrelated boxes: raw text in dim panels, a column of six tiles even with no
groups, a `$` wrapped onto its own line, a stretched piece of radar frame art with no radar, a
drawer with holes and grey cameos that covered a quarter of the screen.

## Approach: re-lay-out the real windows

The engine populates all of the bar's functions into a fixed set of `GameWindow` buttons from
`CommandSet` data (`ControlBar.wnd:ButtonCommand01..14`, `ButtonQueue01..09`, the portrait, the
radar window `LeftHUD`, `MoneyDisplay`, `PowerWindow`, `ButtonGeneral`, the general's
`GenPowersShortcutBar*.wnd` …) and processes their clicks itself (`ControlBar::processCommandUI`
reads the `CommandButton` bound to the clicked window). The `.wnd` layout files are EA game data and
cannot be shipped modified. So on Android the bar's **own windows are moved and resized at runtime**,
and nothing about commands is re-implemented: every faction, general, mod and the multiplayer
command stream work exactly as on the PC.

## Why taps leaked, and the fix

`GameWindowManager::winProcessMouseEvent` / `GameWindow::winPointInChild`:

1. the **first child whose rectangle contains the point wins**, in child-list order;
2. a descendant is reachable only through ancestors whose rectangles contain it;
3. if the final window has `WIN_STATUS_NO_INPUT`, the manager returns no window and the click goes
   to the battlefield;
4. if the final window and every ancestor **ignore** the message (`GameWinDefaultInput`), the click
   goes to the battlefield as well.

The first build made the bar's artwork windows see-through + no-input but left them where the PC
layout put them: the bottom 20 % band, exactly where the drawer tiles sit. Rule 1 found the artwork
first, rule 3 sent the tap to the ground. The fix (`ControlBarPhoneLayout.cpp`):

- **Artwork is parked**: see-through, size 0 × 0, position (−4096, −4096). Never drawn, never hit.
- **Panels** (`CommandWindow`, `ProductionQueueWindow`, `WinUnitSelected`,
  `UnderConstructionWindow`, `OCLTimerWindow`, `BeaconWindow`) paint their own bed and use
  `GameWinBlockInput`, so a tap on a panel's empty part is swallowed (and a drag-select that ends
  over a panel is closed properly, which that function also does).
- **Containers** (`RightHUD`, the powers bar and its per-button parents) are see-through and block
  input; the layout sizes them to cover their children (rule 2).
- The **root** `ControlBarParent` spans the display, keeps `NO_INPUT` (a tap on an empty area is the
  one intended hole to the world, rule 3) and uses `GameWinBlockInput` as its input function: that
  function only ever runs for messages bubbling up from a child (rule 4), so a static text or power
  window that ignores a tap no longer leaks it.
- A window that unexpectedly holds a kept control becomes a container covering its parent (logged),
  so the control stays reachable.
- **Touch layer** (`SDL3GameEngine.cpp`): the strip band and the open drawer's footprint are dead
  zones. A finger that lands there on no window is swallowed instead of becoming a world click; a
  window found there (a tile, the minimap) is handled as a window, as before.

## Layout and look

`GameClient/AndroidHud.h` is the single source of geometry and palette for the three users (layout,
overlay drawing, touch hit-testing). Everything is in millimetres from the real display density
(`TheAndroidPixelsPerMm`, published by the touch layer at start-up).

- **Layout pass** `ControlBar::applyPhoneLayout()` runs from `ControlBar::update()` whenever
  `m_phoneLayoutDirty` is set (`init()`, `switchControlBarStage()` — every faction scheme set ends
  there — and `initSpecialPowershortcutBar()`). It restyles the tree (above), hides and parks the
  controls the phone replaces (`ButtonLarge`, `ButtonIdleWorker`, `ButtonOptions`, `GeneralsExp`,
  `ExpBarForeground`), places the strip (money text inset in its pill with a 3.0 mm font, power
  meter, star, chat and beacon in multiplayer only, radar and glow at the minimap rectangle), and
  lays the powers bar out as one row of tiles.
- **Drawer pass** `ControlBar::updatePhoneDrawer()` runs every frame from the top of
  `ControlBar::update()` (which returns early while nothing is selected) and packs the engine's
  visibility flags into one signature (18 command bits, portrait, cameo count, queue-has-items, the
  three small panels, radar, glow, hidden). On change — or while the drawer slides — it calls
  `layoutPhoneDrawer(dx)`: visible commands are packed into cells 0..n−1 (bottom-left first), the
  grid panel is sized to the rows in use, the small panels take the bottom row(s), and the info row
  above holds the portrait panel (portrait + the visible cameos, 3 per row) and, while the queue
  has items, the queue panel beside it. Panels with nothing to show are parked. The slide is
  `x += (1 − t)³ × (displayW − drawerLeft)` over 160 ms from the moment the drawer opens.
  The radar window is hidden unless `rts::localPlayerHasRadar()`, the glow unless the engine is
  flashing it (`m_radarAttackGlowOn`).
- **Backdrop** (`ControlBar::phoneDrawBackdrop`, the root's draw function, under the bar's
  windows): strip band with a hairline, money pill, power trough, beds under the star / chat /
  beacon / each shown power, the minimap bed.
- **Overlay pass** (`W3DInGameUI::draw()` after `winRepaint()`): ☰, the left column from
  `ControlBar::phoneOverlays()` (groups that hold units with a count badge, the "+" slot for the
  first empty group while something is selected, ALL, IDLE while a worker is idle), tile hairlines,
  cost pills (`getPhoneCommandCost`: `ThingTemplate` / `UpgradeTemplate::calcCostToBuild`, red when
  `Money::countMoney()` is short), lock badges on disabled tiles that are not merely recharging
  (`WIN_STATUS_NOT_READY`), and the name label of the tile under the (synthesized) mouse
  (`TheGameText->fetch(command->getTextLabel())`) beside the drawer at the tile's row.
- `W3DPushButton.cpp` (Android branch): a disabled command image is dimmed in colour
  (`0xff5c5c5c`, or `0xffb4b4b4` for `ALWAYS_COLOR`) instead of greyscale.
- `setScaledViewportHeight()` is the full height on Android, and `ShowControlBar` /
  `HideControlBar` / `animateSpecialPowerShortcut` are immediate: the slide animations capture a
  window's position at registration and would fight the layout pass.

Nothing allocates per frame or per event: `DisplayString`s for labels, badges, cost pills and the
name are created once (released in `W3DInGameUI::reset()`) and re-set only when their value
changes; the per-frame work is a few dozen flag reads.

## Building placement from a tap

The PC's flow (`PlaceEventTranslator.cpp`): the raw mouse-down anchors the pending structure
(`InGameUI::setPlacementStart`), the mouse-up makes the meta-event translator insert a
`MSG_MOUSE_LEFT_CLICK`, and that click places it. Both raw messages pass the window translator
first, which destroys any message a window used. On the phone the second device test showed the
ghost moving to the tapped spot but never being placed. Rather than depend on what the window layer
makes of a synthesized press, a tap while a structure is pending now calls `AndroidPlacementTap()`
(Android-only, in `PlaceEventTranslator.cpp`), which does the two steps directly with the same
checks (`canMakeUnit`, `isLocationLegalToBuild` with the PC's flags, the special-power construction
case) and sends the same `MSG_DOZER_CONSTRUCT` / `_LINE` order, so multiplayer stays deterministic.
The touch layer first synthesizes a motion to the spot, so the engine's ghost (and its red tint
when the spot is illegal) stays where the finger was. While placing, the camera gestures stay live
(the pending placement no longer forces classic mode), a long press is not a box-select, and a
two-finger tap is the right click that cancels. The PC's popup description window is not shown on
Android: it is laid out for the 4:3 bar and anchors to markers the phone layout parks; the touched
tile's name beside the drawer replaces it. Each placement step writes one `ANDROID placement:` line
to the log for the next device test.

## Overlay buttons

The ☰ menu and the left column are not `GameWindow`s: they are drawn in the overlay pass and
hit-tested in the touch layer, both from `ControlBar::phoneOverlays()`, so a button is tappable
exactly where and when it is drawn. A finger arms the button it lands on; release over it fires; a
group button fires its long-press action instead after 350 ms (haptic tick). The "+" slot has only
the long-press action. Actions go through the message stream the PC hotkeys use:
`MSG_META_SELECT_TEAMn` (the engine centres the camera on a quick second select),
`MSG_META_CREATE_TEAMn`, `MSG_META_SELECT_ALL`, `MSG_META_SELECT_NEXT_IDLE_WORKER`. Group presence
and counts come from `Player::getHotkeySquad()->getLiveObjects()`, IDLE from the bar's hidden
`ButtonIdleWorker`, which `InGameUI` enables while a worker is idle.

## Verified without a device

A host-side simulation (`clang++`, no NDK) compiles the touch block against stubs and drives it
with a fake clock: column packing ([2·5][+1][ALL][IDLE]), tap/hold on groups and on the "+" slot,
slide-off, menu, strip and drawer dead zones (swallowed; the same point is the battlefield once the
drawer closes), the HUD hidden (no overlays), the geometry budget (a full drawer clears the
minimap, six column slots fit, queue + portrait fit the drawer's width).

## What is not in this step

Placement flow with ✓/✗/rotate, 3D model thumbnails and sharper cameo upscaling, the command
wheel, a collapsible minimap, toasts and the left-handed option — see spec §6.5 / Phase C-2.
