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

## HUD
- **Added** an auto-folding command bar: with nothing selected, the bar drops to the game's own
  "low" stage (the thin top rail), and selecting anything brings the full bar back. The bar's
  up/down arrow overrides this until the selection changes. On Android the world is always drawn
  full height (`m_viewportHeightScale = 1`), so folding never resizes the battlefield view.

## Menus
- **Added** smart touch in menus: a tap that lands on no control moves to the nearest enabled
  control within 4 mm, and a tap on a dropdown's text opens its list. A modal dialog limits the
  search to itself. Never in a battle unless the pause menu is up.
- **Added** finger-sized rows in dropdown lists (at least 7 % of the screen height, about 5 mm on a
  phone; the text is centred). A list that would run off the bottom shows fewer rows and scrolls.
- **Fixed** the first tap on the main menu after launch only waking the menu ("Solo Play" needed
  two taps): on Android the menu is woken right away, as the PC does on the first mouse movement.
- **Fixed** the Skirmish colour column reading `???` (now "Random") and a label missing from the
  player's CSF reading `MISSING: 'GUI:CustomMission'` (now "Custom Mission").
- **Fixed** the log spam: `D3DRS_PATCHSEGMENTS`, which DXVK's D3D8 layer doesn't implement and
  warned about on every shader change (~13 KB/s into `ccg_log.txt`), is no longer set on Android.

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
- **Changed** the import to shrink the installed data by about 450 MB (2.44 → 1.99 GB on a
  Steam copy): uncompressed 16-bit speech and sound effects inside the `.big` archives are
  re-encoded to IMA ADPCM, the 4:1 format the retail game already uses for some of its own sounds
  and which FFmpeg decodes. Music, videos, textures and maps are copied untouched. Each archive is
  rewritten next to itself and swapped in only when complete; a `.compacted` list skips the work
  on a re-import. The two "sizzle" promo videos (39 MB), which only play after the intro movies
  that Android skips, are no longer copied.

## In progress
- LAN multiplayer between two Android devices (UDP discovery; `MulticastLock` held by the
  Activity; `udp.cpp`, `LanLobbyMenu.cpp`).
