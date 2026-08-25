{
  lib,
  pkgs,
  simulator ? false,
  iosToolchain,
  ...
}:

let
  isVisionOS = iosToolchain.isVisionOSToolchain or false;
  isTVOS = iosToolchain.isTVOSToolchain or false;
  platformName =
    if isTVOS then "tvos" else if isVisionOS then "visionos" else "ios";
  slice =
    if isTVOS then
      if simulator then "tvos-arm64_x86_64-simulator" else "tvos-arm64_arm64e"
    else if isVisionOS then
      if simulator then "xros-arm64_x86_64-simulator" else "xros-arm64"
    else if simulator then
      "ios-arm64_x86_64-simulator"
    else
      "ios-arm64";
in
pkgs.stdenv.mkDerivation {
  pname = "moltenvk-${platformName}${lib.optionalString simulator "-sim"}";
  version = "1.4.1";

  src = pkgs.fetchurl {
    url = "https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-all.tar";
    hash = "sha256-LEmL+MmLiLoehMHxU0A9TBqEkMEi2eKj3yOLJdThBVc=";
  };

  dontConfigure = true;
  dontBuild = true;
  sourceRoot = ".";
  unpackPhase = ''
    mkdir source
    tar -xf "$src" -C source
    cd source
  '';

  installPhase = ''
    runHook preInstall
    root="MoltenVK/MoltenVK"
    archive="$root/static/MoltenVK.xcframework/${slice}/libMoltenVK.a"
    if [ ! -f "$archive" ]; then
      echo "missing $archive; xcframework slices:" >&2
      ls -1 "$root/static/MoltenVK.xcframework" >&2 || true
      exit 1
    fi
    mkdir -p "$out/lib" "$out/include" "$out/nix-support"
    install -m644 "$archive" "$out/lib/libMoltenVK.a"
    cp -R "$root/include/." "$out/include/"
    cat > "$out/nix-support/moltenvk-build-metadata.json" <<EOF
{
  "version": "1.4.1",
  "upstreamTag": "v1.4.1",
  "releaseAsset": "MoltenVK-all.tar",
  "releaseAssetSha256": "2c498bf8c98b88ba1e84c1f153403d4c1a8490c122d9e2a3df238b25d4e10557",
  "slice": "${slice}",
  "linkKind": "static",
  "privateApiVariant": false
}
EOF
    runHook postInstall
  '';

  meta = with lib; {
    description = "Pinned store-safe MoltenVK static archive for Apple mobile";
    homepage = "https://github.com/KhronosGroup/MoltenVK";
    license = licenses.asl20;
    platforms = platforms.darwin;
  };
}
