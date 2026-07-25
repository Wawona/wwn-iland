{
  lib,
  pkgs,
  common ? null,
  buildModule ? null,
  ...
}:

# Native ANGLE GLES/EGL over Metal. L1 owns the graphics registry key while
# nixpkgs supplies the pinned implementation derivation.
pkgs.angle
