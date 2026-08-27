# ANGLE for watchOS: GLES on Vulkan (SwiftShader), Metal off. Store-safe shape
# (static .a archives, no dlopen). Inverse of ios.nix Metal-on / Vulkan-off.
{
  lib,
  pkgs,
  buildPackages,
  common ? null,
  buildModule ? null,
  simulator ? false,
  iosToolchain ? null,
}:

let
  sdkPlatform = if simulator then "WatchSimulator" else "WatchOS";
  minFlag =
    if simulator then
      "-mwatchos-simulator-version-min=${iosToolchain.deploymentTarget or "10.0"}"
    else
      "-mwatchos-version-min=${iosToolchain.deploymentTarget or "10.0"}";
  packageSuffix = if simulator then "watchos-simulator" else "watchos";
  watchosPlatformPatch = ./patches/0002-chromium-build-watchos-target-platform.patch;
  watchosGnFix = ./patches/watchos-gn-fix.py;
in
import ./cross-base.nix {
  inherit lib pkgs buildPackages;
  pname = "angle-${packageSuffix}";
  clangBasePath = "xcode-clang";
  buildTargets = "angle_common libEGL_static libGLESv2_static";
  patchesExtra = [ watchosPlatformPatch ];
  postPatchExtra = ''
    python3 ${watchosGnFix}
    python3 - <<'PY'
from pathlib import Path
p = Path("src/common/apple_platform_utils.mm")
if not p.exists():
    raise SystemExit("missing apple_platform_utils.mm")
text = p.read_text()
text = text.replace(
    "#include <Metal/Metal.h>",
    """#if __has_include(<Metal/Metal.h>)
#include <Metal/Metal.h>
#define ANGLE_HAS_METAL_FRAMEWORK 1
#endif""",
)
# On watchOS (no Metal.framework), short-circuit availability.
if "ANGLE_HAS_METAL_FRAMEWORK" in text and "WWN_WATCH_NO_METAL_GATE" not in text:
    text = text.replace(
        "bool IsMetalRendererAvailable()\n{",
        """bool IsMetalRendererAvailable()
{
#if !defined(ANGLE_HAS_METAL_FRAMEWORK)
    // WWN_WATCH_NO_METAL_GATE: Watch SDK has no Metal.framework.
    return false;
#else
""",
        1,
    )
    # Close the #else before the final closing brace of the function.
    # Find the first function end after IsMetalRendererAvailable.
    start = text.find("bool IsMetalRendererAvailable()")
    brace = text.find("{", start)
    depth = 0
    end = None
    for i, ch in enumerate(text[brace:], brace):
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end is None:
        raise SystemExit("could not find end of IsMetalRendererAvailable")
    text = text[:end] + "#endif\n" + text[end:]
p.write_text(text)
# Vulkan-loader Linux-only path defines are missing on Apple/watch GN.
loader = Path("third_party/vulkan-loader/src/loader/loader.c")
if loader.exists():
    lt = loader.read_text()
    if "WWN_WATCH_VK_LOADER_DIRS" not in lt:
        loader.write_text(
            """/* WWN_WATCH_VK_LOADER_DIRS */
#ifndef FALLBACK_CONFIG_DIRS
#define FALLBACK_CONFIG_DIRS "/etc/xdg"
#endif
#ifndef FALLBACK_DATA_DIRS
#define FALLBACK_DATA_DIRS "/usr/local/share:/usr/share"
#endif
#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif
"""
            + lt
        )
PY
  '';
  gnExtraFlags = [
    "target_os=\"ios\""
    "target_cpu=\"arm64\""
    "target_environment=\"${if simulator then "simulator" else "device"}\""
    "target_platform=\"watchos\""
    # Chromium mobile_config.gni asserts this for Apple TV; same GN gate on watch.
    "use_blink=true"
    "angle_enable_metal=false"
    "angle_enable_vulkan=true"
    # Headless Vulkan display (pbuffers). Avoids DisplayVkMac / Metal WSI on Watch.
    "angle_use_vulkan_null_display=true"
    # Avoid Linux vulkan-loader (FALLBACK_CONFIG_DIRS / SYSCONFDIR). ICD is
    # static SwiftShader force-loaded into the watch binary.
    "angle_shared_libvulkan=false"
    "ios_deployment_target=\"${iosToolchain.deploymentTarget or "10.0"}\""
    "ios_enable_code_signing=false"
    "use_custom_libcxx=false"
    "use_thin_archives=false"
  ];
  preConfigureHook = ''
    unset DEVELOPER_DIR
    if [ -z "''${XCODE_APP:-}" ]; then
      if [ -d /Applications/Xcode.app/Contents/Developer ]; then
        XCODE_APP=/Applications/Xcode.app
      fi
    fi
    if [ -n "''${XCODE_APP:-}" ]; then
      export DEVELOPER_DIR="$XCODE_APP/Contents/Developer"
    else
      export DEVELOPER_DIR=$(/usr/bin/xcode-select -p)
    fi
    export SDKROOT="$DEVELOPER_DIR/Platforms/${sdkPlatform}.platform/Developer/SDKs/${sdkPlatform}.sdk"
    export PATH="$PATH:$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin:/usr/bin:/usr/sbin"
    LLVM_AR=$(command -v llvm-ar)
    [ -n "$LLVM_AR" ] || { echo "llvm-ar is required to build ANGLE" >&2; exit 1; }
    export CC="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
    export CXX="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
    export AR="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar"
    export CFLAGS="-arch arm64 -isysroot $SDKROOT ${minFlag}"
    export CXXFLAGS="$CFLAGS"
    export LDFLAGS="-arch arm64 -isysroot $SDKROOT ${minFlag}"
    rm -rf .angle-xcode-clang
    mkdir -p .angle-xcode-clang/bin
    ln -s "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang" \
      .angle-xcode-clang/bin/clang
    ln -s "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++" \
      .angle-xcode-clang/bin/clang++
    ln -s "$LLVM_AR" .angle-xcode-clang/bin/llvm-ar
    ln -s "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/lib" \
      .angle-xcode-clang/lib
    CLANG_VER=$(ls "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang" | sort -V | tail -1)
    CLANG_MAJOR="''${CLANG_VER%%.*}"
    if [ "$CLANG_VER" != "$CLANG_MAJOR" ] && [ ! -e "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/$CLANG_MAJOR" ]; then
      ln -sf "$CLANG_VER" \
        "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/$CLANG_MAJOR"
    fi
    for clang_dir in build/config/clang build/toolchain/ios; do
      rm -rf "$clang_dir/xcode-clang"
      ln -s "$PWD/.angle-xcode-clang" "$clang_dir/xcode-clang"
    done
  '';
  installHook = ''
    OUT_DIR=out
    mkdir -p $out/lib $out/include $out/nix-support
    EGL_ARCHIVE=$(find "$OUT_DIR" -type f \( -name libEGL_static.a -o -name libEGL.a \) -print | LC_ALL=C sort | awk 'NR == 1 { print; exit }')
    GLES_ARCHIVE=$(find "$OUT_DIR" -type f -name libGLESv2_static.a -print | LC_ALL=C sort | awk 'NR == 1 { print; exit }')
    if [ -z "$EGL_ARCHIVE" ] || [ -z "$GLES_ARCHIVE" ]; then
      echo "ERROR: expected libEGL.a and libGLESv2_static.a in ANGLE watchOS build output" >&2
      find "$OUT_DIR" -name '*.a' -print | LC_ALL=C sort >&2 || true
      exit 1
    fi
    LLVM_AR=$(command -v llvm-ar)
    printf 'CREATE %s\nADDLIB %s\nSAVE\nEND\n' \
      "$TMPDIR/libEGL-materialized.a" "$EGL_ARCHIVE" | "$LLVM_AR" -M
    {
      printf 'CREATE %s\n' "$TMPDIR/libGLESv2-materialized.a"
      # Prefer the GN static archive when it is a real multi-member fat
      # archive. Falling back to every .o pulls vkmock + vulkan-loader twice
      # (duplicate vkCreate*Surface / PresentRectangles) and breaks watch link.
      if "$LLVM_AR" t "$GLES_ARCHIVE" 2>/dev/null | awk 'END { exit !(NR > 1) }'; then
        printf 'ADDLIB %s\n' "$GLES_ARCHIVE"
      else
        find "$OUT_DIR" -type f -name '*.o' \
          ! -path '*/libEGL_static/*' \
          ! -path '*/vkmock/*' \
          ! -path '*/tests/*' \
          ! -path '*/unittests/*' \
          -print | LC_ALL=C sort |
        while IFS= read -r object; do
          printf 'ADDMOD %s/%s\n' "$PWD" "$object"
        done
      fi
      printf 'SAVE\nEND\n'
    } | "$LLVM_AR" -M
    # Ensure null display symbols exist (needed when ADDLIB path is used).
    if ! nm -g "$TMPDIR/libGLESv2-materialized.a" 2>/dev/null | grep -q IsVulkanNullDisplayAvailable; then
      NULL_O=$(find "$OUT_DIR" -type f -name 'DisplayVkNull.o' -print | LC_ALL=C sort | awk 'NR==1{print;exit}')
      if [ -n "$NULL_O" ]; then
        "$LLVM_AR" r "$TMPDIR/libGLESv2-materialized.a" "$NULL_O"
      fi
    fi
    # Same as iOS: namespace ANGLE entry points so iland's EGL shim owns the
    # public symbols (shim calls angle_eglGetDisplay, etc.).
    ${pkgs.bash}/bin/bash ${./rename-angle-symbols.sh} \
      "$TMPDIR/libEGL-materialized.a" $out/lib/libEGL.a
    ${pkgs.bash}/bin/bash ${./rename-angle-symbols.sh} \
      "$TMPDIR/libGLESv2-materialized.a" $out/lib/libGLESv2.a
    # Fail closed: no Mac Metal Vulkan display path in the Watch archive.
    if nm -g $out/lib/libGLESv2.a 2>/dev/null | c++filt | grep -E 'CreateVulkanMacDisplay' >/dev/null; then
      echo "ERROR: Watch ANGLE archive still contains CreateVulkanMacDisplay" >&2
      nm -g $out/lib/libGLESv2.a | c++filt | grep CreateVulkanMacDisplay >&2 || true
      exit 1
    fi
    if ! nm -g $out/lib/libGLESv2.a 2>/dev/null | c++filt | grep -q IsVulkanNullDisplayAvailable; then
      echo "ERROR: Watch ANGLE archive missing DisplayVkNull" >&2
      exit 1
    fi
    # Prefer a single definition of each vk* WSI entry (vkmock + loader = link fail).
    dup=$(nm -g $out/lib/libGLESv2.a 2>/dev/null | awk '/ T _vkDestroySurfaceKHR$/ {c++} END {print c+0}')
    if [ "$dup" -gt 1 ]; then
      echo "ERROR: Watch ANGLE archive has $dup copies of vkDestroySurfaceKHR" >&2
      exit 1
    fi
    cp -r include/* "$out/include/" 2>/dev/null || true
    cp -r third_party/angle/include/* "$out/include/" 2>/dev/null || true
    echo static > "$out/nix-support/link-kind"
    echo vulkan > "$out/nix-support/angle-backend"
  '';
}
