# tvOS Mode A userland. Vulkan first: IOSurface + Metal present and
# Wayland-Vulkan WSI. No ANGLE until Phase 2 (Chromium GN tvOS target).
# watchOS stays the empty stub (no Metal in the SDK).
args: import ./ios.nix (args // { enableGl = false; })
