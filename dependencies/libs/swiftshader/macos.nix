{
  lib,
  pkgs,
  stdenv,
  buildModule ? null,
  # Injected by wwn-toolchain (the Apple toolchain wrapper).
  xcodeUtils,
  # Simulator variant reuses this recipe with a different SDK/sysroot (see
  # ios.nix). macOS is the default.
  appleSdk ? "macosx",
  minVersionFlag ? "-mmacosx-version-min=12.0",
  # Extra -D flags the simulator variant needs (CMAKE_SYSTEM_NAME=iOS,
  # CMAKE_OSX_ARCHITECTURES, CMAKE_OSX_DEPLOYMENT_TARGET, …).
  extraCmakeFlags ? [ ],
  # watchOS cross build (see watchos.nix). No Metal in OS_LIBS.
  watchOsBuild ? false,
  # watchOS store path: static ICD archive for in-process link.
  installStaticIcd ? false,
  ...
}:

# SwiftShader software Vulkan 1.3 ICD for Apple targets. nixpkgs' swiftshader is
# Linux-only (meta.platforms is all *-linux), so we build the upstream CMake
# project ourselves against the macOS / iOS-Simulator SDK. This is the
# last-resort ICD in Wawona's Vulkan fallback chain (selected -> MoltenVK ->
# SwiftShader): a pure-CPU device that always enumerates, so vkcube and other
# Vulkan clients run even on a headless CI VM / Simulator with no usable
# Metal-backed Vulkan device. It stays userland (no kernel graphics node) per
# the mission's runtime-only rule.
#
# Device store builds must remain MoltenVK-only; this ICD is bundled for macOS
# and the iOS *Simulator* / CI shape, never signed into on-device App Store
# artifacts (see verify-iland-graphics-bundle.sh + the bundling gate).
let
  src = pkgs.fetchFromGitHub {
    owner = "google";
    repo = "swiftshader";
    rev = "436722b391188ad8c1d1d5dd2447c38ac7f71439";
    hash = "sha256-bIWrl2ZGBfmmQxCNvwyLWQAezgMKY6QKwxp7StfdgqQ=";
  };
  glslangSrc = pkgs.fetchFromGitHub {
    owner = "KhronosGroup";
    repo = "glslang";
    rev = "2b2523fb951f63f072cfba514c26f2feea5f4329";
    hash = "sha256-47vN1gTxRa3MU9avmxVJ/E7MeR9cnjJiheCFBPdci1U=";
  };
  googletestSrc = pkgs.fetchFromGitHub {
    owner = "google";
    repo = "googletest";
    rev = "e2239ee6043f73722e7aa812a459f54a28552929";
    hash = "sha256-SjlJxushfry13RGA7BCjYC9oZqV4z6x8dOiHfl/wpF0=";
  };
  isWatch = builtins.elem appleSdk [ "watchos" "watchsimulator" ];
  isSimulator = appleSdk != "macosx";
  isCrossApple = isSimulator;
  sdkPlatformName =
    if appleSdk == "watchsimulator" then "WatchSimulator"
    else if appleSdk == "watchos" then "WatchOS"
    else if appleSdk == "iphonesimulator" then "iPhoneSimulator"
    else "MacOSX";
  sdkLeafName =
    if appleSdk == "watchsimulator" then "WatchSimulator"
    else if appleSdk == "watchos" then "WatchOS"
    else if appleSdk == "iphonesimulator" then "iPhoneSimulator"
    else "MacOSX";
in
pkgs.stdenv.mkDerivation {
  pname = "swiftshader-${
    if isWatch then
      if appleSdk == "watchsimulator" then "watchos-sim" else "watchos"
    else if isSimulator then
      "ios-sim"
    else
      "macos"
  }";
  version = "436722b";
  inherit src;

  # Needs the Apple SDK (frameworks + xcrun) at build time.
  __noChroot = true;

  nativeBuildInputs = with pkgs.buildPackages; [
    cmake
    ninja
    pkg-config
    python3
    git
    cacert
    perl
  ];

  postPatch = ''
    rm -rf third_party/glslang third_party/googletest
    cp -r ${glslangSrc} third_party/glslang
    cp -r ${googletestSrc} third_party/googletest
    chmod -R u+w third_party/glslang third_party/googletest

    # cmake >= 4 (nixpkgs macOS) hard-errors on
    # cmake_minimum_required(VERSION < 3.5). The vendored third_party trees
    # (googletest/googlemock, glslang, marl, SPIRV-*, …) still pin ancient
    # minimums in nested CMakeLists, so bump every one to 3.5. Lowering a floor
    # is harmless and CMAKE_POLICY_VERSION_MINIMUM=3.5 keeps policy consistent.
    find third_party -name CMakeLists.txt -print0 2>/dev/null \
      | while IFS= read -r -d "" f; do
      sed -i.bak -E \
        's/cmake_minimum_required\(VERSION [0-9]+(\.[0-9]+)*/cmake_minimum_required(VERSION 3.5/' \
        "$f" || true
    done
