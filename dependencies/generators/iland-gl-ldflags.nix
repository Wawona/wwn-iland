# Link flags for iland + ANGLE + in-process kmscube on Apple mobile targets.
# Pass the platform nativeDeps attrset from flake.nix (must include iland, angle,
# iland-gl-clients).
{ lib, deps, forceLoad ? true, simulator ? false }:

let
  strip = d: if d == null then "" else toString d;
  libPath = name:
    if deps ? ${name} && deps.${name} != null then "-L${strip deps.${name}}/lib" else "";
  iland = deps.iland or null;
  angle = deps.angle or null;
  glClients = deps."iland-gl-clients" or deps.iland-gl-clients or null;
  angleLinkKind =
    if angle != null && builtins.pathExists "${strip angle}/nix-support/link-kind" then
      lib.strings.trim (builtins.readFile "${strip angle}/nix-support/link-kind")
    else
      "static";
  libPaths = lib.filter (s: s != "") [
    (libPath "iland")
    (libPath "angle")
    (libPath "iland-gl-clients")
  ];
  ilandArchive =
    if forceLoad && iland != null then
      [ "-force_load" "${strip iland}/lib/libiland_userland.a" ]
    else
      [ ];
  kmscubeArchive =
    if forceLoad && glClients != null then
      [ "-force_load" "${strip glClients}/lib/libkmscube.a" ]
    else
      [ ];
  # Static ANGLE archives must be force-loaded (C++ lives in-app).
  # Dylib ANGLE is embedded into the app bundle and loaded at runtime via dlopen
  # (see iland egl.c). Do not pass dylib paths to the linker: renamed exports break
  # the Mach-O export trie, and force_load pulls ANGLE's C++ into the app binary.
  angleFlags =
    if angle == null then
      [ ]
    else if angleLinkKind == "dylib" then
      [ ]
    else
      [
        "-force_load" "${strip angle}/lib/libEGL.a"
        "-force_load" "${strip angle}/lib/libGLESv2.a"
      ];
  cxxFlags =
    if angle != null && angleLinkKind != "dylib" then
      [ "-lc++" "-lc++abi" ]
    else
      [ ];
  # iland (Accelerate/vImage) + glib (iconv) final-link deps for Xcode app targets.
  platformSupportLibs = [
    "-framework"
    "Accelerate"
    "-liconv"
  ];
in
libPaths ++ ilandArchive ++ kmscubeArchive ++ angleFlags ++ cxxFlags ++ platformSupportLibs
