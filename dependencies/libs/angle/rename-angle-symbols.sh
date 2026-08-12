#!/usr/bin/env bash
# Keep iland's public EGL/GLES shim and statically linked ANGLE in one link
# unit by namespacing ANGLE's public entry points that the shim also exports.
#
# Applied to both libEGL.a and libGLESv2.a (each archive only has a subset of
# the names; missing symbols are skipped). After rename, the shim owns the
# public `_egl*` / `_glEGL*` spellings at link time and reaches ANGLE via
# eglGetProcAddress (string lookup — unaffected by these link-time renames).
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

NM="${LLVM_NM:-}"
if [ -z "$NM" ] || ! command -v "$NM" >/dev/null 2>&1; then
  NM="$(xcrun --find llvm-nm 2>/dev/null || true)"
fi
if [ -z "$NM" ] || ! command -v "$NM" >/dev/null 2>&1; then
  NM="$(command -v nm || true)"
fi
if [ -z "$NM" ]; then
  echo "ERROR: nm/llvm-nm not found" >&2
  exit 1
fi

cp "$in" "$out"

# Materialized archives from llvm-ar -M may lack a symbol index; nm then
# reports nothing and we would skip the rename (visionOS GLESv2 footgun).
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
  # The shim defines these too (EGL_EXT_platform_base, which it advertises), so
  # they must move aside even though the shim never calls ANGLE's versions.
  eglCreatePlatformWindowSurface eglCreatePlatformWindowSurfaceEXT
  eglGetPlatformDisplay eglGetPlatformDisplayEXT
  # EGL_KHR_image / GLES OES_EGL_image — force-loaded static ANGLE on visionOS
  # collided with iland's IOSurface dma_buf shim (`ld: duplicate symbol`).
  eglCreateImageKHR eglDestroyImageKHR
  eglCreateImage eglDestroyImage
  glEGLImageTargetTexture2DOES
)

# List defined globals. Prefer --defined-only (llvm-nm); BSD nm uses -U for the
# same meaning (llvm-nm's -U is the opposite — undefined-only). Use $NF so both
# `ADDR T _sym` and `---------------- T _sym` formats work.
list_defined() {
  local archive="$1"
  if "$NM" --defined-only -g "$archive" >/dev/null 2>&1; then
    "$NM" --defined-only -g "$archive" 2>/dev/null | awk '{ print $NF }'
  else
    "$NM" -gU "$archive" 2>/dev/null | awk '{ print $NF }'
  fi
}

defined="$(list_defined "$out" | sort -u)"
if [ -z "$defined" ]; then
  echo "ERROR: nm found no defined symbols in $in (cannot rename)" >&2
  exit 1
fi

args=()
for sym in "${SYMS[@]}"; do
  if printf '%s\n' "$defined" | grep -qx "_${sym}"; then
    args+=(--redefine-sym "_${sym}=_angle_${sym}")
  fi
done

if [ "${#args[@]}" -eq 0 ]; then
  echo "rename-angle-symbols: no matching public symbols in $in (left unchanged)"
  exit 0
fi

"$OBJCOPY" "${args[@]}" "$out"
echo "rename-angle-symbols: namespaced ${#args[@]} symbols in $(basename "$out")"
