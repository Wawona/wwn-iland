{
  lib,
  pkgs,
  stdenv,
  xcodeUtils,
  simulator ? true,
  ...
}@args:

# SwiftShader CPU Vulkan ICD for the iOS *Simulator* / CI only. On-device iOS
# stays MoltenVK-only (App Store posture); a software Vulkan device is neither
# needed nor bundled there. The Simulator (and headless CI) may lack a
# Metal-backed Vulkan device MoltenVK can use, so this is the last-resort link
# in the Vulkan fallback chain there.
#
# Reuses macos.nix with the iOS Simulator SDK. CMAKE_SYSTEM_NAME=iOS drives
# CMake's Apple cross-compile; the Simulator target is arm64 on Apple-silicon
# hosts.
import ./macos.nix (
  (removeAttrs args [ "simulator" ])
  // {
    appleSdk = if simulator then "iphonesimulator" else "iphoneos";
    minVersionFlag =
      if simulator then
        "-mios-simulator-version-min=15.0"
      else
        "-miphoneos-version-min=15.0";
    extraCmakeFlags = [
      "-DCMAKE_SYSTEM_NAME=iOS"
      "-DCMAKE_OSX_ARCHITECTURES=arm64"
      "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0"
    ];
  }
)
