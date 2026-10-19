# Adreno notes: Mali workarounds and what to check first

The port was only validated on a Mali-G57. Before testing on Adreno (first target: POCO F7 Pro,
Snapdragon 8 Gen 3 / Adreno 750, Android 16), here is every Mali-related workaround and where it
lives. Nothing below has been changed for Adreno yet; this is the checklist for the first
on-device run.

## 1. Fixes the docs describe that are *not* in this repository

These lived in the original author's private DXVK build. The CI APK uses fbraz3's GeneralsX DXVK
2.6 fork plus `scripts/ci/dxvk-android.patch`, so none of them are in it.

| Mali fix (docs) | Effect of it missing on Adreno 750 |
|---|---|
| Software BC/DXT decode ([terrain-textures-bc-decode.md](fixes/terrain-textures-bc-decode.md)) | Stock DXVK *requires* `textureCompressionBC`. Adreno is expected to report it and decode BC in hardware. If the driver doesn't report it, DXVK refuses to create the device and the game exits at startup. |
| Disabled D3D user clip planes / `gl_ClipDistance` ([mali-shader-cmpbe-crash.md](fixes/mali-shader-cmpbe-crash.md)) | Missing is the *right* state for Adreno: clip planes stay on, so water reflections are correct. Don't port this fix to Adreno unless its shader compiler crashes the same way. |
| Dummy descriptors where `nullDescriptor` is missing | DXVK 2.6 requires `VK_EXT_robustness2` `nullDescriptor`. Adreno 7xx should have it. The fork's dummy vertex-buffer change (GeneralsX Patch 12) is in, which also helps. |
| Serialized pipeline creation | Not present. It was defence-in-depth against a Mali compiler race. Expect no effect on Adreno, but it's the first suspect if a crash shows up inside the driver's compiler thread. |
| Tolerate persistent `VK_SUBOPTIMAL_KHR` | **Re-added** in `dxvk-android.patch`, guarded by `__ANDROID__`. It's an Android-wide issue, not a Mali one: a landscape app on a portrait-native panel gets SUBOPTIMAL every frame. |

DXVK 2.6 also hard-requires `geometryShader`, `shaderClipDistance` and Vulkan 1.3. Mali-G57 lacks
geometry shaders, so the private build must have relaxed these checks. Adreno 750 is expected to
pass all of them. If one is missing, DXVK prints `DxvkAdapter: ... not supported` in `ccg_log.txt`.

## 2. Workarounds that are in this repository

| Where | What | Adreno risk |
|---|---|---|
| `WW3D2/dx8caps.cpp` | Backbuffer format that maps to no legacy format → treated as `X8R8G8B8` (all non-Windows) | Low. Adreno usually gets an 8-bit swapchain, which the original code already handled. |
| `W3DRadar.cpp` | Radar texture prefers 32-bit `X8R8G8B8` over 24-bit `R8G8B8` (non-Windows) | Low. The fix is format-correct on any GPU. |
| `missingtexture.cpp` | Missing-texture fallback is white, not magenta | None. Not GPU-specific. |
| Water/terrain/tree null guards, 16-bit sorting-buffer flush | Defensive guards | None. |
| `engine/resources/dxvk/dxvk.conf` | Only `d3d9.forceSamplerTypeSpecConstants = False` | The docs mention a "mobile-tuned" dxvk.conf; this one has no Mali-specific settings. |

## 3. Not Mali-specific, but different on this phone

- **Resolution:** `SDL3Main.cpp` renders at the panel's native size (2400×1080 vs 1640×720 on the
  Mali phone, about 2.2× the pixels). This is the main lever for the planned render-scale setting.
- **120 Hz:** the present mode comes from DXVK's FIFO default. The engine's own frame pacer sets
  the frame rate; check whether it renders 30, 60 or 120 frames/s.

## What to collect on the first run

The debug APK is debuggable, so the engine log can be read without root:

```bash
adb logcat -c
adb shell monkey -p org.generalsx.zerohour -c android.intent.category.LAUNCHER 1
# ...reproduce, then:
adb logcat -d > logcat.txt
adb shell run-as org.generalsx.zerohour cat /data/data/org.generalsx.zerohour/ccg_log.txt > ccg_log.txt
```

`ccg_log.txt` holds the engine's and DXVK's stderr (`DxvkAdapter`, `Presenter`, `SDL3 WSI` lines).
A native crash shows up in `logcat.txt` as a `DEBUG` tombstone. The CI run also uploads an
unstripped `libmain.so` (artifact `libmain-symbols`) for `ndk-stack`.
