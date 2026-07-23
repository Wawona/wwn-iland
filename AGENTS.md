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