${lib.optionalString (isCrossApple && !isWatch) ''
    # The iOS-Simulator SDK has no Cocoa or Quartz (macOS umbrella) frameworks, so
    # SwiftShader's APPLE branch find_library(Cocoa/Quartz) resolves to NOTFOUND
    # and the generate step aborts. They are only used for SwiftShader's macOS
    # window/test harness — the headless CPU Vulkan ICD does not need them — while
    # CoreFoundation, IOSurface, and Metal do exist on iOS. Drop the two
    # macOS-only frameworks from OS_LIBS for the simulator build.
    sed -i.bak \
      -e '/find_library(COCOA_FRAMEWORK Cocoa)/d' \
      -e '/find_library(QUARTZ_FRAMEWORK Quartz)/d' \
      -e 's|set(OS_LIBS "''${COCOA_FRAMEWORK}" "''${QUARTZ_FRAMEWORK}" "''${CORE_FOUNDATION_FRAMEWORK}" "''${IOSURFACE_FRAMEWORK}" "''${METAL_FRAMEWORK}")|set(OS_LIBS "''${CORE_FOUNDATION_FRAMEWORK}" "''${IOSURFACE_FRAMEWORK}" "''${METAL_FRAMEWORK}" "-framework Foundation" "-framework QuartzCore" "-framework UIKit")|' \
      CMakeLists.txt

    # MetalSurface.mm includes <AppKit/NSView.h>, which does not exist on iOS
    # (AppKit is macOS-only). Only the MacOSSurfaceMVK path uses NSView; the
    # VK_EXT_metal_surface path drives CAMetalLayer directly, which is available
    # on iOS. Map NSView -> UIView for the simulator so the file compiles and the
    # Metal-EXT surface still works; SwiftShader remains a headless CPU ICD that
    # Wawona presents through iland regardless.
    perl -0777 -pi -e 's{\#include <AppKit/NSView.h>}{#include <TargetConditionals.h>\n#if TARGET_OS_IPHONE\n#import <UIKit/UIKit.h>\n#define NSView UIView\n#else\n#include <AppKit/NSView.h>\n#endif}' \
      src/WSI/MetalSurface.mm
''}
${lib.optionalString isWatch ''
    # watchOS: no Cocoa, Quartz, Metal, or UIKit window harness. Headless CPU ICD only.
    sed -i.bak \
      -e '/find_library(COCOA_FRAMEWORK Cocoa)/d' \
      -e '/find_library(QUARTZ_FRAMEWORK Quartz)/d' \
      -e '/find_library(METAL_FRAMEWORK Metal)/d' \
      -e 's|set(OS_LIBS "''${COCOA_FRAMEWORK}" "''${QUARTZ_FRAMEWORK}" "''${CORE_FOUNDATION_FRAMEWORK}" "''${IOSURFACE_FRAMEWORK}" "''${METAL_FRAMEWORK}")|set(OS_LIBS "''${CORE_FOUNDATION_FRAMEWORK}" "-framework Foundation")|' \
      CMakeLists.txt
    # SPIRV-Tools fatal-errors on watchOS; downgrade to a status line (headless ICD).
    if [ -f third_party/SPIRV-Tools/CMakeLists.txt ]; then
      sed -i.bak \
        's/message(FATAL_ERROR "Your platform .* is not supported!")/message(STATUS "SPIRV-Tools: watchOS allowed by Wawona patch")/' \
        third_party/SPIRV-Tools/CMakeLists.txt
    fi
    # LLVM Program.inc uses fork/exec/spawn APIs unavailable on watchOS.
    for llvm_prog_inc in \
      third_party/llvm-10.0/llvm/lib/Support/Unix/Program.inc \
      third_party/llvm-subzero/lib/Support/Unix/Program.inc; do
      if [ -f "$llvm_prog_inc" ]; then
        cat > "$TMPDIR/wwn-watch-program-stub.h" <<'EOF'
/* Wawona watchOS SwiftShader stubs (prepended to LLVM Program.inc). */
#include <sys/types.h>
#include <sys/stat.h>
typedef struct { int _stub; } wwn_watch_spawn_actions_t;
typedef wwn_watch_spawn_actions_t posix_spawn_file_actions_t;
static inline int wwn_watch_no_execve(const char *path, char *const argv[], char *const envp[]) {
  (void)path; (void)argv; (void)envp; return -1;
}
static inline int wwn_watch_no_execv(const char *path, char *const argv[]) {
  (void)path; (void)argv; return -1;
}
static inline pid_t wwn_watch_no_fork(void) { return -1; }
static inline int wwn_watch_posix_spawn(pid_t *pid, const char *path,
    const wwn_watch_spawn_actions_t *actions, const void *attrp,
    char *const argv[], char *const envp[]) {
  (void)pid; (void)path; (void)actions; (void)attrp; (void)argv; (void)envp; return -1;
}
static inline int wwn_watch_spawn_actions_addopen(wwn_watch_spawn_actions_t *a,
    int fd, const char *path, int oflag, mode_t mode) {
  (void)a; (void)fd; (void)path; (void)oflag; (void)mode; return 0;
}
static inline int wwn_watch_spawn_actions_init(wwn_watch_spawn_actions_t *a) {
  (void)a; return 0;
}
static inline int wwn_watch_spawn_actions_destroy(wwn_watch_spawn_actions_t *a) {
  (void)a; return 0;
}
static inline int wwn_watch_spawn_actions_adddup2(wwn_watch_spawn_actions_t *a,
    int fd1, int fd2) {
  (void)a; (void)fd1; (void)fd2; return 0;
}
EOF
        cat "$TMPDIR/wwn-watch-program-stub.h" "$llvm_prog_inc" > "$TMPDIR/Program.inc.patched"
        mv "$TMPDIR/Program.inc.patched" "$llvm_prog_inc"
        sed -i.bak \
          -e 's/\bexecve(/wwn_watch_no_execve(/g' \
          -e 's/\bexecv(/wwn_watch_no_execv(/g' \
          -e 's/\bfork(/wwn_watch_no_fork(/g' \
          -e 's/\bposix_spawn(/wwn_watch_posix_spawn(/g' \
          -e 's/\bposix_spawn_file_actions_init(/wwn_watch_spawn_actions_init(/g' \
          -e 's/\bposix_spawn_file_actions_destroy(/wwn_watch_spawn_actions_destroy(/g' \
          -e 's/\bposix_spawn_file_actions_adddup2(/wwn_watch_spawn_actions_adddup2(/g' \
          -e 's/\bposix_spawn_file_actions_addopen(/wwn_watch_spawn_actions_addopen(/g' \
          "$llvm_prog_inc"
        # Drop spawn.h; stubs replace the unavailable declarations.
        sed -i.bak '/#include <spawn.h>/d' "$llvm_prog_inc"
      fi
    done
    # Subzero/LLVM Process.inc uses Mach exception ports unavailable on watchOS.
    for llvm_proc_inc in \
      third_party/llvm-subzero/lib/Support/Unix/Process.inc \
      third_party/llvm-10.0/llvm/lib/Support/Unix/Process.inc; do
      if [ -f "$llvm_proc_inc" ]; then
      cat > "$TMPDIR/wwn-watch-process-stub.h" <<'EOF'
/* Wawona watchOS SwiftShader: Mach exception ports unavailable on watchOS. */
#include <mach/mach.h>
static inline kern_return_t wwn_watch_task_get_exception_ports(
    task_t task, exception_mask_t mask, exception_mask_array_t masks,
    mach_msg_type_number_t *count, exception_handler_array_t handlers,
    exception_behavior_array_t behaviors, thread_state_flavor_array_t flavors) {
  (void)task; (void)mask; (void)masks; (void)count; (void)handlers;
  (void)behaviors; (void)flavors;
  return KERN_FAILURE;
}
static inline kern_return_t wwn_watch_task_set_exception_ports(
    task_t task, exception_mask_t mask, mach_port_t port, exception_behavior_t behavior,
    thread_state_flavor_t flavor) {
  (void)task; (void)mask; (void)port; (void)behavior; (void)flavor;
  return KERN_SUCCESS;
}
#define task_get_exception_ports(task, mask, masks, count, handlers, behaviors, flavors) \
  wwn_watch_task_get_exception_ports((task), (mask), (masks), (count), (handlers), (behaviors), (flavors))
#define task_set_exception_ports(task, mask, port, behavior, flavor) \
  wwn_watch_task_set_exception_ports((task), (mask), (port), (behavior), (flavor))
EOF
      cat "$TMPDIR/wwn-watch-process-stub.h" "$llvm_proc_inc" > "$TMPDIR/Process.inc.patched"
      mv "$TMPDIR/Process.inc.patched" "$llvm_proc_inc"
      sed -i.bak \
        -e 's/\btask_get_exception_ports(/wwn_watch_task_get_exception_ports(/g' \
        -e 's/\btask_set_exception_ports(/wwn_watch_task_set_exception_ports(/g' \
        "$llvm_proc_inc"
      fi
    done
    perl -0777 -pi -e 's{\#include <AppKit/NSView.h>}{#define SWIFTSHADER_WATCHOS_HEADLESS 1}' \
      src/WSI/MetalSurface.mm || true
    # Headless watch ICD: no Metal WSI (SDK has no Metal.framework).
    if [ -f src/WSI/CMakeLists.txt ]; then
      sed -i.bak '/MetalSurface.mm/d' src/WSI/CMakeLists.txt
    fi
    find . -name CMakeLists.txt 2>/dev/null | while IFS= read -r f; do
      sed -i.bak \
        -e '/VK_USE_PLATFORM_METAL_EXT/d' \
        -e '/VK_USE_PLATFORM_MACOS_MVK/d' \
        "$f" || true
    done
''}'';

  configurePhase = ''
    runHook preConfigure

    unset DEVELOPER_DIR
    SDKROOT=$(xcrun --sdk ${appleSdk} --show-sdk-path 2>/dev/null || true)
    if [ ! -d "$SDKROOT" ]; then
      SDKROOT=$(${xcodeUtils.findXcodeScript}/bin/find-xcode)/Contents/Developer/Platforms/${sdkPlatformName}.platform/Developer/SDKs/${sdkLeafName}.sdk
    fi
    test -d "$SDKROOT" || { echo "ERROR: ${appleSdk} SDK not found" >&2; exit 1; }
    export SDKROOT

    CC_LAUNCH="$(command -v clang)"
    CXX_LAUNCH="$(command -v clang++)"
