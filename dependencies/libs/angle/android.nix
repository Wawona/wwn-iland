{
  lib,
  pkgs,
  buildPackages,
  common ? null,
  buildModule ? null,
  androidToolchain,
  usePrebuilt ? true,
}:

if usePrebuilt then
  let
    sources = import ./prebuilt-sources.nix { inherit lib pkgs; };
    src = pkgs.fetchurl {
      url = sources.androidArm64.url;
      hash = sources.androidArm64.hash;
    };
  in
  pkgs.stdenv.mkDerivation {
    pname = "angle-android";
    version = sources.androidArm64.version;
    inherit src;
    dontConfigure = true;
    dontBuild = true;
    dontUnpack = true;
    installPhase = ''
      runHook preInstall
      tar -xzf $src
      mkdir -p $out/lib $out/include
      cp lib/libEGL.so $out/lib/
      cp lib/libGLESv2.so $out/lib/
      cp -r include/* $out/include/
      runHook postInstall
    '';
    meta = with lib; {
      description = "ANGLE OpenGL ES for Android (prebuilt shared)";
      homepage = "https://angleproject.org";
      license = licenses.bsd3;
      platforms = [
        "aarch64-linux-android"
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
    };
  }
else
  let
    ndkRoot = androidToolchain.androidndkRoot;
    ndkHostTag = androidToolchain.androidNdkHostTag;
  in
  import ./cross-base.nix {
    inherit lib pkgs buildPackages;
    pname = "angle-android";
    gnExtraFlags = [
      "target_os=\"android\""
      "target_cpu=\"arm64\""
      "android_ndk_root=\"${ndkRoot}\""
      "android_ndk_api_level=${toString androidToolchain.androidNdkApiLevel}"
      "angle_enable_vulkan=true"
      "angle_enable_metal=false"
    ];
    preConfigureHook = ''
      export CC="${androidToolchain.androidCC}"
      export CXX="${androidToolchain.androidCXX}"
      export AR="${androidToolchain.androidAR}"
      export PATH="${ndkRoot}/toolchains/llvm/prebuilt/${ndkHostTag}/bin:$PATH"
    '';
    installHook = ''
      OUT_DIR=out
      mkdir -p $out/lib $out/include
      EGL_ARCHIVE=$(find "$OUT_DIR" -type f -name libEGL.a -print -quit)
      GLES_ARCHIVE=$(find "$OUT_DIR" -type f -name libGLESv2.a -print -quit)
      [ -n "$EGL_ARCHIVE" ] && install -m644 "$EGL_ARCHIVE" $out/lib/
      [ -n "$GLES_ARCHIVE" ] && install -m644 "$GLES_ARCHIVE" $out/lib/
      test -f $out/lib/libEGL.a
      test -f $out/lib/libGLESv2.a
      cp -rv include/EGL include/GLES2 include/GLES3 include/KHR $out/include/
    '';
  }
