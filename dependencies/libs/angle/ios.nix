# ANGLE for Apple mobile — static .a archives (Metal backend, App Store–safe).
# Prebuilt (default): XCSoar static libs force-loaded into the app binary
# (ILAND_ANGLE_STATIC — no dlopen, no Frameworks/libEGL.dylib).
# GN cross-build (usePrebuilt=false) is the from-source fallback.
{
  lib,
  pkgs,
  buildPackages,
  common ? null,
  buildModule ? null,
  simulator ? false,
  iosToolchain ? null,
  usePrebuilt ? true,
}:

let
  isVisionOS = iosToolchain.isVisionOSToolchain or false;
  xrosPatch = ./patches/0001-chromium-build-add-xros-target.patch;
  xrosPatchHash = builtins.hashFile "sha256" xrosPatch;
  sdkPlatform =
    if isVisionOS then
      if simulator then "XRSimulator" else "XROS"
    else if simulator then "iPhoneSimulator" else "iPhoneOS";
  minFlag =
    if isVisionOS then
      # xrOS deployment is encoded in the clang target triple; unlike iOS,
      # Apple clang has no -mvisionos[-simulator]-version-min spelling.
      ""
    else if simulator then
      "-mios-simulator-version-min=${iosToolchain.deploymentTarget}"
    else
      "-miphoneos-version-min=${iosToolchain.deploymentTarget}";
  packageSuffix =
    if isVisionOS then
      if simulator then "visionos-simulator" else "visionos"
    else if simulator then "ios-simulator" else "ios";
in
if usePrebuilt && !isVisionOS && simulator then
  let
    sources = import ./prebuilt-sources.nix { inherit lib pkgs; };
    deviceHeaders = pkgs.fetchurl {
      url = sources.iosArm64.url;
      hash = sources.iosArm64.hash;
    };
  in
  pkgs.stdenv.mkDerivation {
    pname = "angle-ios-simulator";
    version = sources.iosUniversal.version;
    src = pkgs.fetchurl {
      url = sources.iosUniversal.url;
      hash = sources.iosUniversal.hash;
    };
    nativeBuildInputs = [ pkgs.unzip pkgs.gnutar ];
    dontConfigure = true;
    dontBuild = true;
    unpackPhase = "true";
    sourceRoot = ".";
    installPhase = ''
      runHook preInstall
      mkdir -p $out/lib $out/include $out/nix-support
      unzip -q $src -d "$TMPDIR/angle-universal"
      root="$TMPDIR/angle-universal"
      install -m644 "$root/${sources.iosUniversal.simEgl}" $out/lib/libEGL.dylib
      install -m644 "$root/${sources.iosUniversal.simGles}" $out/lib/libGLESv2.dylib
      tar -xzf ${deviceHeaders} -C "$TMPDIR"
      cp -r "$TMPDIR/${sources.iosArm64.unpackDir}/include/"* $out/include/
      echo dylib > $out/nix-support/link-kind
      runHook postInstall
    '';
    meta = with lib; {
      description = "ANGLE OpenGL ES for iOS Simulator (prebuilt dylibs from universal XCFramework)";
      homepage = "https://angleproject.org";
      license = licenses.bsd3;
      platforms = platforms.darwin;
    };
  }
