{
  lib,
  pkgs,
  buildPackages,
  androidToolchain,
  ...
}:

let
  src = pkgs.fetchFromGitHub {
    owner = "google";
    repo = "swiftshader";
    rev = "436722b391188ad8c1d1d5dd2447c38ac7f71439";
    hash = "sha256-bIWrl2ZGBfmmQxCNvwyLWQAezgMKY6QKwxp7StfdgqQ=";
  };
  glslangSrc = pkgs.fetchFromGitHub {
    owner = "KhronosGroup";
    repo = "glslang";
    rev = "2b2523fb951f63f072cfba514c26f2feea5f4329";
    hash = "sha256-47vN1gTxRa3MU9avmxVJ/E7MeR9cnjJiheCFBPdci1U=";
  };
  googletestSrc = pkgs.fetchFromGitHub {
    owner = "google";
    repo = "googletest";
    rev = "e2239ee6043f73722e7aa812a459f54a28552929";
    hash = "sha256-SjlJxushfry13RGA7BCjYC9oZqV4z6x8dOiHfl/wpF0=";
  };
in
pkgs.stdenv.mkDerivation {
  pname = "swiftshader-android";
  version = "436722b";
  inherit src;

  patches = [
    (pkgs.fetchurl {
      url = "https://raw.githubusercontent.com/termux/termux-packages/20127306bf/packages/swiftshader/swiftshader-no-android.patch";
      hash = "sha256-giMt5iWgwWEEeZwZ8AO1vSEBORnYfl5Y3Y3dZlufaTo=";
    })
    (pkgs.fetchurl {
      url = "https://raw.githubusercontent.com/termux/termux-packages/20127306bf/packages/swiftshader/src-Vulkan-VkImage-cpp.patch";
      hash = "sha256-B4O5Vh2MlgQCy+NPQvxa6HuP9pjdmtqVP9Nw9TXLmOY=";
    })
    (pkgs.fetchurl {
      url = "https://raw.githubusercontent.com/termux/termux-packages/20127306bf/packages/swiftshader/src-Vulkan-libVulkan-cpp.patch";
      hash = "sha256-IOhQ6NlrFz3SZYFYK9ZzX/wy35YPfGIX1JaUmtaqUfY=";
    })
    (pkgs.fetchurl {
      url = "https://raw.githubusercontent.com/termux/termux-packages/20127306bf/packages/swiftshader/src-Reactor-Debug-cpp.patch";
      hash = "sha256-N/LzUygGe0DL7LnCo4s4l/Ka69PtvmaCzWBQLAEYoy0=";
    })
    (pkgs.fetchurl {
      url = "https://raw.githubusercontent.com/termux/termux-packages/20127306bf/packages/swiftshader/src-System-Debug-cpp.patch";
      hash = "sha256-9v5ltB6d5NoIIZURAIZq1O2wv4bRbFCjzuYTrzRF3g8=";
    })
  ];

  nativeBuildInputs = with buildPackages; [
    cmake
    pkg-config
    ninja
    python3
    git
    cacert
  ];

  postPatch = ''
    rm -rf third_party/glslang third_party/googletest
    cp -r ${glslangSrc} third_party/glslang
    cp -r ${googletestSrc} third_party/googletest
    chmod -R u+w third_party/glslang third_party/googletest

    if [ -f third_party/marl/CMakeLists.txt ]; then
      sed -i.bak \
        's/cmake_minimum_required(VERSION [0-9.]*)/cmake_minimum_required(VERSION 3.5)/' \
        third_party/marl/CMakeLists.txt
    fi
    if [ -f third_party/googletest/CMakeLists.txt ]; then
      sed -i.bak \
        's/cmake_minimum_required(VERSION [0-9.]*)/cmake_minimum_required(VERSION 3.5)/' \
        third_party/googletest/CMakeLists.txt
    fi

    # Build the bundled app-owned ICD as a portable Linux-style Vulkan driver.
    # Android platform WSI is implemented by the host app; enabling SwiftShader's
    # private HAL path would require non-NDK framework headers. This remains a
    # userland runtime library and never opens a kernel graphics node directly.
    substituteInPlace CMakeLists.txt \
      --replace-fail 'elseif(CMAKE_SYSTEM_NAME MATCHES "Android")
    set(ANDROID TRUE)
    set(CMAKE_CXX_FLAGS "-DANDROID_NDK_BUILD")' \
        'elseif(CMAKE_SYSTEM_NAME MATCHES "Android")
    set(LINUX TRUE)'
    # Bionic provides pthread APIs in libc; an explicit -lpthread does not
    # exist in the NDK sysroot.
    substituteInPlace CMakeLists.txt \
      --replace-fail 'set(OS_LIBS dl pthread)' 'set(OS_LIBS dl)'
    substituteInPlace CMakeLists.txt \
      --replace-fail 'project(SwiftShader C CXX ASM)' \
        'project(SwiftShader C CXX ASM)
add_compile_definitions(__TERMUX__)'
  '';

  configurePhase = ''
    runHook preConfigure
    toolchain="${androidToolchain.androidndkRoot}/build/cmake/android.toolchain.cmake"
    test -f "$toolchain" || {
      echo "Android NDK CMake toolchain not found: $toolchain" >&2
      exit 1
    }
    cmake -S . -B build -GNinja \
      -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
      -DCMAKE_BUILD_TYPE=Release \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-${toString androidToolchain.androidNdkApiLevel} \
      -DANDROID_STL=c++_static \
      -DSWIFTSHADER_BUILD_TESTS=OFF \
      -DSWIFTSHADER_BUILD_PVR=OFF \
      -DSWIFTSHADER_WARNINGS_AS_ERRORS=OFF \
      -DCMAKE_C_FLAGS="-fcommon -D__TERMUX__" \
      -DCMAKE_CXX_FLAGS="-fcommon -D__TERMUX__" \
      -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384 -Wl,-undefined-version" \
      -DCMAKE_MODULE_LINKER_FLAGS="-Wl,-z,max-page-size=16384 -Wl,-undefined-version" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    cmake --build build --parallel "$NIX_BUILD_CORES"
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    cmake --install build --prefix "$out"
    mkdir -p "$out/lib" "$out/lib/vulkan/icd.d"

    icd=$(find build -type f -name libvk_swiftshader.so -print -quit)
    test -n "$icd" || {
      echo "SwiftShader Android Vulkan ICD was not produced" >&2
      exit 1
    }
    install -m755 "$icd" "$out/lib/libvk_swiftshader.so"

    manifest=$(find build -type f -name vk_swiftshader_icd.json -print -quit)
    if [ -n "$manifest" ]; then
      install -m644 "$manifest" "$out/lib/vulkan/icd.d/vk_swiftshader_icd.json"
    else
      cat > "$out/lib/vulkan/icd.d/vk_swiftshader_icd.json" <<'EOF'
    {
      "file_format_version": "1.0.0",
      "ICD": {
        "library_path": "../../libvk_swiftshader.so",
        "api_version": "1.3.0"
      }
    }
    EOF
    fi
    runHook postInstall
  '';

  meta = with lib; {
    description = "SwiftShader software Vulkan ICD for Android";
    homepage = "https://swiftshader.googlesource.com/SwiftShader";
    license = licenses.asl20;
    platforms = platforms.unix;
  };
}
