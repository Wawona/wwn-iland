# Link flags for iland + ANGLE + in-process GL clients on Android (NDK).
# Pass nativeDeps with iland, angle, and kmscube (or legacy iland-gl-clients).
{ lib, deps, forceLoadIland ? true }:

let
  strip = d: if d == null then "" else toString d;
  libPath = name:
    if deps ? ${name} && deps.${name} != null then "-L${strip deps.${name}}/lib" else "";
  iland = deps.iland or null;
  angle = deps.angle or null;
  kmscube =
    deps.kmscube or deps."iland-gl-clients" or deps.iland-gl-clients or null;
  vkcube = deps.vkcube or null;
  opengl-cube = deps."opengl-cube" or deps.opengl-cube or null;
  gbm-es2-demo = deps."gbm-es2-demo" or null;
  libPaths = lib.filter (s: s != "") [
    (libPath "iland")
    (libPath "angle")
    (libPath "kmscube")
    (libPath "iland-gl-clients")
    (libPath "vkcube")
    (libPath "opengl-cube")
    (libPath "gbm-es2-demo")
  ];
  ilandArchive =
    if forceLoadIland && iland != null then
      [
        "-Wl,--whole-archive"
        "${strip iland}/lib/libiland_userland.a"
        "-Wl,--no-whole-archive"
      ]
    else
      [ ];
  # Wayland-EGL winsys (AHB dmabuf post). Whole-archive so the constructor
  # registers iland_wl_ops; absent on older iland builds.
  ilandWaylandEglArchive =
    let archive = "${strip iland}/lib/libiland_wayland_egl.a";
    in if iland != null && builtins.pathExists archive then
      [
        "-Wl,--whole-archive"
        archive
        "-Wl,--no-whole-archive"
        "-lwayland-client"
      ]
    else
      [ ];
  clientArchives = lib.concatLists (lib.map (client:
    if client.pkg != null then
      [
        "-Wl,-u,${client.sym}"
        "${strip client.pkg}/lib/${client.lib}"
      ]
    else
      [ ]
  ) [
    {
      pkg = kmscube;
      lib = "libkmscube.a";
      sym = "kmscube_main";
    }
    {
      pkg = vkcube;
      lib = "libvkcube.a";
      sym = "vkcube_main";
    }
    {
      pkg = opengl-cube;
      lib = "libopengl_cube.a";
      sym = "opengl_cube_main";
    }
    {
      pkg = gbm-es2-demo;
      lib = "libgbm_es2_demo.a";
      sym = "gbm_es2_demo_main";
    }
  ]);
  # ANGLE ships as shared libs (libEGL.so / libGLESv2.so) copied into jniLibs;
  # iland's egl shim dlopens them at runtime, so no GL link is needed for iland
  # itself. kmscube_main.o (and other in-process GL clients) call GL ES 2 entry
  # points directly though, and libwawona.so links with --no-undefined, so
  # resolve those against libGLESv2/libEGL (ANGLE's lib dir is already in -L,
  # and the NDK sysroot has system stubs as fallback).
  glesLink =
    if kmscube != null || opengl-cube != null || gbm-es2-demo != null then
      [ "-lGLESv2" "-lEGL" ]
    else
      [ ];
  # gbm_es2_demo is C++ (iostream / exceptions). libwawona.so is otherwise a
  # C link unit; without libc++ the NDK lld fails on std::__ndk1 / __cxa_*.
  # Prefer the static STL so the APK does not need libc++_shared.so in jniLibs
  # (shared -lc++ left DT_NEEDED and crashed at System.loadLibrary).
  cxxLink =
    if gbm-es2-demo != null then
      [ "-lc++_static" "-lc++abi" ]
    else
      [ ];
in
libPaths
++ ilandArchive
++ ilandWaylandEglArchive
++ clientArchives
++ glesLink
++ cxxLink
