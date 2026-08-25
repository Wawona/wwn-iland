# tvOS Mode A userland. IOSurface + Metal present, Wayland-Vulkan WSI
# (MoltenVK), and ANGLE GLES (source GN, same ios.nix as visionOS).
# watchOS stays the empty stub (no Metal in the SDK).
# Cited: Wawona/docs/wwn-repo-dag.md (angle is L1).
args: import ./ios.nix args
