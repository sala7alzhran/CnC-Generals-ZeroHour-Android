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
drawer with holes and grey cameos that covered a quarter of the screen. The second build fixed the
taps but kept raw `IDLE` / `ALL` text boxes, a lone rank star and a strip of square power cameos; the
owner then drew the target (spec §6.3) that the third build implements.

## Approach: re-lay-out the real windows, draw everything else

The engine populates all of the bar's functions into a fixed set of `GameWindow` buttons from
`CommandSet` data (`ControlBar.wnd:ButtonCommand01..14`, `ButtonQueue01..09`, the portrait, the
radar window `LeftHUD`, `ButtonGeneral`, the general's `GenPowersShortcutBar*.wnd` …) and processes
their clicks itself (`ControlBar::processCommandUI` reads the `CommandButton` bound to the clicked
window). The `.wnd` layout files are EA game data and cannot be shipped modified. So on Android the
bar's **own windows are moved and resized at runtime**, the buttons the design re-skins get an
Android **draw function** (their input and system functions, and so the command processing, stay the
engine's), and every other HUD surface is **drawn from primitives** by the HUD itself: no artwork is
shipped, nothing about commands is re-implemented, and every faction, general, mod and the
multiplayer command stream work exactly as on the PC.

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
- **Panels are containers** (`CommandWindow`, `ProductionQueueWindow`, `WinUnitSelected`,
  `RightHUD`, `UnderConstructionWindow`, `OCLTimerWindow`, `BeaconWindow`, the powers bar and its
  per-button parents): see-through and `GameWinBlockInput`, sized by the layout to cover their
  children (rule 2), so a tap on a panel's empty part is swallowed (and a drag-select that ends over
  a panel is closed properly, which that function also does). The window manager does not call a
  see-through window's draw function, only its children's, so the root paints their beds.
- The **root** `ControlBarParent` spans the display, keeps `NO_INPUT` (a tap on an empty area is the
  one intended hole to the world, rule 3) and uses `GameWinBlockInput` as its input function: that
  function only ever runs for messages bubbling up from a child (rule 4), so a window that ignores a
  tap no longer leaks it.
- A window that unexpectedly holds a kept control becomes a container covering its parent (logged),
  so the control stays reachable.
- **Touch layer** (`SDL3GameEngine.cpp`): the resource pill, the minimap block (map, rank badge,
  powers, countdown line) and the open drawer's footprint are dead zones. A finger that lands there
  on no window is swallowed instead of becoming a world click; a window found there (a tile, the
  minimap) is handled as a window, as before. The top centre between the pill and the minimap block
  is battlefield.

## Layout and look

`GameClient/AndroidHud.h` is the single source of geometry and palette for the two users (the
layout + draw in `ControlBarPhoneLayout.cpp`, the touch hit-testing in `SDL3GameEngine.cpp`).
Everything is in millimetres from the real display density (`TheAndroidPixelsPerMm`, published by
the touch layer at start-up), and the layout adapts: tiles are 9 mm wide but shrink (never below
7 mm) so a chip row and two tile rows fit under the minimap block; the left column's buttons are
10 mm but shrink (never below 7.5 mm) so six fit; on a short phone with two rows of powers the power
buttons shrink to 7 mm and then the minimap down to 11 mm tall, so the drawer always clears them.

- **Round shapes** come from four Android-only `Display` calls (`drawFillCircle`, `drawArc`,
  `drawFillRoundRect`, `drawOpenRoundRect`, plus `drawFillTriangle`), implemented in
  `W3DDisplay.cpp` as triangle fans added straight to the 2D batch the rest of the frame uses: a
  10 mm button is a few dozen triangles, not a stack of one-pixel rectangles.
- **Layout pass** `ControlBar::applyPhoneLayout()` runs from `ControlBar::update()` whenever
  `m_phoneLayoutDirty` is set (`init()`, `switchControlBarStage()` — every faction scheme set ends
  there — `initSpecialPowershortcutBar()`, and whenever the number of powers on show changes, since
  their rows shape the room the drawer gets). It restyles the tree (above), hides and parks the
  controls the phone replaces (`ButtonLarge`, `ButtonIdleWorker`, `ButtonOptions`, `GeneralsExp`,
  `ExpBarForeground`, `MoneyDisplay`, `PowerWindow`), places the rank badge, the radar and its glow,
  chat and beacon (multiplayer only, bottom-left), and installs the Android draw functions on the
  command tiles, the power shortcuts and the rank badge.
- **Drawer pass** `ControlBar::updatePhoneDrawer()` runs every frame from the top of
  `ControlBar::update()` (which returns early while nothing is selected) and packs the engine's
  visibility flags into two signatures (18 command bits, portrait, cameo count, queue-has-items, the
  three small panels, radar, glow, hidden; the powers on show, the tab, the page). On change — or
  while the drawer slides — it calls `layoutPhoneDrawer(dx)`: the visible commands are classified
  (a builder's menu: BASE / DEF / SUP by the structure's KindOf; any other menu of more than a page:
  UNITS / TECH / MORE by command type; ten or fewer: one page, no tabs), the active tab's page is laid
  into up to two rows of five tiles in reading order hugging the drawer's bottom, the rest is parked
  (never hidden, so the engine keeps populating and enabling it); the chip row sits above the tiles
  with the portrait container at its left and the cameos under the name; the queue row above the
  chip while producing; the small context panels take the tile rows; the powers on show are packed
  into their rows under the minimap. A different set of commands opens on its first tab. The slide
  is `x += (1 − t)³ × (displayW − drawerLeft)` over 160 ms from the moment the drawer opens. The
  radar window is hidden unless `rts::localPlayerHasRadar()`, the glow unless the engine is flashing
  it (`m_radarAttackGlowOn`).
- **Root draw** (`ControlBar::phoneDrawBackdrop`, the root's draw function, under the bar's
  windows): the round ☰ button; the resource pill with its coin, the money with thousands
  separators, the ⚡ and the power bar (green while production covers consumption, with a mark for
  the share in use; red in deficit); the left column (IDLE with the idle worker's own cameo — from
  `InGameUI::androidIdleWorkers` — and count, ARMY, the "+" ghost, the groups with their count
  badges); the minimap frame; chat and beacon beds; the drawer's bed with the chip's name, health
  bar and count badge, and the tabs (active in gold, pressed bright).
- **Tile draw** (`ControlBar::phoneDrawTile`): rounded bed, the cameo in the top 4:3 box (dimmed in
  colour while locked — `0xff5c5c5c`, or `0xffb4b4b4` for `ALWAYS_COLOR` — the engine's veterancy
  overlay and recharge/production clock over it), the price in a pill on the cameo
  (`getPhoneCommandCost`: `ThingTemplate` / `UpgradeTemplate::calcCostToBuild`, red when
  `Money::countMoney()` is short), a padlock while locked (disabled and not `WIN_STATUS_NOT_READY`),
  the name in up to two lines (`TheGameText->fetch(command->getTextLabel())` without the `&` hotkey
  marker, word-wrapped and clipped to the tile), and the outline: gold while available, muted while
  locked, bright and thicker while pressed, pulsing while the engine flashes the button.
- **Power draw** (`ControlBar::phoneDrawPowerButton`): round bed, the cameo inset to 70 %, and the
  ring: a dark track with a gold arc for the engine's clock percentage while recharging, a full
  pulsing ring while ready, a muted ring while unavailable; the engine's ready-count text as a badge.
- **Rank badge** (`ControlBar::phoneDrawRankBadge`, on `ButtonGeneral`): a star that pulses gold with
  the unspent points (`Player::getSciencePurchasePoints()`) in a badge; a tap opens the promotion
  screen as on the PC.
- `setScaledViewportHeight()` is the full height on Android, and `ShowControlBar` /
  `HideControlBar` / `animateSpecialPowerShortcut` are immediate: the slide animations capture a
  window's position at registration and would fight the layout pass. The superweapon and named
  countdowns (`InGameUI`) take their position from the layout and a physical font size.

Nothing allocates per frame or per event: `DisplayString`s for the money, the chip, the badges, the
tab captions and each tile's name and price are created once (`ControlBar::phoneReleaseText()` frees
them on reset) and re-set only when their value changes; the per-frame work is a few dozen flag
reads.

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
Android: it is laid out for the 4:3 bar and anchors to markers the phone layout parks; the tile's
own name replaces it. Each placement step writes one `ANDROID placement:` line to the log for the
next device test.

