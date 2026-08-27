# watchOS iland: CPU GLES/VK via ANGLE-on-Vulkan + wl_shm present (no Metal).
args: import ./ios.nix (args // { enableGl = true; })
