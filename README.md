# wwn-iland

Wawona's userland Linux-graphics compatibility layer: GBM / EGL / DRM-KMS over
Apple `IOSurface` + ANGLE, providing the "Mode A" in-window present path that
replaces the macOS WindowServer/SkyLight stack for Wayland/Weston GL clients
(kmscube, es2gears, weston-simple-egl) on Apple platforms.

Extracted from the Wawona monorepo. Built with [wwn-toolchain](https://github.com/Wawona/wwn-toolchain).

## Credit / upstream

`wwn-iland` was inspired by and originally forked from
[**corebedtime/iland**](https://github.com/CoreBedtime/iland) — the project that
pioneered replacing the macOS WindowServer/SkyLight stack with a custom
`IOSurface`/Metal-backed framebuffer that mimics Linux DRM/KMS/EGL/GBM. All
credit for the original macOS approach goes to bedtime
([corebedtime/iland](https://github.com/CoreBedtime/iland)).

This repo is the new home of what used to live at `Wawona/iland`. Beyond the
upstream macOS-only layer, `wwn-iland` adds and implements substantial changes:

- **KMS/DRM on iOS** and across the rest of the Apple platform family
  (iPadOS/tvOS/visionOS), not just macOS.
- **Android** support.
- Cross-compiled, Nix-wrapped recipes (via `wwn-toolchain`) and a composable
  `registryFragment` consumed by Wawona and `wwn-weston`.
- The DRM/EGL/GBM/udev shim set under `dependencies/libs/iland/upstream/shims/`
  that `wwn-weston` links against for the nested-compositor present path.

## Use

```nix
inputs.wwn-iland.url = "github:Wawona/wwn-iland";

# Wawona merges the fragment and threads the source for shim copying:
registry = wwn-toolchain.lib.baseRegistry // wwn-iland.registryFragment;
extraArgs = { ilandSrc = wwn-iland; };  # weston copies upstream/shims/* from here
```

- `registryFragment.iland` - per-platform iland userland recipes (iOS family + macOS).
- `registryFragment."iland-gl-clients"` - GL smoke clients (kmscube).
- `dependencies/libs/iland/upstream/` - vendored upstream iland sources + DRM/EGL/GBM/udev shims that `wwn-weston` consumes via `ilandSrc`.

## Standalone build

```sh
nix build .#iland-macos       # macOS userland archive
nix build .#iland-ios         # iOS cross archive
```

## License

The Wawona Nix packaging in this repo is MIT (see `LICENSE`). The vendored
upstream `iland` sources under `dependencies/libs/iland/upstream/` retain their
original upstream license terms.