${lib.optionalString isCrossApple ''
    # The nixpkgs macOS stdenv clang *wrapper* re-injects -mmacos-version-min
    # internally (not on the visible argv), which clang refuses alongside
    # -mios-simulator-version-min — and there is no runtime env knob to unbake it.
    # For this iOS-Simulator cross build, bypass the wrapper and drive the
    # unwrapped clang directly: we already pass -isysroot (the iPhoneSimulator SDK
    # has libc++ + system headers), -arch arm64, and the correct min-version flag,
    # which is everything a self-contained SwiftShader (CPU-only, no Apple
    # frameworks) build needs. This is the standard way to CMake-cross to iOS from
    # a macOS-host nix stdenv.
    CC_LAUNCH="${pkgs.stdenv.cc.cc}/bin/clang"
    CXX_LAUNCH="${pkgs.stdenv.cc.cc}/bin/clang++"
    test -x "$CC_LAUNCH" || CC_LAUNCH="${pkgs.stdenv.cc.cc}/bin/clang"
    # The unwrapped clang still drives the nix ld-wrapper, which rejects linking
    # against the impure Xcode iPhoneSimulator SDK (`impure path ... used in
    # link`). This build is already __noChroot and deliberately consumes the
    # host Xcode SDK, so relax purity enforcement for the link step.
    export NIX_ENFORCE_PURITY=0
    # NIX_LDFLAGS injects nix's *macOS* libc++ (-L .../libcxx.../lib), which ld
    # refuses to link into an iOS-Simulator binary ("linking in .tbd built for
    # macOS"). Clear the nix-injected compile/link flags entirely: -isysroot to
    # the iPhoneSimulator SDK already supplies libc++, libSystem, and the crt for
    # the correct platform.
    export NIX_LDFLAGS=""
    export NIX_LDFLAGS_BEFORE=""
    export NIX_CFLAGS_COMPILE=""
    export NIX_CFLAGS_LINK=""
''}
    EXTRA_CMAKE_FLAGS="${lib.concatStringsSep " " extraCmakeFlags}"
    # SwiftShader's Reactor JIT vendors LLVM 10, whose llvm-c/DataTypes.h errors
    # unless the C99 limit/constant/format macros are defined before <cstdint>
    # on a modern libc++ (Apple clang). Define them project-wide.
    STDC_MACRO_FLAGS="-D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS"
    cmake -S . -B build -GNinja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER="$CC_LAUNCH" \
      -DCMAKE_CXX_COMPILER="$CXX_LAUNCH" \
      -DCMAKE_OSX_SYSROOT="$SDKROOT" \
      $EXTRA_CMAKE_FLAGS \
      -DSWIFTSHADER_BUILD_TESTS=OFF \
      -DSWIFTSHADER_BUILD_PVR=OFF \
      -DSWIFTSHADER_BUILD_BENCHMARKS=OFF \
      -DSWIFTSHADER_WARNINGS_AS_ERRORS=OFF \
      -DCMAKE_C_FLAGS="${minVersionFlag} $STDC_MACRO_FLAGS" \
      -DCMAKE_CXX_FLAGS="${minVersionFlag} $STDC_MACRO_FLAGS" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    cmake --build build --parallel "$NIX_BUILD_CORES"
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out/lib" "$out/nix-support"
    ${lib.optionalString (!installStaticIcd) ''
      mkdir -p "$out/lib/vulkan/icd.d"
      icd=$(find build -type f -name 'libvk_swiftshader.dylib' -print -quit)
      test -n "$icd" || {
        echo "SwiftShader ${appleSdk} Vulkan ICD (libvk_swiftshader.dylib) was not produced" >&2
        find build -name 'libvk_swiftshader*' -o -name '*.dylib' | head -50 >&2
        exit 1
      }
      install -m755 "$icd" "$out/lib/libvk_swiftshader.dylib"
      cat > "$out/lib/vulkan/icd.d/vk_swiftshader_icd.json" <<'EOF'
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "../../libvk_swiftshader.dylib",
    "api_version": "1.3.0"
  }
}
EOF
      echo dylib > "$out/nix-support/link-kind"
    ''}
    ${lib.optionalString installStaticIcd ''
      set +e
      LLVM_AR=$(command -v llvm-ar || true)
      [ -n "$LLVM_AR" ] || LLVM_AR=$(command -v ar || true)
      set -e
      static_icd=$(find build -type f -name 'libvk_swiftshader.a' -print -quit || true)
      if [ -n "$static_icd" ]; then
        install -m644 "$static_icd" "$out/lib/libvk_swiftshader.a"
      else
        dylib=$(find build -type f -name 'libvk_swiftshader.dylib' -print -quit || true)
        test -n "$dylib" || {
          echo "SwiftShader ${appleSdk}: no static or shared ICD produced" >&2
          find build -name 'libvk_swiftshader*' | head -20 >&2
          exit 1
        }
        # Mach-O dylibs are not ar archives; materialize a static ICD with Apple libtool.
        echo "SwiftShader ${appleSdk}: materializing static ICD via /usr/bin/libtool -static" >&2
        /usr/bin/libtool -static -o "$out/lib/libvk_swiftshader.a" "$dylib" || {
          echo "SwiftShader ${appleSdk}: libtool -static failed for $dylib" >&2
          exit 1
        }
        test -s "$out/lib/libvk_swiftshader.a" || {
          echo "SwiftShader ${appleSdk}: empty static ICD after libtool" >&2
          exit 1
        }
      fi
      echo static > "$out/nix-support/link-kind"
      echo "Reactor backend: LLVM (Subzero lacks watchOS target; store go/no-go in watchos.nix)" \
        > "$out/nix-support/swiftshader-watch-notes.txt"
    ''}
    runHook postInstall
  '';

  meta = with lib; {
    description = "SwiftShader software Vulkan ICD for ${
      if isWatch then
        if appleSdk == "watchsimulator" then "watchOS Simulator" else "watchOS"
      else if isSimulator then
        "the iOS Simulator"
      else
        "macOS"
    }";
    homepage = "https://swiftshader.googlesource.com/SwiftShader";
    license = licenses.asl20;
    platforms = platforms.darwin;
  };
}
