# Link flags for iland + ANGLE + the in-process cube clients (kmscube,
# opengl-cube, vkcube) on Apple targets. vkcube resolves its Vulkan entry
# points against MoltenVK, which moltenvkLdflags puts on the same link line.
# Mirrors wwn-kmscube/dependencies/generators/kmscube-ldflags.nix.
{ lib, deps, forceLoad ? true, simulator ? false }:

let
  strip = d: if d == null then "" else toString d;
  libPath = name:
    if deps ? ${name} && deps.${name} != null then "-L${strip deps.${name}}/lib" else "";
  iland = deps.iland or null;
  angle = deps.angle or null;
  kmscube =
    deps.kmscube or deps."iland-gl-clients" or deps.iland-gl-clients or null;
  openglCube = deps."opengl-cube" or null;
  vkcube = deps.vkcube or null;
  angleLinkKind =
    if angle == null then
      "none"
    else if builtins.pathExists "${strip angle}/nix-support/link-kind" then
      lib.strings.trim (builtins.readFile "${strip angle}/nix-support/link-kind")
    else if builtins.pathExists "${strip angle}/lib/libEGL.dylib" then
      "dylib"
    else
      "static";
  libPaths = lib.filter (s: s != "") [
    (libPath "iland")
    (libPath "angle")
    (libPath "kmscube")
    (libPath "iland-gl-clients")
    (libPath "opengl-cube")
    (libPath "vkcube")
  ];
  ilandArchive =
    if forceLoad && iland != null then
      [ "-force_load" "${strip iland}/lib/libiland_userland.a" ]
    else
      [ ];
  # Wayland-EGL winsys: a separate archive so KMS-only clients need not link
  # libwayland. egl.c refers to it weakly, and a weak undefined reference does
  # not pull an archive member, so force_load is what actually enables
  # EGL_PLATFORM_WAYLAND. Absent on targets without the winsys (Android, and
  # any iland predating it), where the weak refs stay NULL.
  ilandWaylandEglArchive =
    let archive = "${strip iland}/lib/libiland_wayland_egl.a";
    in if iland != null && builtins.pathExists archive then
      [ "-force_load" archive ]
    else
      [ ];
  # Do not -force_load libkmscube.a beside static ANGLE: iOS 26 ld fails to resolve
  # libc++ for libGLESv2.a when kmscube is force-loaded in the same link unit as
  # WWNIlandPresenter.o. Archive pull via -lkmscube is enough (kmscube_main is referenced).
  # Same pattern for the sibling cubes: an undefined-symbol reference is enough
  # to pull the archive member, and it keeps them off the -force_load list.
  cubeArchive = { dep, entry, lib_ }:
    if forceLoad && dep != null then
      [
        "-L${strip dep}/lib"
        "-Wl,-u,_${entry}"
        "-l${lib_}"
      ]
    else
      [ ];
  kmscubeArchive = cubeArchive {
    dep = kmscube;
    entry = "kmscube_main";
    lib_ = "kmscube";
  };
  openglCubeArchive = cubeArchive {
    dep = openglCube;
    entry = "opengl_cube_main";
    lib_ = "opengl_cube";
  };
  vkcubeArchive = cubeArchive {
    dep = vkcube;
    entry = "vkcube_main";
    lib_ = "vkcube";
  };
  angleFlags =
    if angle == null then
      [ ]
    else if angleLinkKind == "dylib" then
      [ "-lEGL" "-lGLESv2" ]
    else
      [
        "-force_load" "${strip angle}/lib/libEGL.a"
        "-force_load" "${strip angle}/lib/libGLESv2.a"
      ];
  cxxFlags =
    if angle != null && angleLinkKind != "dylib" then
      [ ]
    else
      [ ];
  platformSupportLibs = [
    "-framework"
    "Accelerate"
    "-liconv"
  ];
in
libPaths
++ ilandArchive
++ ilandWaylandEglArchive
++ angleFlags
++ cxxFlags
++ platformSupportLibs
++ kmscubeArchive
++ openglCubeArchive
++ vkcubeArchive
