# True fullscreen at native resolution

## Symptom

The game rendered into a 4:3 pillar‑boxed area in the middle of the screen instead of filling
the phone's 1640×720 display, and the Options → Resolution list could crash.

## Root cause

The engine's resolution handling was written for a Win32 render device that enumerates a table
of display modes. On Android/DXVK there is no such table:

1. `buildFilteredResolutions()` called `WW3D::Get_Render_Device_Desc(0).Enumerate_Resolutions()`
   which dereferenced an empty table and **SIGSEGV'd** the moment the Options menu built its
   resolution list.
2. Nothing told the 3D backbuffer, the 2D UI and the mouse to use the real device size, so they
   defaulted to a 4:3 mode and got letter/pillar‑boxed.

## Fix (`W3DDisplay.cpp`)

- On Android, `buildFilteredResolutions()` returns just the **single native display resolution**
  (published by `SDL3Main.cpp`) instead of enumerating an empty Win32 table — no crash, and the
  Options list shows the correct mode.
- `W3DDisplay::init()` sets `m_xResolution`/`m_yResolution` to the real device size so the 3D
  backbuffer, the 2D UI and the mouse are all in agreement — the game fills the screen edge to
  edge at 1640×720.

Result: native‑resolution fullscreen, correct terrain/water/textures, and an Options menu that
doesn't crash.

## Follow‑up: side black bars from the display cutout (`styles.xml`)

Even after the engine renders at the native resolution, a phone with a **display cutout**
(punch‑hole / notch camera) can still show a **black bar on one side** in landscape. That bar is
not the engine's doing — it's Android reserving the cutout region because the app never opted into
drawing there. With `targetSdkVersion 35` (edge‑to‑edge enforced) this is especially visible.

Fix in the app theme:

```xml
<style name="AppTheme" parent="android:Theme.NoTitleBar.Fullscreen">
    <item name="android:windowLayoutInDisplayCutoutMode">shortEdges</item>
</style>
```

`shortEdges` lets the game extend into the cutout on the short edges (left/right in landscape),
which removes the side bar. It needs API 28+ (the app's `minSdk` is 28). This is pure app
configuration — no engine or SDL code is touched.


## Follow‑up: the status bar and navigation bar came back (`ZerohourActivity.java`)

### Symptom

On the owner's POCO F7 Pro (Android 16) the game sometimes ran with the system bars painted over
it: `dumpsys` showed the window at the full `[0,0][2400,1080]` but `InsetsSource statusBars
frame=[0,0][2400,121] visible=true` and `navigationBars frame=[2267,0][2400,1080] visible=true`,
so the top 121 px and right 133 px of the image were covered (a half‑cut SKIRMISH title, the ☰
button and the end of the command bar hidden). The same phone had been fully immersive in an
earlier session, so it depended on what happened on the way in (a folder picker in portrait, a
touch on an edge, the soft keyboard).

### Root cause

SDL's `SDLActivity` hides the bars with the legacy `View.SYSTEM_UI_FLAG_FULLSCREEN |
HIDE_NAVIGATION | IMMERSIVE_STICKY` flags and re‑hides them from a
`setOnSystemUiVisibilityChangeListener` callback. Both APIs are deprecated since API 30 and, with
edge‑to‑edge enforced for `targetSdk 35`, the system can show the bars and drop the two hide
flags (`vsysui` kept only `IMMERSIVE_STICKY|LAYOUT_HIDE_NAVIGATION|LAYOUT_FULLSCREEN`) without
the legacy callback ever firing, so nothing hid them again.

### Fix

`ZerohourActivity` owns immersive mode with the current API:

- `WindowInsetsController.hide(statusBars | navigationBars)` with
  `BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE` (a swipe shows the bars briefly, then they go away by
  themselves), `setDecorFitsSystemWindows(false)` and `LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS` so
  the surface stays edge to edge; the legacy flags remain for API 28–29.
- It is applied in `onCreate`, `onResume` and `onWindowFocusChanged(true)`, and an
  `OnApplyWindowInsetsListener` on the decor view re‑applies it 1.5 s after the insets report a
  bar visible (debounced, so a deliberate swipe is still seen). `SDLSurface` keeps its own insets
  listener for the safe area it reports to native; ours consumes nothing.
