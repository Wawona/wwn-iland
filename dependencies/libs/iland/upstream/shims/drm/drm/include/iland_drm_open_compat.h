/*
 * iland_drm_open_compat.h — Mode A (store-safe) DRM device-open redirect.
 *
 * Force-include (clang -include) into stock DRM/KMS/GBM clients (kmscube,
 * weston drm-backend, es2gears, …) so their raw open of a "/dev/dri/cardN" node
 * reaches iland's in-process virtual DRM fd instead of failing ENOENT. There is
 * no /dev/dri on Apple/Android and — unlike the Mode B dylib — the static Mode A
 * archive cannot interpose libc open() via Dobby + DYLD_INSERT_LIBRARIES.
 *
 * This redirect needs no code injection, no SIP change, and no private
 * entitlements, so it is App Store / Play safe. It is the Mode A counterpart to
 * the Mode B open() hook in wayland-mac.c.
 *
 * Include ONLY in client sources that must be redirected — never in the iland
 * implementation TUs (they need the real libc open()).
 */
#ifndef ILAND_DRM_OPEN_COMPAT_H
#define ILAND_DRM_OPEN_COMPAT_H

/* Pull in the real open() declaration BEFORE we shadow it with the macro, so a
 * later #include <fcntl.h> (include-guarded) does not see the macro and mangle
 * the prototype. */
#include <fcntl.h>
#include <stddef.h>
#include <sys/mman.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Implemented in drm_linux.c. For a "/dev/dri/cardN" path it prepares and returns
 * the in-process virtual DRM fd (so drmModeGetResources(fd) etc. drive the
 * emulated Apple/Android KMS backend). Every other path defers to libc open()
 * with correct O_CREAT mode handling, preserving normal file semantics.
 */
int iland_drm_open_card(const char *path, int flags, ...);

/*
 * Weston drm_fb_create_dumb mmap's the card fd after MAP_DUMB. The virtual
 * fd is a pipe (select/poll), so libc mmap fails and pixman init returns -1
 * ("Failed to init output pixman state"). MAP_DUMB's offset is already the
 * CPU pointer from drmModeCreateDumbBuffer. Return that pointer instead.
 */
void *iland_drm_mmap(void *addr, size_t length, int prot, int flags, int fd,
                     off_t offset);
int iland_drm_munmap(void *addr, size_t length);

#ifdef __cplusplus
}
#endif

/* Redirect open() at the call sites in force-included client sources. Variadic
 * so both open(path, flags) and open(path, flags, mode) compile unchanged. */
#define open(...) iland_drm_open_card(__VA_ARGS__)
#define mmap(...) iland_drm_mmap(__VA_ARGS__)
#define munmap(...) iland_drm_munmap(__VA_ARGS__)

#endif /* ILAND_DRM_OPEN_COMPAT_H */
