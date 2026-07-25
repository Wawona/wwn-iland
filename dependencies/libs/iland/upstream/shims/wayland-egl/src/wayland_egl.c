/*
 * iland libwayland-egl replacement.
 *
 * Upstream libwayland ships wayland-egl as a vendor-replaced stub: the four
 * public entry points below are expected to come from the GL implementation,
 * which owns the buffer allocation and the wl_surface attach. On Apple there is
 * no Mesa to provide them, which is why weston-simple-egl was stubbed out and
 * both GPU cubes ended up hosted on iland's KMS emulation instead of Wayland.
 *
 * A window here carries no pixels. It is the client's declared geometry plus
 * the target wl_surface; the swapchain (IOSurface + ANGLE pbuffer + wl_buffer)
 * is created by eglCreateWindowSurface and lives in shims/egl/src/egl_wayland.c.
 */
#include <stdint.h>
#include <stdlib.h>

#include "iland_wayland_egl.h"

#define ILAND_WL_EGL_MAGIC 0x69574557u /* 'iWEW' */

struct wl_egl_window {
    uint32_t magic;
    struct wl_surface *surface;

    int width;
    int height;

    /* Geometry of the most recent committed buffer. Clients poll this to know
     * whether their resize has taken effect yet. */
    int attached_width;
    int attached_height;

    /* Attach offset requested alongside a resize, consumed by the next commit. */
    int dx;
    int dy;
    int resized;
};

struct wl_egl_window *wl_egl_window_create(struct wl_surface *surface,
                                           int width, int height)
{
    if (!surface || width <= 0 || height <= 0)
        return NULL;

    struct wl_egl_window *win = calloc(1, sizeof(*win));
    if (!win)
        return NULL;

    win->magic = ILAND_WL_EGL_MAGIC;
    win->surface = surface;
    win->width = width;
    win->height = height;
    /* Nothing committed yet; upstream reports 0x0 until the first attach. */
    win->attached_width = 0;
    win->attached_height = 0;

    return win;
}

void wl_egl_window_destroy(struct wl_egl_window *win)
{
    if (!win || win->magic != ILAND_WL_EGL_MAGIC)
        return;
    win->magic = 0;
    free(win);
}

void wl_egl_window_resize(struct wl_egl_window *win, int width, int height,
                          int dx, int dy)
{
    if (!win || win->magic != ILAND_WL_EGL_MAGIC)
        return;
    if (width <= 0 || height <= 0)
        return;
    if (win->width == width && win->height == height && dx == 0 && dy == 0)
        return;

    win->width = width;
    win->height = height;
    win->dx += dx;
    win->dy += dy;
    win->resized = 1;
}

void wl_egl_window_get_attached_size(struct wl_egl_window *win,
                                     int *width, int *height)
{
    if (!win || win->magic != ILAND_WL_EGL_MAGIC)
        return;
    if (width)
        *width = win->attached_width;
    if (height)
        *height = win->attached_height;
}

int iland_wl_egl_window_is_valid(const struct wl_egl_window *win)
{
    return win && win->magic == ILAND_WL_EGL_MAGIC;
}

struct wl_surface *iland_wl_egl_window_get_surface(const struct wl_egl_window *win)
{
    return iland_wl_egl_window_is_valid(win) ? win->surface : NULL;
}

void iland_wl_egl_window_get_size(const struct wl_egl_window *win,
                                  int *width, int *height)
{
    if (!iland_wl_egl_window_is_valid(win))
        return;
    if (width)
        *width = win->width;
    if (height)
        *height = win->height;
}

int iland_wl_egl_window_take_resize(struct wl_egl_window *win, int *dx, int *dy)
{
    if (!iland_wl_egl_window_is_valid(win) || !win->resized)
        return 0;

    if (dx)
        *dx = win->dx;
    if (dy)
        *dy = win->dy;

    win->resized = 0;
    win->dx = 0;
    win->dy = 0;
    return 1;
}

void iland_wl_egl_window_set_attached(struct wl_egl_window *win,
                                      int width, int height)
{
    if (!iland_wl_egl_window_is_valid(win))
        return;
    win->attached_width = width;
    win->attached_height = height;
}
