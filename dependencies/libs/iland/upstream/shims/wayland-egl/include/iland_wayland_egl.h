/*
 * iland libwayland-egl replacement — internal interface.
 *
 * Upstream's wayland-egl-core.h keeps `struct wl_egl_window` opaque and leaves
 * the layout to the GL vendor. iland is that vendor on Apple/Android: the
 * layout lives in wayland_egl.c and the EGL winsys (shims/egl/src/egl_wayland.c)
 * reaches it only through the accessors below.
 */
#ifndef ILAND_WAYLAND_EGL_H
#define ILAND_WAYLAND_EGL_H

struct wl_surface;
struct wl_egl_window;

/* See iland_wl_ops.h: these live in libiland_wayland_egl.a, and the EGL shim in
 * the core archive reaches them through the ops table instead of by name. */
#define ILAND_WL_EGL_API

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero when `win` is one of ours (guards against a client passing a
 * foreign/garbage native window into eglCreateWindowSurface). */
ILAND_WL_EGL_API int iland_wl_egl_window_is_valid(const struct wl_egl_window *win);

ILAND_WL_EGL_API struct wl_surface *
iland_wl_egl_window_get_surface(const struct wl_egl_window *win);
ILAND_WL_EGL_API void iland_wl_egl_window_get_size(const struct wl_egl_window *win,
                                                   int *width, int *height);

/* Consumes the pending wl_egl_window_resize(): returns non-zero once per
 * resize and reports the attach offset the next commit must apply. */
ILAND_WL_EGL_API int iland_wl_egl_window_take_resize(struct wl_egl_window *win,
                                                     int *dx, int *dy);

/* Records the geometry actually committed, for wl_egl_window_get_attached_size. */
ILAND_WL_EGL_API void iland_wl_egl_window_set_attached(struct wl_egl_window *win,
                                                       int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* ILAND_WAYLAND_EGL_H */
