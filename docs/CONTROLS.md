# Touch controls reference

The port ships a custom multi-touch scheme (there is no mouse). See
[fixes/touch-controls.md](fixes/touch-controls.md) for the implementation and
[MOBILE-UX-SPEC.md](MOBILE-UX-SPEC.md) for the design it is converging on.

## In a battle (finger on the battlefield)

| Gesture | Action |
|---|---|
| **Drag** (one finger) | Pan the camera. The ground under your finger follows it 1:1 at every zoom; let go while moving and the view glides on and slows down. Touching the screen stops the glide. |
| **Tap** | Select the unit / building under the finger. With units selected, tapping the ground or an enemy gives the order (move / attack / enter…). |
| **Double-tap** a unit | Select every unit of that type on screen. |
| **Hold still (≈ ⅓ s, short vibration), then drag** | Rubber-band box-select. Dragging near a screen edge pans the camera while the box is open. |
| **Pinch** (two fingers) | Zoom. The ground under the midpoint of your fingers stays put; the zoom eases into its limits and springs back if you push past them. |
| **Two-finger drag** | Pan (the older, slider-scaled pan; kept for now so it can be compared with the one-finger pan). |
| **Two-finger tap** | Deselect everything (or cancel a building placement). |
| **☰ button** (top-left) | Open the in-game / pause menu (acts as the **ESC** key). |
| **▶▶ button** (top-right, during a cutscene) | Skip the cutscene. |

A battle starts about ⅓ zoomed in from the widest view, so units are readable; pinch out to see
more. The zoom range on phones is roughly 70–550 world units above the ground (the PC default on
a wide screen was about 190–590).

## The HUD in a battle

The PC command bar is gone; the battlefield fills the screen and the controls sit in the thumb
zones (design: [MOBILE-UX-SPEC.md §6](MOBILE-UX-SPEC.md), implementation:
[fixes/phone-hud.md](fixes/phone-hud.md)).

| Where | What |
|---|---|
| **Top-left** | The round **☰** pause / in-game menu button (ESC), then one **resource pill**: the `$` coin and your **money**, the ⚡ and the **power bar** (green while your plants cover what you use, with a mark for the share in use; red in deficit). |
| **Top-right** | The **minimap** in its gold frame, only once you have a radar (command centre, radar van…). Tap it to jump the camera there; with units selected, tap to send them (attack-move if that mode is on). It glows when your base is under attack. The **★ rank badge** beside it opens the promotion screen; it pulses gold with the number of points you can spend. Under the map, your **general's powers** as round buttons once you have any: a gold **ring** fills while a power recharges and glows when it is ready; a ready-count badge when more than one is ready. **Superweapon countdowns** appear under them. |
| **Left column** | Round buttons, only what has content, stacked from the top: **IDLE** shows your idle worker's own picture with how many idle — tap to select the next one. **ARMY** selects every unit. While something is selected, a **"+" slot** offers the first free group: hold it to assign (a tap does nothing). Each **control group that holds units** (its number, and how many units in the badge): tap = select, tap again quickly = also centre the camera, **hold ≈ ⅓ s** (short vibration) = replace it with the current selection. |
| **Right drawer** | Slides in from the right edge while something you selected has commands. A **chip row** shows the portrait, name and health of the selection (a count badge for several units); under it up to **two rows of five tiles** in reading order, each its picture over its **name**, with the **price** on the picture (red when you cannot pay it yet); a **padlock** marks what you cannot build yet (the picture stays in colour, just darker); a gold outline means available. A menu with more than ten commands gets **tabs** at the right of the chip row: **BASE / DEF / SUP** for a builder (power, production and supply; defences; everything else), **UNITS / TECH / MORE** for a factory or any other big menu (tap the active tab again to turn its page). Build (dozer / worker), train and upgrade (factory), abilities, sell, repair, rally point, stop/evacuate for garrisons all work from here. While something is being built the **production queue** appears as a row of thumbnails above the chip (tap an item to cancel; the first shows its progress). |

Taps on the pill, the minimap block or the drawer that miss a control do nothing (they never hit
a unit behind them); taps anywhere else on the battlefield work as in the gesture table above.

**Placing a building:** tap a structure tile, then **tap the ground** where it should go. The
translucent ghost moves to the finger and the dozer is sent; if the spot is not allowed you hear
the dozer's "cannot build" line and the ghost turns red — tap elsewhere. While the ghost is up you
can still drag to pan and pinch to zoom; a **two-finger tap** cancels the placement.

## Menus, HUD and building placement

**Smart touch.** The PC menus have small rows and arrows. In every menu (and in the pause menu),
a tap that misses a control by up to 4 mm presses the nearest one, and a tap anywhere on a
dropdown row opens it. You can tap at the text, not only at the little arrow.

Wherever the finger is on a window rather than on the battlefield (menus, the command bar, the
minimap, while placing a building, in cutscenes) a finger behaves like a mouse:

| Gesture | Action |
|---|---|
| **Tap** | Press the button / select |
| **Drag** | Left drag (move a slider, drag on the minimap, move a building ghost) |
| **Two-finger tap** | Right click |

## Sensitivity

**Options → Scroll Speed** is repurposed on Android as a camera-speed multiplier. It scales the
strength of a fling, the speed of the edge pan while box-selecting, and the older two-finger pan.
The one-finger pan and the pinch are direct manipulation and do not use it. It persists across
launches (saved with the rest of the options). Higher = faster camera.

## Minimap

- **Campaign missions:** the minimap is always available (you navigate the map by touching it).
- **Skirmish / multiplayer:** the minimap appears only once you have radar (build the structure
  that provides it), exactly as on desktop.

See [fixes/minimap-radar.md](fixes/minimap-radar.md).
