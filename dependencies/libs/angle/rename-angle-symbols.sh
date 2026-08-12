#!/usr/bin/env bash
# Keep iland's public EGL/GLES shim and statically linked ANGLE in one link
# unit by namespacing ANGLE's public entry points that the shim also exports.
#
# Applied to both libEGL.a and libGLESv2.a. Whole-archive llvm-objcopy
# --redefine-sym is unreliable on Mach-O .a files in the nix sandbox (it can
# exit 0 while leaving glEGLImageTargetTexture2DOES public). Extract each
# member (xN for duplicate basenames), rename the object, then re-ar + ranlib.
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "usage: $0 <input-lib> <output-lib>" >&2
  exit 1
fi

in="$1"
out="$2"

resolve_tool() {
  local env_name="$1" fallback="$2" xcrun_name="$3"
  local v="${!env_name:-}"
  if [ -n "$v" ] && command -v "$v" >/dev/null 2>&1; then
    printf '%s\n' "$v"
    return
  fi
  if command -v "$fallback" >/dev/null 2>&1; then
    printf '%s\n' "$fallback"
    return
  fi
  local xc
  xc="$(xcrun --find "$xcrun_name" 2>/dev/null || true)"
  if [ -n "$xc" ] && command -v "$xc" >/dev/null 2>&1; then
    printf '%s\n' "$xc"
    return
  fi
  return 1
}

OBJCOPY="$(resolve_tool LLVM_OBJCOPY llvm-objcopy llvm-objcopy)" || {
  echo "ERROR: llvm-objcopy not found (set LLVM_OBJCOPY)" >&2
  exit 1
}
AR="$(resolve_tool LLVM_AR llvm-ar llvm-ar)" || AR=ar
RANLIB="$(resolve_tool LLVM_RANLIB llvm-ranlib llvm-ranlib)" || RANLIB=ranlib

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

# Non-archive: rename a single Mach-O object.
if ! "$AR" t "$in" >/dev/null 2>&1; then
  cp "$in" "$out"
  "$OBJCOPY" "${args[@]}" "$out"
  echo "rename-angle-symbols: applied ${#SYMS[@]} renames to $(basename "$out")"
  exit 0
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/rename-angle-XXXXXX")"
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

cp "$in" "$work/in.a"
mkdir "$work/members" "$work/extract"

# Occurrence count per basename so xN pulls the right duplicate.
declare -A seen=()
idx=0
while IFS= read -r member; do
  case "$member" in
    __.SYMDEF*|__/|/) continue ;;
  esac
  seen["$member"]=$((${seen["$member"]:-0} + 1))
  n=${seen["$member"]}
  idx=$((idx + 1))
  dest="$work/members/$(printf '%05d' "$idx").o"
  (
    cd "$work/extract"
    rm -f -- "$member"
    "$AR" xN "$n" "$work/in.a" "$member"
    if [ ! -f "$member" ]; then
      echo "ERROR: failed to extract [$n] $member from $in" >&2
      exit 1
    fi
    mv -- "$member" "$dest"
  )
  # redefine-sym is a no-op for absent names; ignore non-Mach-O members.
  "$OBJCOPY" "${args[@]}" "$dest" 2>/dev/null || true
done < <("$AR" t "$work/in.a")
if [ "$idx" -eq 0 ]; then
  echo "ERROR: empty archive: $in" >&2
  exit 1
fi

rm -f "$out"
"$AR" rc "$out" "$work"/members/*
"$RANLIB" "$out"

echo "rename-angle-symbols: processed ${idx} members (${#SYMS[@]} redefine-sym) -> $(basename "$out")"
