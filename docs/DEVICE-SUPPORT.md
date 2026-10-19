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

- **Mali-G57**: the big one — the Mali shader compiler crashes on Direct3D user clip planes
  (`gl_ClipDistance`); the port disables them. See
  [fixes/mali-shader-cmpbe-crash.md](fixes/mali-shader-cmpbe-crash.md).
- **Adreno / PowerVR / Xclipse**: **untested.** They may not need the clip-plane workaround and
  may have their own quirks. Reports (and fixes) welcome — see [../CONTRIBUTING.md](../CONTRIBUTING.md).

## Reporting a device

Open an issue with: exact model, SoC/GPU, Android version, and the app's private log. Say what
worked and what didn't (boots? menu? in mission? audio? minimap?).
