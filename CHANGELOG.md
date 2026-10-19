# Changelog — Android port

All changes are relative to the [GeneralsX](https://github.com/fbraz3/GeneralsX) baseline
(`@0cdbc81`). Grouped by area; see [docs/](docs/) for detailed write‑ups.

## Platform / build
- Android (`arm64-v8a`) target: NDK 27 toolchain, CMake, Gradle app project.
- `SDL3Main.cpp` native entry point (replaces `WinMain`): SDL3 + DXVK WSI init, `chdir()` to the
  app's internal data dir, publish native display size, run `GameMain()`.
- CMake: `SAGE_USE_OPENAL` backend, SDL3/FFmpeg via vcpkg, DXVK `.so` integration
  (`cmake/sdl3.cmake`, per‑target `CMakeLists.txt`, `vcpkg.json`).
- **Fixed** the status/navigation bars staying painted over the game on Android 15/16:
  `ZerohourActivity` now owns immersive mode via `WindowInsetsController` and re‑applies it on
  resume, focus gain and whenever the insets show a bar (SDL's legacy flags + re‑hide callback no
  longer work with edge‑to‑edge enforcement).
- **Added** a committed debug signing key (`android/app/debug.keystore`) so every CI/local debug
  APK installs over the previous one instead of failing with `INSTALL_FAILED_UPDATE_INCOMPATIBLE`
  and forcing an uninstall + 2.6 GB re‑import.

## Rendering (DXVK → Vulkan on Mali‑G57)
- **Fixed** the Mali shader‑compiler crash in missions — root cause: D3D user clip planes
  (`gl_ClipDistance`). Disabling them unblocked gameplay and full model deployment.
- **Fixed** a DXVK heap‑corruption crash (gameplay blocker).
- **Fixed** the texture‑format caps: an unmapped DXVK backbuffer format made the engine disable
  **all** texture formats (blanked the radar). Fall back to `X8R8G8B8` for the caps query.
- **Fixed** magenta terrain: the terrain atlas fill path bypassed the BC/DXT decode hook.
- **Added** software BC/DXT decode where Mali lacks the legacy compressed formats.
- **Fixed** true fullscreen at native 1640×720 (was 4:3 pillar‑boxed); Options→Resolution no
  longer crashes on the empty Win32 mode table.
- Pipeline‑creation serialization as defense against compiler races.

## Minimap / radar
- **Fixed** black radar texture on the 10‑bit swapchain: prefer 32‑bit `X8R8G8B8` over 24‑bit
  `R8G8B8` for the CPU‑filled radar terrain texture (`W3DRadar.cpp`).
- **Changed** minimap availability: on Android, campaign keeps the minimap always‑on (touch
  navigation); skirmish/multiplayer keep desktop radar‑gating (`GameUtility.cpp`).

## Input (touch)
- **Added** a full multi‑touch control scheme in `SDL3GameEngine.cpp` (tap/drag/pinch/2‑finger,
  min‑press + motion‑keepalive for synthesized clicks).
- **Added** the on‑screen **☰** in‑game menu (ESC) button (`W3DInGameUI.cpp`).
- **Added** the on‑screen **▶▶** cutscene Skip button (synthesizes `KEY_ESC`).
- **Added** a persisted touch sensitivity control (repurposes Options→Scroll Speed for pan+zoom).
- Disabled SDL's built‑in touch→mouse synthesis (set before `SDL_InitSubSystem`).
- **Added** phone camera gestures (MOBILE‑UX‑SPEC Phase A): 1‑finger grab pan that keeps the
  terrain under the finger at every zoom, fling with exponential decay, long‑press box‑select
  with edge pan, double‑tap same‑type select, pinch zoom anchored under the fingers with soft
  limits. Thresholds in mm from the display density (JNI), haptic tick on long‑press.
- **Changed** the camera range on Android to 70–550 world units above ground (absolute, no
  aspect‑ratio compensation) and battles start 33 % zoomed in; the alternate‑mouse option is
  pinned off so a tap always selects/orders and a 2‑finger tap always deselects.
- **Added** the smart phone HUD (MOBILE‑UX‑SPEC Phase C‑1): the battlefield fills the screen; the
  control bar's own windows are re‑laid‑out at runtime into a top strip (☰, money, power, ★,
  the general's powers row), a top‑right minimap and a right‑thumb command drawer of 4:3 tiles
  (≈ 10 mm) that slides in only while the selection has commands; a left‑thumb column adds the
  control groups that hold units (with counts), a "+" slot to assign, ALL and IDLE. All geometry
  and one palette from one header (`AndroidHud.h`), sized in mm from the display density.
- **Fixed** HUD taps falling through to the battlefield (a tap on a build tile ordered the dozer
  to drive there): the bar's artwork windows were still at their PC positions and, being no‑input,
  were hit before the tiles. Artwork is now parked off‑screen at zero size, panels and the root
  block the taps they do not use, and the strip and drawer are dead zones for the touch layer.
- **Changed** the HUD's look after the first phone test: nothing is shown without content (no
  empty column slots, no empty queue, no radar frame without a radar, no chat/beacon in single
  player), visible commands are packed into the first cells, every build tile shows its price
  (red when unaffordable) and a padlock when locked (dimmed in colour instead of greyscale), the
  touched tile's name appears beside the drawer, money sits on one line in a pill, the power meter
  has a trough.
- **Added** smart touch in menus (MOBILE‑UX‑SPEC Phase B‑1): a tap that lands on no control is
  moved to the nearest button / slider / list / dropdown within 4 mm, and a tap on a dropdown's
  text opens it through its arrow; the shift stays on the drag. Battles are unaffected unless the
  pause menu is up.
- **Fixed** the Skirmish colour column's `???` (now "Random"), menu labels missing from the retail
  CSF showing as `MISSING: '…'` (now a name made from the key, e.g. "Custom Mission"), and the
  per‑frame `D3DRS_PATCHSEGMENTS` DXVK warning that grew `ccg_log.txt` by ~13 KB/s (the state is
  not set on Android).

## Audio
- **Fixed** total silence: `SDL3Main.cpp` forced desktop OpenAL drivers on Android (`__linux__`
  branch) → the `null` device. Force the **OpenSL ES** backend on Android.

## Video / cutscenes
- FFmpeg Bink (`.bik`) playback; movies optional.
- First‑launch bootstrap to stage movies from external → internal storage (dev route).

## Packaging
- **Added** the self‑contained APK: game data bundled as split `.pak` assets + a `SetupActivity`
  first‑run extractor. Documents the SELinux constraints on writing internal storage.
- App icon + label ("Generals Zero Hour").

## In progress
- LAN multiplayer between two Android devices (UDP discovery; `MulticastLock` held by the
  Activity; `udp.cpp`, `LanLobbyMenu.cpp`).
