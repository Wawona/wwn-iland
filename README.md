# wwn-iland

Wawona's userland Linux-graphics compatibility layer: GBM / EGL / DRM-KMS over
Apple `IOSurface` + ANGLE, providing the "Mode A" in-window present path that
replaces the macOS WindowServer/SkyLight stack for Wayland/Weston GL clients
(kmscube, es2gears, weston-simple-egl) on Apple platforms.

Extracted from the Wawona monorepo. Built with [wwn-toolchain](https://github.com/Wawona/wwn-toolchain).

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