## Overlay buttons

The ☰ menu, the left column and the drawer tabs are not `GameWindow`s: they are drawn by the root
draw and hit-tested in the touch layer, both from `ControlBar::phoneOverlays()`, so a button is
tappable exactly where and when it is drawn. A finger arms the button it lands on; release over it
fires; a group button fires its long-press action instead after 350 ms (haptic tick). The "+" slot
has only the long-press action. Actions go through the message stream the PC hotkeys use:
`MSG_META_SELECT_TEAMn` (the engine centres the camera on a quick second select),
`MSG_META_CREATE_TEAMn`, `MSG_META_SELECT_ALL`, `MSG_META_SELECT_NEXT_IDLE_WORKER`; a tab calls
`ControlBar::phoneSelectTab()` (the active tab again turns its page). Group presence and counts
come from `Player::getHotkeySquad()->getLiveObjects()`, IDLE from `InGameUI`'s idle list. The
pressed look is `TheAndroidOverlayPressed`, set by the touch layer and read by the draw.

## Verified without a device

A host-side simulation (`clang++`, no NDK) compiles the touch block against stubs and drives it
with a fake clock: the geometry on three phones (the owner's 3200 × 1440 at 20.7 px/mm, a 1080p
20:9 and a 1080p 19.5:9) with 0, 5 and 8 powers (nothing overlaps, every target is ≥ 7 mm, a full
drawer clears the powers, five tiles and nine queue thumbnails fit the drawer, the tabs are thumb
targets); column packing ([IDLE][ARMY][+1][2]); tap/hold on groups and on the "+" slot; slide-off;
menu; the tabs (hidden when empty, packed right, a tap switches, a second tap on the active tab
turns its page, nothing reaches the engine's mouse); the pill, minimap-block and drawer dead zones
(swallowed; the same point is the battlefield once the drawer closes; the top centre is
battlefield); the HUD hidden (no overlays); and the placement tap. The engine-side layout and draw
code mirrors the engine's own button drawing and is compiled by CI.

## What is not in this step

Placement flow with ✓/✗/rotate, 3D model thumbnails and sharper cameo upscaling, the command
wheel, a collapsible minimap, toasts and the left-handed option — see spec §6.8 / Phase C-2.
