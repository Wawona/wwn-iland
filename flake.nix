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

      mkAndroidSDK = system: pkgs:
        let
          androidConfig = import "${wwn-toolchain}/dependencies/android/sdk-config.nix" {
            inherit system;
            lib = pkgs.lib;
          };
          androidComposition = pkgs.androidenv.composeAndroidPackages {
            cmdLineToolsVersion = "latest";
            platformToolsVersion = "latest";
            buildToolsVersions = [ androidConfig.buildToolsVersion ];
            platformVersions = [ (toString androidConfig.compileSdk) ];
            abiVersions = [ androidConfig.hostEmulatorAbi ];
            systemImageTypes = [ "google_apis_playstore" ];
            includeEmulator = androidConfig.emulatorSupported;
            includeSystemImages = androidConfig.emulatorSupported;
            includeNDK = true;
            includeCmake = true;
            ndkVersions = [ androidConfig.ndkVersion ];
            cmakeVersions = [ androidConfig.cmakeVersion ];
            useGoogleAPIs = false;
          };
          sdkRoot = "${androidComposition.androidsdk}/libexec/android-sdk";
        in {
          androidsdk = androidComposition.androidsdk;
          inherit sdkRoot;
          platformTools = androidComposition.platform-tools;
          cmdlineTools = androidComposition.androidsdk;
          buildTools = "${sdkRoot}/build-tools/${androidConfig.buildToolsVersion}";
          cmake = "${sdkRoot}/cmake/${androidConfig.cmakeVersion}";
          ndk = "${sdkRoot}/ndk/${androidConfig.ndkVersion}";
          emulator =
            if androidConfig.emulatorSupported then
              androidComposition.emulator
            else
              androidComposition.androidsdk;
          systemImage =
            "${sdkRoot}/system-images/android-${toString androidConfig.compileSdk}/google_apis_playstore/${androidConfig.hostEmulatorAbi}";
          androidSdkPackages = { };
          inherit androidConfig;
        };
    in
    {
      # Registry fragment merged by Wawona / standalone builds over baseRegistry.
      registryFragment = {
        iland = withPlatformVariants {
          android = ./dependencies/libs/iland/android.nix;
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
          androidSDK = mkAndroidSDK system pkgs;
          androidAllowExperimentalFallback =
            (builtins.getEnv "WAWONA_ANDROID_EXPERIMENTAL_FALLBACK") == "1"
            || builtins.elem system [ "aarch64-darwin" "aarch64-linux" ];
          tc = mkToolchains {
            inherit pkgs androidSDK androidAllowExperimentalFallback;
            pkgsAndroid = pkgs.pkgsCross.aarch64-android;
            registry = baseRegistry // self.registryFragment;
          };
          isDarwin = builtins.elem system darwinSystems;
        in
        ((if isDarwin then {
          iland-ios = tc.buildForIOS "iland" { };
          iland-macos = tc.buildForMacOS "iland" { };
        } else { }) // {
          iland-android = tc.buildForAndroid "iland" { };
        })
      );

      formatter = forAll (system: (pkgsFor system).nixfmt-rfc-style);
    };
}
