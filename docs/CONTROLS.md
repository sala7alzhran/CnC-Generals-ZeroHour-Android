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

**The command bar folds away by itself.** With nothing selected, the game's command bar folds down
to its thin top rail (money, power and the up arrow), so the battlefield gets the whole screen.
Select a unit or building and the full bar comes back; deselect and it folds again. Tapping the
bar's up/down arrow overrides this until the selection changes. The battlefield is always drawn
full height behind the bar, so the view never jumps when the bar folds or unfolds.

A battle starts about ⅓ zoomed in from the widest view, so units are readable; pinch out to see
more. The zoom range on phones is roughly 70–550 world units above the ground (the PC default on
a wide screen was about 190–590).

## Menus, HUD and building placement

**Quick Battle.** The main menu has a **QUICK BATTLE** button (bottom-right). It opens one phone
screen of big cards — General, Map, Opponents, Difficulty, Starting cash — each with ◀ ▶ arrows,
and **START BATTLE**. It remembers your last choice (shared with the Skirmish screen) and starts an
ordinary skirmish; the full Skirmish screen is still there for everything else.

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
