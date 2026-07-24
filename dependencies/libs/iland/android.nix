# iland userland core for Android (Mode A — in-window GBM/EGL/DRM over ANGLE).
#
# Cross-compiles libiland_userland.a with the NDK via wwn-toolchain. Android
# shims live under ./android/ (IOSurface compat, GBM, DRM IPC stubs); upstream
# drm_linux.c + drm_ioctl.c + egl.c are patched in postPatch.
{
  lib,
  pkgs,
  buildModule,
  androidToolchain ? (import "${toolchainSrc}/dependencies/toolchains/android.nix" {
    inherit lib pkgs;
  }),
  toolchainSrc ? null,
  ...
}:

let
  angle = buildModule.buildForAndroid "angle" { };
  androidDir = ./android;
in
pkgs.stdenv.mkDerivation {
  pname = "iland-userland";
  version = "0.1.0";

  src = ./upstream;

  dontConfigure = true;
  dontFixup = true;

  nativeBuildInputs = [ pkgs.python3 ];

  postPatch = ''
    cp -f ${androidDir}/DisplaySurface.h shims/drm/displaysurface/include/DisplaySurface.h
    cp -f ${androidDir}/iland_present.h shims/drm/drm/include/iland_present.h
    cp -f ${androidDir}/drm.h shims/drm/drm/include/drm.h
    cp -f ${androidDir}/gbm_priv.h shims/gbm/include/gbm_priv.h
    cp -f ${androidDir}/iosurface_compat.h shims/include/iosurface_compat.h

    # Android has no WindowServer plist / CoreGraphics refresh probe.
    sed -i '/static uint32_t get_display_refresh_rate(void)/,/^}/c\
static uint32_t get_display_refresh_rate(void)\
{\
    return 60;\
}' shims/drm/drm/src/drm_linux.c

    # Drop macOS WindowServer plist probe; keep preferred-mode + default sizes.
    python3 - <<'PY'
from pathlib import Path
path = Path("shims/drm/drm/src/drm_linux.c")
text = path.read_text()
start = '    CFURLRef url = CFURLCreateWithFileSystemPath(NULL,'
end = '    if (url) CFRelease(url);'
si = text.find(start)
ei = text.find(end)
if si < 0 or ei < 0:
    raise SystemExit("drm_linux.c CF plist block anchors missing")
ei = text.find('\n', ei) + 1
text = text[:si] + text[ei:]
path.write_text(text)
PY

    sed -i '/#include <IOSurface/d' shims/drm/drm/src/drm_linux.c
    sed -i '/#include <mach\/mach.h>/d' shims/drm/drm/src/drm_linux.c
    sed -i '/#include <CoreFoundation/d' shims/drm/drm/src/drm_linux.c
    sed -i '/#include <CoreGraphics/d' shims/drm/drm/src/drm_linux.c
    sed -i '1i #include "iosurface_compat.h"' shims/drm/drm/src/drm_linux.c

    sed -i 's/mach_port_t surface_port/uint32_t surface_port/g' shims/drm/drm/src/drm_linux.c
    sed -i 's/MACH_PORT_NULL/0/g' shims/drm/drm/src/drm_linux.c
    sed -i 's/surface_port = IOSurfaceCreateMachPort(surf);/surface_port = 0; (void)surf;/g' shims/drm/drm/src/drm_linux.c
    sed -i 's/mach_port_deallocate(mach_task_self(), surface_port);/(void)surface_port;/g' shims/drm/drm/src/drm_linux.c

    # egl.c: no Accelerate / IOSurface / Apple dlopen paths on Android.
    sed -i '/#include <TargetConditionals.h>/d' shims/egl/src/egl.c
    sed -i '/#include <Accelerate\/Accelerate.h>/d' shims/egl/src/egl.c
    sed -i 's|#include <IOSurface/IOSurfaceRef.h>|#include "iosurface_compat.h"|' shims/egl/src/egl.c

    python3 - <<'PY'
from pathlib import Path
path = Path("shims/egl/src/egl.c")
text = path.read_text()
old = "        g_zerocopy_enabled = (e && e[0] == '0') ? 0 : 1;"
if old not in text:
    raise SystemExit("egl.c zerocopy default anchor missing")
text = text.replace(old, "        g_zerocopy_enabled = 0; (void)e;", 1)
old_vimage = """    vImage_Buffer buf = {
        .data     = dst8,
        .width    = w,
        .height   = h,
        .rowBytes = dst_pitch_bytes,
    };
    vImagePermuteChannels_ARGB8888(&buf, &buf, kRGBAToBGRAMap, 0);"""
new_vimage = """    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = dst8 + (size_t)y * dst_pitch_bytes;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *px = row + (size_t)x * 4;
            uint8_t r = px[0], b = px[2];
            px[0] = b;
            px[2] = r;
        }
    }"""
count = text.count(old_vimage)
if count < 1:
    raise SystemExit(f"egl.c vImage anchor missing (found {count})")
text = text.replace(old_vimage, new_vimage)
old_egl = '    g_angle_handle = open_angle_library("/opt/local/lib/libEGL.dylib");'
new_egl = """#if defined(__ANDROID__)
    g_angle_handle = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
#else
    g_angle_handle = open_angle_library("/opt/local/lib/libEGL.dylib");
#endif"""
if old_egl not in text:
    raise SystemExit("egl.c mac EGL dlopen anchor missing")
text = text.replace(old_egl, new_egl, 1)
old_gles = '    void *h = open_angle_library("/opt/local/lib/libGLESv2.dylib");'
new_gles = """#if defined(__ANDROID__)
    void *h = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
#else
    void *h = open_angle_library("/opt/local/lib/libGLESv2.dylib");
#endif"""
if old_gles not in text:
    raise SystemExit("egl.c mac GLES dlopen anchor missing")
text = text.replace(old_gles, new_gles, 1)
path.write_text(text)
PY
  '';

  buildPhase = ''
    runHook preBuild

    CC="${androidToolchain.androidCC}"
    AR="${androidToolchain.androidAR}"

    INCLUDES="\
      -Ishims/include \
      -I${androidDir} \
      -Ishims/drm/displaysurface/include \
      -Ishims/drm/drm/include \
      -Ishims/gbm/include \
      -Ishims/egl/include \
      -I${angle}/include \
      -I${angle}/include/EGL \
      -I${angle}/include/GLES2"

    COMMON_FLAGS="-fPIC -O2 -std=c11 $INCLUDES -Wno-int-conversion -D_GNU_SOURCE"

    OBJS=""
    for src in \
      ${androidDir}/iosurface_compat.c \
      ${androidDir}/DisplaySurface.c \
      ${androidDir}/gbm.c \
      ${androidDir}/drm_ipc_stubs.c \
      shims/drm/drm/src/drm_linux.c \
      shims/drm/drm/src/drm_ioctl.c \
      shims/egl/src/egl.c; do
      obj="$(basename "$src").o"
      echo "CC $src"
      "$CC" -c "$src" $COMMON_FLAGS -o "$obj"
      OBJS="$OBJS $obj"
    done

    "$AR" rcs libiland_userland.a $OBJS

    runHook postBuild
  '';

  installPhase = ''
    mkdir -p $out/lib $out/include/EGL $out/include/GLES2 $out/include/GLES3 $out/include/KHR $out/nix-support

    cp libiland_userland.a $out/lib/

    cp shims/gbm/include/gbm.h                       $out/include/
    cp shims/egl/include/egl_shim.h                  $out/include/
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
    # Consumed by Wawona's iland_presenter_android.c (JNI presenter bridge).
    cp shims/include/iosurface_compat.h              $out/include/

    cp -r ${angle}/include/EGL/.   $out/include/EGL/
    cp -r ${angle}/include/GLES2/. $out/include/GLES2/
    cp -r ${angle}/include/GLES3/. $out/include/GLES3/ || true
    cp -r ${angle}/include/KHR/.   $out/include/KHR/

    echo "${angle}" > $out/nix-support/angle-path
    echo "dylib" > $out/nix-support/link-kind
    echo "mode-a-userland" > $out/nix-support/iland-mode
  '';

  passthru = {
    inherit angle;
    angleLibs = "${angle}/lib";
  };

  meta = with lib; {
    description = "iland userland in-window Linux-graphics compat (GBM/EGL/DRM + ANGLE) for Android";
    homepage = "https://github.com/wawona/iland";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
