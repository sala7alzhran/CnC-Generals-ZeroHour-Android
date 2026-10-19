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
| **Top-left ☰** | Pause / in-game menu (ESC). |
| **Top strip** | Money, the power bar, the **★ generals star** (opens the promotion screen; it flashes when you have points to spend), and your **general's powers** as a row of tiles once you have any — each shows its recharge clock. In multiplayer: chat and beacon buttons. |
| **Top-right** | The **minimap**. Tap it to jump the camera there; with units selected, tap to send them (attack-move if that mode is on). It glows when your base is under attack. |
| **Left column** | **1 2 3 4** control groups: tap = select the group, tap again quickly = also centre the camera on it, **hold ≈ ⅓ s** (short vibration) = assign the current selection to that group. Empty groups are dimmed. **ALL** = select every unit. **IDLE** = select the next idle worker/dozer (dimmed when nobody is idle). |
| **Right drawer** | Appears only while something you selected has commands, and grows upward from the corner with the number of commands. Big tiles: build (dozer), train and upgrade (factory), abilities, sell, repair, rally point, stop/evacuate for garrisons. The first commands are in the bottom row, under your thumb. Above the tiles: the unit portrait with its upgrade/veterancy icons, or the **production queue** while something is being built (tap an item to cancel it; the first shows its progress). |

Taps on the drawer's empty parts do nothing (they never hit a unit behind it); taps anywhere
else on the battlefield work as in the gesture table above.

## Menus, HUD and building placement

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
