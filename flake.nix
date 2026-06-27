{
  description = "wwn-iland: Wawona's userland Linux-graphics compat layer (GBM/EGL/DRM over IOSurface/ANGLE) replacing the WindowServer/SkyLight path for Wayland/Weston GL clients on Apple platforms.";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    rust-overlay.url = "github:oxalica/rust-overlay";
    rust-overlay.inputs.nixpkgs.follows = "nixpkgs";
    wwn-toolchain.url = "github:Wawona/wwn-toolchain";
    wwn-toolchain.inputs.nixpkgs.follows = "nixpkgs";
    wwn-toolchain.inputs.rust-overlay.follows = "rust-overlay";
  };

  outputs = { self, nixpkgs, rust-overlay, wwn-toolchain, ... }:
    let
      darwinSystems = [ "x86_64-darwin" "aarch64-darwin" ];
      linuxSystems = [ "x86_64-linux" "aarch64-linux" ];
      allSystems = darwinSystems ++ linuxSystems;
      forAll = nixpkgs.lib.genAttrs allSystems;
      inherit (wwn-toolchain.lib) withPlatformVariants baseRegistry mkToolchains;

      pkgsFor = system: import nixpkgs {
        inherit system;
        overlays = [ (import rust-overlay) ];
        config = { allowUnfree = true; allowUnsupportedSystem = true; android_sdk.accept_license = true; };
      };
    in
    {
      # Registry fragment merged by Wawona / standalone builds over baseRegistry.
      registryFragment = {
        iland = withPlatformVariants {
          android = null;
          ios = ./dependencies/libs/iland/ios.nix;
          ipados = ./dependencies/libs/iland/ios.nix;
          tvos = ./dependencies/libs/iland/tvos.nix;
          visionos = ./dependencies/libs/iland/visionos.nix;
          watchos = ./dependencies/libs/iland/watchos.nix;
          macos = ./dependencies/libs/iland/macos.nix;
        };
      };

      packages = forAll (system:
        let
          pkgs = pkgsFor system;
          tc = mkToolchains { inherit pkgs; registry = baseRegistry // self.registryFragment; };
          isDarwin = builtins.elem system darwinSystems;
        in
        (if isDarwin then {
          iland-ios = tc.buildForIOS "iland" { };
          iland-macos = tc.buildForMacOS "iland" { };
        } else { }));

      formatter = forAll (system: (pkgsFor system).nixfmt-rfc-style);
    };
}
