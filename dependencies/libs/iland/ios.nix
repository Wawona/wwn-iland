# iland userland core for iOS (Mode A — in-window, App-Store-safe shape).
#
# Builds the IOSurface/ANGLE-backed Linux-graphics compat shims as a single
# static archive (libiland_userland.a) for nested GL clients (kmscube,
# weston-simple-egl). EGL calls link directly against ANGLE static libs
# (ILAND_ANGLE_STATIC — no dlopen).
{
  lib,
  pkgs,
  stdenv,
  buildModule,
  simulator ? false,
  iosToolchain ? (import ../../apple/default.nix { inherit lib pkgs; }),
  # Injected by wwn-toolchain (xcodeUtils === the apple toolchain). Previously
  # imported via ../../utils/xcode-wrapper.nix; falls back to iosToolchain.
  xcodeUtils ? iosToolchain,
  ...
}:

let
  angle = buildModule.buildForIOS "angle" { inherit simulator; };
  # Wayland-EGL winsys (same IOSurface-as-dmabuf path as macOS): libwayland-client
  # for the protocol calls, scanner + XML for the linux-dmabuf client bindings.
  libwayland = buildModule.buildForIOS "libwayland" { inherit simulator; };
  waylandScanner = pkgs.wayland-scanner;
  waylandProtocols = pkgs.wayland-protocols;
  angleLinkKind =
    if builtins.pathExists "${angle}/nix-support/link-kind" then
      lib.strings.trim (builtins.readFile "${angle}/nix-support/link-kind")
    else
      "static";
  angleStaticFlag = if angleLinkKind == "static" then "-DILAND_ANGLE_STATIC" else "";
  # buildForVisionOS sets isVisionOSToolchain + xros/xrsimulator SDK; do not
  # hardcode iPhoneSimulator or ld rejects libiland_userland.a (platform 7 vs 12).
  isVisionOS = iosToolchain.isVisionOSToolchain or false;
  minVersion = iosToolchain.deploymentTarget or (if isVisionOS then "26.0" else "17.0");
  sdkPlatform =
    if isVisionOS then
      if simulator then "XRSimulator" else "XROS"
    else if simulator then
      "iPhoneSimulator"
    else
      "iPhoneOS";
  minFlag =
    if isVisionOS && simulator then
      "-target arm64-apple-xros${minVersion}-simulator"
    else if isVisionOS then
      "-target arm64-apple-xros${minVersion}"
    else if simulator then
      "-mios-simulator-version-min=${minVersion}"
    else
      "-miphoneos-version-min=${minVersion}";
