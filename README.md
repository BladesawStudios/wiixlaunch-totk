# WiiXLaunch-TotK

The *Tears of the Kingdom* game module for
[WiiXLaunch](https://github.com/TKVSC-Team/WiiXLaunch): the game-specific half
of a TotK host, registering named, versioned export surfaces that compiled
`.wxlm` mods import. Base WiiXLaunch stays game-agnostic; everything that knows
it is running TotK lives here.

Switch only. There is no Wii U Tears of the Kingdom, and `module.hpp` refuses
to compile for any other platform.

## Using it

In the WiiXLaunch tree, as `vendor/wiixlaunch-totk`, and named by the target:

```json
// targets/totk.json
"modules": ["wiixlaunch-totk"]
```

```bash
build_switch.bat totk
```

Every build script asks `scripts/target_modules.py` which module to compile,
so having `wiixlaunch-botw` in `vendor/` alongside it is fine; a target that
names neither, with both present, is a build error rather than a coin flip.

## What's here

| Header | Provides |
| --- | --- |
| `module.hpp` | The host contract: `kName = "totk"` and `Register()` |
| `totk/build.hpp` | The per-build address table, keyed by the host's build fingerprint. One row (1.2.1), no fields yet |
| `totk/graphics/present.hpp` | The frame source: `nvnBootstrapLoader` -> `nvnDeviceGetProcAddress` -> `nvnQueuePresentTexture`, which runs `Tick::RunAll` once per presented frame |
| `totk/surfaces/frame_surface.hpp` | `totk.frame` v1.0: `FrameCount`, `IsLive` |
| `totk/game/input.hpp` | Pad reads and input capture, from hooks on the sdk's five `nn::hid::GetNpadStates` overloads (by symbol, no addresses) |
| `totk/surfaces/input_surface.hpp` | `totk.input` v1.0 |

### Surfaces

| Surface | Version | Symbols |
| --- | --- | --- |
| `totk.frame` | 1.0 | `FrameCount() -> u32`, `IsLive() -> u32` |
| `totk.input` | 1.0 | `IsLive`, `HeldButtons`, `MaskFor`, `IsPressed`, `GetLeftStick`, `GetRightStick`, `HoldInputCapture`, `SetInputCapture`, `IsInputCaptured` - the read calls match `botw.input`'s names, signatures and button indices; injection is planned for 1.1 |

## Builds

Addresses into `main` are true of one executable. `build.hpp` keys them by the
fingerprint the host computes at boot (see `targets/totk.json`, `identity`),
so a build with no row gets its address-based features refused by name rather
than hooked at the wrong place.

The frame source needs no address on any build: `nvnBootstrapLoader` is an
sdk export, which `main` only imports. It works on every enrolled build, and
the table is empty until the first surface that needs an address in `main`.

| Build | Fingerprint | Row |
| --- | --- | --- |
| 1.2.1 | `0x181D91D8` | yes, no fields yet |
| 1.0.0 - 1.4.3 | see `targets/totk.json` | no |

## Status

* Builds into the TotK Switch host; `nvnBootstrapLoader` links as an rtld
  import exactly like `nn::fs::MountRom`.
* First boot (1.2.1, Ryujinx, UltraCam on, 2026-10-10): the bootstrap hook
  took and saw `nvnDeviceGetProcAddress`, but present was never asked of it -
  UltraCam is in the same chain and answers for present itself. Present is
  now an inline hook on the sdk's own function body instead of a wrapped
  pointer. Expected, in order: `nvnBootstrapLoader hooked`,
  `nvnQueuePresentTexture hooked in the sdk`, `first frame presented`, then
  `examples/tick_test`'s `first tick` / `tick 60` / `tick 600` lines.
* The tick runs on the render thread. A game-thread source is planned and
  needs base to accept more than one nominated source.

## License

GPLv3, matching WiiXLaunch - see `LICENSE`.
