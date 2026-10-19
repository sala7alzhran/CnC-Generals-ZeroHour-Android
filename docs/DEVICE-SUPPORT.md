# Device support

## Reference device (developed & tested on)

| | |
|---|---|
| Model | Redmi (`25028RN03A`, codename `serenity`) |
| SoC / GPU | **Mali-G57** class, Vulkan 1.1+ |
| ABI | `arm64-v8a` |
| Android | 14+ |
| Orientation | **landscape-locked** (a rotation flip breaks the DXVK swapchain) |
| Free space | ~5 GB (APK in `/data/app` + extracted data in `/data/data`) |

## Requirements

- 64-bit ARM (`arm64-v8a`). No 32-bit or x86 build.
- A Vulkan-capable GPU (DXVK targets Vulkan). No GL fallback.
- Enough RAM/VRAM for the base game textures; low-RAM devices may struggle.

## Graphics settings for slower phones

On the first run the game picks its render resolution and detail level from the phone's class,
the lower of two guesses (`W3DDisplay.cpp`, `androidGraphicsTier`):

| Class | GPU (from the Vulkan device name) | Memory | Render height | Detail |
|---|---|---|---|---|
| Low (0) | Adreno below 530 and 600–614, Mali-G31…G57/G71/G72, Mali-G310, Mali-T, PowerVR | 4 GB or less | up to 720 lines | Low |
| Mid (1) | Adreno 530–599, 615–629, 700–724, Mali-G68/G76/G77/G78, Mali-G610/G615, Xclipse < 900 | about 6 GB | up to 900 lines | Medium |
| High (2) | Adreno 630–699, 725+, Mali-G710+, Immortalis, Xclipse 900+, anything unknown | more | native | High |

On a 2400×1080 screen that is 1600×720, 1800×810 or 2400×1080. The player can change both in
Options at any time; their choice is kept. To force a class (to test, or for a phone the table
gets wrong), put `0`, `1` or `2` in `device_tier.txt` in the app's files folder and remove the
`IdealStaticGameLOD`, `StaticGameLOD` and `Resolution` lines from
`files/GeneralsX/GeneralsZH/Options.ini`:

```bash
adb shell "run-as org.generalsx.zerohour sh -c 'echo 1 > files/device_tier.txt'"
```

The engine log (`ccg_log.txt`) shows the decision on lines starting with `[Graphics]`.

## Known GPU notes

- **Adreno 750** (POCO F7 Pro, Android 16): tested; it has every Vulkan feature DXVK uses, so none
  of the fallbacks below are active.
- **Mali and other GPUs missing DXVK features**: `scripts/ci/dxvk-android.patch` adds fallbacks.
  Each one takes effect only when the GPU lacks the feature or runs the ARM driver:

  | Missing on the phone | Fallback |
  |---|---|
  | `VK_EXT_robustness2` / `nullDescriptor` (Mali drivers before r53) | Unbound descriptor slots get zero-filled dummy images and buffers |
  | `robustBufferAccess2`, `multiDrawIndirect` | Not enabled; indirect multi-draws and the HUD draw one at a time |
  | `textureCompressionBC` (all Mali, PowerVR) | DXT formats are reported unsupported, so the engine decodes DXT textures on the CPU |
  | `shaderClipDistance` (all Mali) | D3D user clip planes are left out of the shaders; the game doesn't use them |
  | ARM proprietary driver | `OpKill` instead of demote-to-helper, and serialized pipeline creation (Mali compiler crashes) |

  The log line `D3D9DeviceEx: Android workarounds: ...` in `ccg_log.txt` lists what is active.
  These were tested on the Adreno 750 by faking the missing features (below), not yet on a real
  Mali phone. See also [fixes/mali-shader-cmpbe-crash.md](fixes/mali-shader-cmpbe-crash.md).
- **Not supported**: drivers that only offer Vulkan 1.1 (DXVK 2.6 needs 1.3). That covers older
  Mali drivers (r34 and before), Adreno 610/618 and early Adreno 619 drivers, and PowerVR GE8320,
  which also has no geometry shaders. A system update often brings a newer driver.

### Testing the Mali fallbacks on a phone that has everything

DXVK reads `dxvk.conf` from the app's files folder. To act like an older Mali driver:

```bash
adb shell "run-as org.generalsx.zerohour sh -c 'printf \"dxvk.fakeMissingFeatures = robustness2,mdi,clipdistance,bc,outputlayer\ndxvk.fakeArmDriver = True\n\" > files/dxvk.conf'"
```

For a Mali r53/r54 driver use `rba2,mdi,clipdistance,bc,outputlayer`. Delete `files/dxvk.conf` to
go back. The log then shows `test overrides active` and the workaround line.

## Reporting a device

Open an issue with: exact model, SoC/GPU, Android version, and the app's private log. Say what
worked and what didn't (boots? menu? in mission? audio? minimap?).
