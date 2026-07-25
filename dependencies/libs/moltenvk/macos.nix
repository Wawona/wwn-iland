{ pkgs, ... }:

# macOS is unrestricted third-party distribution. Keep nixpkgs' full native
# MoltenVK package, including its dylib and ICD manifest, behind the L1 registry.
pkgs.moltenvk
