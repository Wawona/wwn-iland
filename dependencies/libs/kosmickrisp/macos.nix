{ pkgs, ... }:

# Mesa's native Darwin expression builds KosmicKrisp. Keep this L1 wrapper
# Vulkan-only so Wawona does not accidentally acquire Mesa GL/X11 renderers.
(pkgs.mesa.override {
  eglPlatforms = [ "macos" ];
  galliumDrivers = [ ];
  vulkanDrivers = [ "kosmickrisp" ];
  vulkanLayers = [ ];
}).overrideAttrs (old: {
  mesonFlags = (old.mesonFlags or [ ]) ++ [
    "-Dglx=disabled"
    "-Dopengl=false"
    "-Degl=disabled"
    "-Dgles1=disabled"
    "-Dgles2=disabled"
  ];
  # nixpkgs' generic Darwin Mesa fixup targets libGL.dylib, intentionally
  # absent from this Vulkan-only output.
  postFixup = "";
})
