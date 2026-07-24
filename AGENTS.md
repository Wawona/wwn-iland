# AGENTS.md — wwn-iland

Guidance for AI agents editing this repository.

## What this repo is

Wawona’s Linux-graphics compat layer (GBM / EGL / DRM-KMS over IOSurface +
ANGLE on Apple, Android stubs). Fork lineage:
[CoreBedtime/iland](https://github.com/CoreBedtime/iland) → deleted
`Wawona/iland` → **this repo**.

Query **wwn-mcp** (`project=iland`, source `wwn-iland` for packaging; source
`iland` for upstream CoreBedtime reference). Prefer `wwn-iland` recipes over
vanilla CoreBedtime behavior.

## Mode A vs Mode B (non-negotiable)

| Mode | Recipe | Output | Who ships it |
|------|--------|--------|----------------|
| **A** (default) | `registryFragment.iland` → `macos.nix` / `ios.nix` / `android.nix` / stubs | `libiland_userland.a` | All Wawona targets that need GL/userland DRM |
| **B** | `registryFragment.iland-baremetal` → `macos-baremetal.nix` only | `libwayland-mac.dylib` | **Only** Wawona `wawona-macos-desktop-host` |

- Mode B = Dobby + `DYLD_INSERT_LIBRARIES` + embedded `framebufferd` /
  `inputd` / `amfiexceptiond`. Needs SIP debugging off + root. **Not** App Store.
- Never add `iland-baremetal` variants for iOS/iPadOS/tvOS/watchOS/visionOS/Android.
- tvOS/watchOS Mode A builds are empty stubs (no ANGLE/IOKit). See workspace
  rule `wwn-iland-apple-fallback`.

Canonical prose (integration side):
`Wawona/docs/iland-mode-a-b-desktop.md` and workspace rule
`wawona-iland-mode-b-desktop`.

## Repo DAG layer (L1) — never invert

`wwn-iland` is **L1: the complete Wawona graphics stack** (iland userland
DRM/KMS/GBM/EGL present + Mode A/B; after P2 also `angle`, `swiftshader`,
`moltenvk`, `kosmickrisp`, Turnip hooks, `iland-cpu`). It depends on
**`wwn-toolchain` (L0) ONLY**.

- **Never** add `wwn-weston`, `wwn-kmscube`, `wwn-waypipe`, or `Wawona` as a
  flake input of this repo (that would make iland depend on its consumers).
- **Never** move substrate libs (`pixman`, `cairo`, `pango`, `libwayland`) into
  this repo — they stay L0; `iland-cpu` *links* toolchain `pixman`.
- Graphics keys (`angle`/`swiftshader`) move here from toolchain in P2; after
  the move, consumers merge this repo's `registryFragment`, not bare toolchain.

Canonical: `Wawona/docs/wwn-repo-dag.md` + workspace rule `wawona-repo-dag`.

## Layout

- `dependencies/libs/iland/upstream/` — vendored CoreBedtime tree + Wawona shims
  (`iland_present.h`, Mode A present redirect, ANGLE/zerocopy EGL patches).
- `dependencies/libs/iland/android/` — Android GBM/IOSurface-compat sources.
- `dependencies/generators/iland-gl-*.nix` — link flags for clients.
- `flake.nix` — `registryFragment` consumed by Wawona / wwn-weston / wwn-kmscube.

## Do / don’t

- **Do** keep Mode A App-Store-safe (no injection, no priv daemons in the
  default archive).
- **Do** build Mode B only via `macos-baremetal.nix` / `iland-baremetal-macos`.
- **Don’t** MacPorts-assume `/opt/local` in new code; Nix-substitute ANGLE paths.
- **Don’t** document Mode B as the default present path for Wawona apps.
