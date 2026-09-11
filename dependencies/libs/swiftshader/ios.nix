{
  lib,
  pkgs,
  stdenv,
  xcodeUtils,
  iosToolchain,
  simulator ? true,
  ...
}@args:

# SwiftShader CPU Vulkan ICD for iOS/iPadOS devices and Simulator. It is the
# last-resort link in the Vulkan fallback chain after MoltenVK, and is bundled
# with every Wawona iOS deployment from the product floor through the newest SDK.
#
# Reuses macos.nix with the iPhoneOS or iPhoneSimulator SDK.
import ./macos.nix (
  (removeAttrs args [ "simulator" ])
  // {
    appleSdk = if simulator then "iphonesimulator" else "iphoneos";
    minVersionFlag =
      if simulator then
        "-mios-simulator-version-min=${iosToolchain.deploymentTarget}"
      else
        "-miphoneos-version-min=${iosToolchain.deploymentTarget}";
    extraCmakeFlags = [
      "-DCMAKE_SYSTEM_NAME=iOS"
      "-DCMAKE_OSX_ARCHITECTURES=arm64"
      "-DCMAKE_OSX_DEPLOYMENT_TARGET=${iosToolchain.deploymentTarget}"
    ];
  }
)
