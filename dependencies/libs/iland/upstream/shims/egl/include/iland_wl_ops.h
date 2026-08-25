/*
 * Link seam between libiland_userland.a and libiland_wayland_egl.a.
 *
 * The winsys is a separate archive because it needs libwayland-client, and a
 * pure KMS client such as kmscube has no business linking Wayland. That leaves
 * the EGL shim, which lives in the core archive, calling into an archive that
 * may not be there.
 *
 * A weak undefined reference does not solve this on Mach-O: ld64 only resolves
 * weak_import against a dylib, so a static link still fails. Instead the core
 * owns a pointer that defaults to NULL, and the winsys fills it from a
 * constructor. Because the winsys is force_loaded when it is linked at all (see
 * iland-gl-ldflags.nix), the constructor is guaranteed to run before any client
 * code — so a non-NULL pointer means the whole archive is present, and a NULL
 * one means this build is KMS-only and must refuse EGL_PLATFORM_WAYLAND.
 */
#ifndef ILAND_WL_OPS_H
#define ILAND_WL_OPS_H

#include <stdint.h>
#include <IOSurface/IOSurfaceRef.h>

struct wl_display;
struct wl_egl_window;

typedef struct IlandWlWinsys IlandWlWinsys;
typedef struct IlandWlSwapchain IlandWlSwapchain;

typedef struct IlandWlOps {
    IlandWlWinsys *(*winsys_create)(struct wl_display *display);
    void (*winsys_destroy)(IlandWlWinsys *ws);

    IlandWlSwapchain *(*swapchain_create)(IlandWlWinsys *ws,
                                          struct wl_egl_window *win);
    void (*swapchain_destroy)(IlandWlSwapchain *sc);
    void (*swapchain_get_size)(const IlandWlSwapchain *sc,
                               uint32_t *width, uint32_t *height);
    int (*swapchain_acquire)(IlandWlSwapchain *sc);
    IOSurfaceRef (*swapchain_iosurface)(const IlandWlSwapchain *sc, int slot);
    int (*swapchain_post)(IlandWlSwapchain *sc, int slot);
    int (*swapchain_check_resize)(IlandWlSwapchain *sc);

    int (*egl_window_is_valid)(const struct wl_egl_window *win);
} IlandWlOps;

#ifdef __cplusplus
extern "C" {
#endif

/* Defined in shims/egl/src/iland_wl_ops.c (core), set by shims/egl/src/egl_wayland.c. */
extern const IlandWlOps *iland_wl_ops;

#ifdef __cplusplus
}
#endif

#endif /* ILAND_WL_OPS_H */