in
pkgs.stdenv.mkDerivation {
  pname = "iland-userland";
  version = "0.1.0";

  src = ./upstream;

  __noChroot = true;
  dontConfigure = true;

  # wayland-scanner is multi-output; take it from PATH.
  nativeBuildInputs = [ waylandScanner ];

  postPatch = ''
    find shims -type f \( -name '*.h' -o -name '*.m' -o -name '*.c' \) \
      -exec sed -i 's|IOSurface/IOSurface.h|IOSurface/IOSurfaceRef.h|g' {} +

    # iOS has no bootstrap.h — stub Mode B Mach IPC helpers (Mode A uses present callback).
    cat > shims/drm/drm/src/drm_ios_ipc_stubs.c <<'EOF'
#include "drm.h"
#include <mach/mach.h>

int drm_send_json(const char *json)
{
    (void)json;
    return -1;
}

int drm_send_json_with_surface(const char *json, mach_port_t surface_port)
{
    (void)json;
    (void)surface_port;
    return -1;
}

int drm_receive_present_ack(unsigned timeout_ms)
{
    (void)timeout_ms;
    return -1;
}
EOF
  '';

  buildPhase = ''
    runHook preBuild

    unset DEVELOPER_DIR
    if [ -z "''${XCODE_APP:-}" ]; then
      XCODE_APP=$(${xcodeUtils.findXcodeScript}/bin/find-xcode || true)
      [ -n "$XCODE_APP" ] && export DEVELOPER_DIR="$XCODE_APP/Contents/Developer"
    fi
    export SDKROOT="$DEVELOPER_DIR/Platforms/${sdkPlatform}.platform/Developer/SDKs/${sdkPlatform}.sdk"
    CLANG="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
    AR="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar"

    # linux-dmabuf client bindings for the Wayland-EGL winsys.
    DMABUF_XML="${waylandProtocols}/share/wayland-protocols/unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml"
    wayland-scanner client-header "$DMABUF_XML" linux-dmabuf-v1-client-protocol.h
    wayland-scanner private-code  "$DMABUF_XML" linux-dmabuf-v1-protocol.c

    INCLUDES="\
      -I. \
      -Ishims/include \
      -Ishims/drm/displaysurface/include \
      -Ishims/drm/drm/include \
      -Ishims/gbm/include \
      -Ishims/egl/include \
      -Ishims/wayland-egl/include \
      -I${libwayland}/include \
      -I${libwayland}/include/wayland \
      -I${angle}/include \
      -I${angle}/include/EGL \
      -I${angle}/include/GLES2"

    COMMON_FLAGS="-arch arm64 -isysroot $SDKROOT ${minFlag} -fPIC -O2 -std=c11 \
      ${angleStaticFlag} $INCLUDES -framework IOSurface -framework Foundation \
      -framework CoreFoundation -framework CoreGraphics -framework QuartzCore -framework Metal"

    OBJS=""
    for src in \
      shims/drm/displaysurface/src/DisplaySurface.m \
      shims/gbm/src/gbm.m \
      shims/drm/drm/src/drm_linux.c \
      shims/drm/drm/src/drm_ioctl.c \
      shims/drm/drm/src/drm_ios_ipc_stubs.c \
      shims/wayland-egl/src/wayland_egl.c \
      shims/egl/src/egl_wayland.c \
      linux-dmabuf-v1-protocol.c \
      shims/egl/src/egl.c; do
      obj="$(basename "$src").o"
      echo "CC $src"
      "$CLANG" -c "$src" $COMMON_FLAGS -o "$obj"
      OBJS="$OBJS $obj"
    done

    "$AR" rcs libiland_userland.a $OBJS

    runHook postBuild
  '';

  installPhase = ''
    mkdir -p $out/lib/pkgconfig $out/include/EGL $out/include/GLES2 $out/include/GLES3 $out/include/KHR

    cp libiland_userland.a $out/lib/

    cp shims/gbm/include/gbm.h                       $out/include/
    cp shims/egl/include/egl_shim.h                  $out/include/
    # Wayland-EGL winsys. wl_egl_window_* live in this archive, so clients must
    # NOT also link libwayland-egl (that one is an abort-on-call vendor stub).
    cp shims/egl/include/iland_wl_winsys.h           $out/include/
    cp shims/wayland-egl/include/iland_wayland_egl.h $out/include/
    cp shims/drm/displaysurface/include/DisplaySurface.h $out/include/
    cp shims/include/drm_fourcc.h                    $out/include/
    cp shims/include/xf86drm.h                       $out/include/
    cp shims/include/xf86drmMode.h                   $out/include/
    cp shims/include/esUtil.h                        $out/include/ || true
    cp shims/drm/drm/include/drm.h                   $out/include/ || true
    cp shims/drm/drm/include/iland_present.h         $out/include/
    # Mode A store-safe open() redirect — force-included by GL/DRM clients so
    # their raw open("/dev/dri/cardN") reaches the in-process virtual fd (#58).
    cp shims/drm/drm/include/iland_drm_open_compat.h $out/include/

    cat > $out/lib/pkgconfig/gbm.pc <<EOF
prefix=$out
libdir=\''${prefix}/lib
includedir=\''${prefix}/include

Name: gbm
Description: wwn-iland IOSurface-backed GBM ABI
Version: 1.0.0
Libs: -L\''${libdir} -liland_userland
Cflags: -I\''${includedir}
EOF

    cp -r ${angle}/include/EGL/.   $out/include/EGL/
    cp -r ${angle}/include/GLES2/. $out/include/GLES2/
    cp -r ${angle}/include/GLES3/. $out/include/GLES3/ || true
    cp -r ${angle}/include/KHR/.   $out/include/KHR/

    mkdir -p $out/nix-support
    echo "${angle}" > $out/nix-support/angle-path
    echo "mode-a-userland" > $out/nix-support/iland-mode
  '';

  passthru = {
    inherit angle;
    angleLibs = "${angle}/lib";
  };

  meta = with lib; {
    description = "iland userland in-window Linux-graphics compat (GBM/EGL/DRM over IOSurface+ANGLE) for iOS";
    homepage = "https://github.com/wawona/iland";
    license = licenses.mit;
    platforms = platforms.darwin;
  };
}
