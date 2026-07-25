/*
 * iland Wayland-EGL winsys — buffer allocation and surface posting.
 *
 * This is the client half of the IOSurface-as-dmabuf convention that the
 * Wawona compositor already implements (#86): a wl_buffer whose
 * zwp_linux_buffer_params_v1 modifier has bit 63 set carries an IOSurface id in
 * the low bits, and the compositor resolves it with IOSurfaceLookup instead of
 * importing the passed fd.
 *
 * Split of responsibility with egl.c: the swapchain owns the IOSurfaces and
 * their wl_buffers, the EGL shim owns the ANGLE pbuffer bound to each
 * IOSurface (indexed by the swapchain slot, so the pbuffer cache in
 * EGLShimSurface is reused unchanged).
 */
#ifndef ILAND_WL_WINSYS_H
#define ILAND_WL_WINSYS_H

#include <stdint.h>
#include <IOSurface/IOSurfaceRef.h>

struct wl_display;
struct wl_egl_window;

typedef struct IlandWlWinsys IlandWlWinsys;
typedef struct IlandWlSwapchain IlandWlSwapchain;

/* Must not exceed the pbuffer cache in EGLShimSurface (4 slots). Three keeps
 * the client drawing while the compositor holds one and one is queued. */
#define ILAND_WL_SWAPCHAIN_DEPTH 3

#ifdef __cplusplus
extern "C" {
#endif

/* Binds zwp_linux_dmabuf_v1 on a private event queue so the winsys never
 * dispatches (or steals) the client's own events. NULL when the compositor
 * does not offer dmabuf, which is the caller's cue to fail surface creation
 * rather than silently render nowhere. */
IlandWlWinsys *iland_wl_winsys_create(struct wl_display *display);
void iland_wl_winsys_destroy(IlandWlWinsys *ws);

IlandWlSwapchain *iland_wl_swapchain_create(IlandWlWinsys *ws,
                                            struct wl_egl_window *win);
void iland_wl_swapchain_destroy(IlandWlSwapchain *sc);

void iland_wl_swapchain_get_size(const IlandWlSwapchain *sc,
                                 uint32_t *width, uint32_t *height);

/* Blocks until a slot the compositor has released is available.
 * Returns the slot index, or -1. */
int iland_wl_swapchain_acquire(IlandWlSwapchain *sc);

IOSurfaceRef iland_wl_swapchain_iosurface(const IlandWlSwapchain *sc, int slot);

/* attach + damage + commit + flush, and mark the slot busy until release. */
int iland_wl_swapchain_post(IlandWlSwapchain *sc, int slot);

/* Reallocates when the client called wl_egl_window_resize(). Returns 1 when the
 * IOSurfaces were replaced, so the caller must drop its cached pbuffers. */
int iland_wl_swapchain_check_resize(IlandWlSwapchain *sc);

#ifdef __cplusplus
}
#endif

#endif /* ILAND_WL_WINSYS_H */
