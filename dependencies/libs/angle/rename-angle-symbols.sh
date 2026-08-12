#!/usr/bin/env bash
# Keep iland's public EGL/GLES shim and statically linked ANGLE in one link
# unit by namespacing ANGLE's public entry points that the shim also exports.
#
# Applied to both libEGL.a and libGLESv2.a. llvm-objcopy --redefine-sym is a
# no-op for names absent from the archive, so we apply the full known set
# without an nm pre-filter (nm on freshly llvm-ar -M'd GLESv2 archives was
# missing glEGLImageTargetTexture2DOES in the nix sandbox and left it public).
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <input-lib> <output-lib>" >&2
  exit 1
fi

in="$1"
out="$2"
OBJCOPY="${LLVM_OBJCOPY:-llvm-objcopy}"
if ! command -v "$OBJCOPY" >/dev/null 2>&1; then
  OBJCOPY="$(xcrun --find llvm-objcopy 2>/dev/null || true)"
fi
if [ -z "$OBJCOPY" ] || ! command -v "$OBJCOPY" >/dev/null 2>&1; then
  echo "ERROR: llvm-objcopy not found (set LLVM_OBJCOPY)" >&2
  exit 1
fi

cp "$in" "$out"

if command -v llvm-ranlib >/dev/null 2>&1; then
  llvm-ranlib "$out" 2>/dev/null || true
elif command -v ranlib >/dev/null 2>&1; then
  ranlib "$out" 2>/dev/null || true
fi

SYMS=(
  eglGetDisplay eglInitialize eglTerminate eglGetError eglQueryString
  eglGetConfigs eglChooseConfig eglGetConfigAttrib eglCreateContext
  eglDestroyContext eglCreateWindowSurface eglDestroySurface eglMakeCurrent
  eglSwapBuffers eglBindAPI eglWaitGL eglSwapInterval eglCreatePbufferSurface
  eglCreatePbufferFromClientBuffer eglGetCurrentContext eglGetProcAddress
  eglCreatePlatformWindowSurface eglCreatePlatformWindowSurfaceEXT
  eglGetPlatformDisplay eglGetPlatformDisplayEXT
  eglCreateImageKHR eglDestroyImageKHR
  eglCreateImage eglDestroyImage
  glEGLImageTargetTexture2DOES
)

args=()
for sym in "${SYMS[@]}"; do
  args+=(--redefine-sym "_${sym}=_angle_${sym}")
done

"$OBJCOPY" "${args[@]}" "$out"
echo "rename-angle-symbols: applied ${#SYMS[@]} renames to $(basename "$out")"