else if usePrebuilt && !isVisionOS && !simulator then
  let
    sources = import ./prebuilt-sources.nix { inherit lib pkgs; };
    deviceHeaders = pkgs.fetchurl {
      url = sources.iosArm64.url;
      hash = sources.iosArm64.hash;
    };
  in
  pkgs.stdenv.mkDerivation {
    pname = "angle-ios";
    version = sources.iosUniversal.version;
    src = pkgs.fetchurl {
      url = sources.iosUniversal.url;
      hash = sources.iosUniversal.hash;
    };
    nativeBuildInputs = [ pkgs.unzip pkgs.gnutar ];
    dontConfigure = true;
    dontBuild = true;
    unpackPhase = "true";
    sourceRoot = ".";
    installPhase = ''
      runHook preInstall
      mkdir -p $out/lib $out/include $out/nix-support
      unzip -q $src -d "$TMPDIR/angle-universal"
      root="$TMPDIR/angle-universal"
      install -m644 "$root/${sources.iosUniversal.deviceEgl}" $out/lib/libEGL.dylib
      install -m644 "$root/${sources.iosUniversal.deviceGles}" $out/lib/libGLESv2.dylib
      tar -xzf ${deviceHeaders} -C "$TMPDIR"
      cp -r "$TMPDIR/${sources.iosArm64.unpackDir}/include/"* $out/include/
      echo dylib > $out/nix-support/link-kind
      runHook postInstall
    '';
    meta = with lib; {
      description = "ANGLE OpenGL ES for iOS device (prebuilt dylibs from universal XCFramework, Metal, App Store–safe)";
      homepage = "https://angleproject.org";
      license = licenses.bsd3;
      platforms = platforms.darwin;
    };
  }
