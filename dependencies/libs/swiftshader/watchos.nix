{
  lib,
  pkgs,
  stdenv,
  buildModule ? null,
  xcodeUtils,
  simulator ? true,
  ...
}@args:

# SwiftShader CPU Vulkan ICD for watchOS (Simulator + device store builds behind
# WWN_WATCH_SWIFTSHADER). No Metal.framework. Installs a static archive for
# in-process link (not an iOS-Simulator-style Frameworks dylib).
#
# App Store 2.5.2: Reactor still uses LLVM JIT on watch (Subzero has no watchOS
# target). Local proof (watchos-sim, 2026-08-27): libvk_swiftshader.a ~35MB with
# _vkGetInstanceProcAddr; strings still show LLVMJIT. Treat default-on store
# ship as blocked until an interpreter/AOT path lands (Mesa softpipe candidate).
# This recipe remains the development ICD behind WWN_WATCH_SWIFTSHADER.
import ./macos.nix (
  (removeAttrs args [ "simulator" ])
  // {
    appleSdk = if simulator then "watchsimulator" else "watchos";
    minVersionFlag =
      if simulator then
        "-mwatchos-simulator-version-min=10.0"
      else
        "-mwatchos-version-min=10.0";
    extraCmakeFlags = [
      "-DCMAKE_SYSTEM_NAME=watchOS"
      "-DCMAKE_OSX_ARCHITECTURES=arm64"
      "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.0"
      "-DSWIFTSHADER_BUILD_TESTS=OFF"
      "-DREACTOR_BACKEND=LLVM"
      "-DVK_USE_PLATFORM_METAL_EXT=0"
      "-DVK_USE_PLATFORM_MACOS_MVK=0"
      "-DBUILD_SHARED_LIBS=OFF"
    ];
    watchOsBuild = true;
    installStaticIcd = true;
  }
)
