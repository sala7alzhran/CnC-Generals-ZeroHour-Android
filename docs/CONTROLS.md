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
| **Top strip** | Left to right: **☰** pause / in-game menu (ESC); your **money**; the **power meter** (empty trough until something produces power); the **★ generals star** (opens the promotion screen; it flashes when you have points to spend); your **general's powers** as a row of tiles once you have any — each shows its recharge clock. In multiplayer only: chat and beacon buttons before the minimap. |
| **Top-right** | The **minimap**, only once you have a radar (command centre, radar van…). Tap it to jump the camera there; with units selected, tap to send them (attack-move if that mode is on). It glows when your base is under attack. |
| **Left column** | Only what has content, stacked from the top: each **control group that holds units** (its number, and how many units in the small badge): tap = select, tap again quickly = also centre the camera, **hold ≈ ⅓ s** (short vibration) = replace it with the current selection. While something is selected, a **"+" slot** offers the first free group: hold it to assign (a tap does nothing). **ALL** = select every unit. **IDLE** appears only while a worker/dozer is idle: select the next one. |
| **Right drawer** | Slides in from the right edge while something you selected has commands, and is exactly as tall as those commands need: 3 columns of tiles, the first command bottom-left under your thumb, no gaps. Build (dozer), train and upgrade (factory), abilities, sell, repair, rally point, stop/evacuate for garrisons. Each build tile shows its **price** (red when you cannot pay it yet); a **padlock** marks what you cannot build yet (it stays in colour, just darker). The **name** of the tile you touched appears beside the drawer. Above the tiles: the **portrait** with its upgrade/veterancy icons, and — only while something is being built — the **production queue** beside it (tap an item to cancel; the first shows its progress). |

Taps on the strip or the drawer that miss a control do nothing (they never hit a unit behind
them); taps anywhere else on the battlefield work as in the gesture table above.

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