else
  import ./cross-base.nix {
    inherit lib pkgs buildPackages;
    pname = "angle-${packageSuffix}";
    clangBasePath = if isVisionOS then "xcode-clang" else null;
    buildTargets = "angle_common libEGL_static libGLESv2_static";
    patchesExtra = lib.optionals isVisionOS [
      xrosPatch
    ];
    gnExtraFlags = [
      "target_os=\"ios\""
      "target_cpu=\"arm64\""
      "target_environment=\"${if simulator then "simulator" else "device"}\""
    ] ++ lib.optionals isVisionOS [
      "target_platform=\"xros\""
    ] ++ [
      "angle_enable_metal=true"
      "angle_enable_vulkan=false"
      "ios_deployment_target=\"${iosToolchain.deploymentTarget}\""
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
      ${lib.optionalString isVisionOS ''
        rm -rf .angle-xcode-clang
        mkdir -p .angle-xcode-clang/bin
        ln -s "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang" \
          .angle-xcode-clang/bin/clang
        ln -s "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++" \
          .angle-xcode-clang/bin/clang++
        ln -s "$LLVM_AR" .angle-xcode-clang/bin/llvm-ar
        ln -s "$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/lib" \
          .angle-xcode-clang/lib
        for clang_dir in build/config/clang build/toolchain/ios; do
          rm -rf "$clang_dir/xcode-clang"
          ln -s "$PWD/.angle-xcode-clang" "$clang_dir/xcode-clang"
        done
      ''}
    '';
    installHook = ''
      OUT_DIR=out
      mkdir -p $out/lib $out/include $out/nix-support
      EGL_ARCHIVE=$(find "$OUT_DIR" -type f \( -name libEGL_static.a -o -name libEGL.a \) -print | LC_ALL=C sort | awk 'NR == 1 { print; exit }')
      GLES_ARCHIVE=$(find "$OUT_DIR" -type f -name libGLESv2_static.a -print | LC_ALL=C sort | awk 'NR == 1 { print; exit }')
      if [ -z "$EGL_ARCHIVE" ] || [ -z "$GLES_ARCHIVE" ]; then
        echo "ERROR: expected libEGL.a and libGLESv2.a in ANGLE iOS build output" >&2
        find "$OUT_DIR" -name '*.a' -print | LC_ALL=C sort >&2 || true
        exit 1
      fi
      LLVM_AR=$(command -v llvm-ar)
      # Materialize both static archives the same way. Packing every .o under
      # out/ (old GLESv2 path) produced a ~200MB fat archive that sandbox
      # llvm-nm could not list, so the rename canary false-failed.
      printf 'CREATE %s\nADDLIB %s\nSAVE\nEND\n' \
        "$TMPDIR/libEGL-materialized.a" "$EGL_ARCHIVE" | "$LLVM_AR" -M
      printf 'CREATE %s\nADDLIB %s\nSAVE\nEND\n' \
        "$TMPDIR/libGLESv2-materialized.a" "$GLES_ARCHIVE" | "$LLVM_AR" -M
      # Namespace ANGLE's public EGL/GLES entry points that iland's shim also
      # exports, so a -force_load of both archives is one definition each —
      # not weak coexistence. Same script on both archives (skips missing).
      ${pkgs.bash}/bin/bash ${./rename-angle-symbols.sh} \
        "$TMPDIR/libEGL-materialized.a" $out/lib/libEGL.a
      ${pkgs.bash}/bin/bash ${./rename-angle-symbols.sh} \
        "$TMPDIR/libGLESv2-materialized.a" $out/lib/libGLESv2.a
      # Canary: visionOS Ld collides if these remain public beside the shim.
      # Prefer BSD nm -gU (Xcode); never use bare -U with llvm-nm (that flag
      # means undefined-only there, the opposite of BSD nm).
      list_defined() {
        local archive="$1"
        if nm -gU "$archive" >/dev/null 2>&1; then
          nm -gU "$archive" 2>/dev/null || true
        elif command -v llvm-nm >/dev/null 2>&1; then
          llvm-nm --defined-only -g "$archive" 2>/dev/null || true
        else
          nm --defined-only -g "$archive" 2>/dev/null || true
        fi
      }
      for archive in $out/lib/libEGL.a $out/lib/libGLESv2.a; do
        for sym in eglCreateImageKHR eglDestroyImageKHR glEGLImageTargetTexture2DOES; do
          # Match as a whole nm field (avoid awk+$NF; some nm lines confuse -qx).
          # ''${ keeps ${sym} for bash — Nix would otherwise interpolate it.
          if list_defined "$archive" | grep -E "[[:space:]]_''${sym}$" >/dev/null; then
            echo "ERROR: public _$sym still in $archive after rename" >&2
            list_defined "$archive" | grep -E "_''${sym}" >&2 || true
            exit 1
          fi
        done
      done
      # Positive check: GLESv2 must own the namespaced image entrypoint.
      if ! list_defined "$out/lib/libGLESv2.a" \
           | grep -E '[[:space:]]_angle_glEGLImageTargetTexture2DOES$' >/dev/null; then
        echo "ERROR: _angle_glEGLImageTargetTexture2DOES missing from libGLESv2.a" >&2
        echo "--- nm sample (EGLImage) ---" >&2
        list_defined "$out/lib/libGLESv2.a" | grep -i EGLImage | head -n 40 >&2 || true
        exit 1
      fi
      cp -rv include/EGL include/GLES2 include/GLES3 include/KHR $out/include/
      echo static > $out/nix-support/link-kind
      cat > $out/nix-support/angle-build-metadata.json <<'EOF'
      {
        "angleRevision": "7ab02e1d49a649adaba62b8a7fdfabf8144b313f",
        "nixpkgsAngleVersion": "${pkgs.angle.version}",
        "chromiumBuildRevision": "169fcf699b64d2d5e75a391beaec8a7ad6e41a7f",
        "sdkPlatform": "${sdkPlatform}",
        "targetPlatform": "${if isVisionOS then (if simulator then "xrsimulator" else "xros") else (if simulator then "iphonesimulator" else "iphoneos")}",
        "targetEnvironment": "${if simulator then "simulator" else "device"}",
        "targetCpu": "arm64",
        "deploymentTarget": "${iosToolchain.deploymentTarget}",
        "xcodeVersionRange": "26.x",
        "backend": "metal",
        "linkKind": "static",
        "sourceBuild": true,
        "patchSeries": ${if isVisionOS then ''
          [
            {
              "name": "0001-chromium-build-add-xros-target.patch",
              "sha256": "${xrosPatchHash}"
            }
          ]
        '' else "[]"}
      }
      EOF
    '';
  }
