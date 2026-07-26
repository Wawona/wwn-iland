/*
 * iland Wayland-EGL winsys.
 *
 * Posts IOSurface-backed wl_buffers to a wl_surface using the modifier
 * convention the Wawona compositor implements: bit 63 of the dmabuf modifier
 * marks "the low bits are an IOSurface id", which the compositor resolves with
 * IOSurfaceLookup rather than importing the plane fd. That makes a GPU Wayland
 * client on Apple zero-copy — ANGLE renders into the same IOSurface the
 * compositor samples.
 *
 * Deliberately binds zwp_linux_dmabuf_v1 at version 3 and never asks for
 * dmabuf feedback: Wawona answers feedback with an empty format table (no DRM
 * render node exists on an Apple host), which is a client's signal to fall back
 * to wl_shm. The v3 modifier events carry what we need.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>

#include "linux-dmabuf-v1-client-protocol.h"

#include "DisplaySurface.h"
#include "drm_fourcc.h"
#include "iland_wayland_egl.h"
#include "iland_wl_ops.h"
#include "iland_wl_winsys.h"

/* Same convention as gbm_bo_get_modifier() in shims/gbm/src/gbm.m. */
#define ILAND_IOSURFACE_MODIFIER 0x8000000000000000ULL
#define ILAND_IOSURFACE_ID_MASK  0x7fffffffffffffffULL

struct IlandWlWinsys {
    struct wl_display *display;
    struct wl_event_queue *queue;
    struct zwp_linux_dmabuf_v1 *dmabuf;
};

typedef struct {
    DisplaySurfaceInfo info;
    struct wl_buffer *buffer;
    int busy;
} IlandWlSlot;

struct IlandWlSwapchain {
    IlandWlWinsys *ws;
    struct wl_egl_window *win;
    struct wl_surface *surface;
    uint32_t width;
    uint32_t height;
    int orientation; /* ILAND_WL_SWAPCHAIN_TOP_DOWN or BOTTOM_UP */
    IlandWlSlot slots[ILAND_WL_SWAPCHAIN_DEPTH];
    int pending_dx;
    int pending_dy;
};

/* -------------------------------------------------------------------------
 * Winsys (per EGLDisplay)
 * ------------------------------------------------------------------------- */

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    IlandWlWinsys *ws = (IlandWlWinsys *)data;

    if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0 && !ws->dmabuf) {
        uint32_t want = version < 3 ? version : 3;
        ws->dmabuf = (struct zwp_linux_dmabuf_v1 *)
            wl_registry_bind(registry, name,
                             &zwp_linux_dmabuf_v1_interface, want);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

IlandWlWinsys *iland_wl_winsys_create(struct wl_display *display)
{
    if (!display)
        return NULL;

    IlandWlWinsys *ws = calloc(1, sizeof(*ws));
    if (!ws)
        return NULL;

    ws->display = display;
    ws->queue = wl_display_create_queue(display);
    if (!ws->queue) {
        free(ws);
        return NULL;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    if (!registry) {
        wl_event_queue_destroy(ws->queue);
        free(ws);
        return NULL;
    }
    wl_proxy_set_queue((struct wl_proxy *)registry, ws->queue);
    wl_registry_add_listener(registry, &registry_listener, ws);

    /* Two roundtrips: the first delivers the globals, the second the modifier
     * events the bind above subscribes to. */
    int ok = wl_display_roundtrip_queue(display, ws->queue) >= 0 &&
             wl_display_roundtrip_queue(display, ws->queue) >= 0;

    wl_registry_destroy(registry);

    if (!ok || !ws->dmabuf) {
        if (ws->dmabuf)
            zwp_linux_dmabuf_v1_destroy(ws->dmabuf);
        wl_event_queue_destroy(ws->queue);
        free(ws);
        return NULL;
    }

    return ws;
}

void iland_wl_winsys_destroy(IlandWlWinsys *ws)
{
    if (!ws)
        return;
    if (ws->dmabuf)
        zwp_linux_dmabuf_v1_destroy(ws->dmabuf);
    if (ws->queue)
        wl_event_queue_destroy(ws->queue);
    free(ws);
}

/* -------------------------------------------------------------------------
 * Swapchain slots
 * ------------------------------------------------------------------------- */

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    (void)buffer;
    IlandWlSlot *slot = (IlandWlSlot *)data;
    slot->busy = 0;
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release,
};

static void slot_free(IlandWlSlot *slot)
{
    if (slot->buffer) {
        wl_buffer_destroy(slot->buffer);
        slot->buffer = NULL;
    }
    if (slot->info.surface)
        DisplaySurface_destroy(&slot->info);
    slot->busy = 0;
}

static int slot_alloc(IlandWlSwapchain *sc, IlandWlSlot *slot)
{
    /* Global: the compositor is a separate process for a bundled client, and
     * all it gets is the id in the modifier. */
    slot->info = DisplaySurface_create_global(sc->width, sc->height,
                                              kWSPixelFormatBGRA);
    if (slot->info.surface && sc->orientation == ILAND_WL_SWAPCHAIN_BOTTOM_UP) {
        /* ANGLE renders bottom-up into this and its Metal backend refuses
         * EGL_ANGLE_surface_orientation, so the pixels really are upside down.
         * The wl_buffer carries dmabuf Y_INVERT to say so, but the compositor
         * reaches the IOSurface by id and hands it to CoreAnimation, which has
         * no way back to the buffer's flags — so mark the surface itself. */
        IOSurfaceSetValue(slot->info.surface, CFSTR("WWNBottomUp"),
                          kCFBooleanTrue);
    }
    if (!slot->info.surface)
        return -1;

    uint32_t stride = (uint32_t)IOSurfaceGetBytesPerRow(slot->info.surface);
    uint64_t modifier = ILAND_IOSURFACE_MODIFIER |
                        ((uint64_t)IOSurfaceGetID(slot->info.surface) &
                         ILAND_IOSURFACE_ID_MASK);

    struct zwp_linux_buffer_params_v1 *params =
        zwp_linux_dmabuf_v1_create_params(sc->ws->dmabuf);
    if (!params) {
        DisplaySurface_destroy(&slot->info);
        return -1;
    }

    /* The compositor keys off the modifier and closes this fd without reading
     * it, but zwp_linux_buffer_params_v1.add requires a plane fd and rejects a
     * zero stride. */
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        zwp_linux_buffer_params_v1_destroy(params);
        DisplaySurface_destroy(&slot->info);
        return -1;
    }

    zwp_linux_buffer_params_v1_add(params, fd, 0, 0, stride,
                                   (uint32_t)(modifier >> 32),
                                   (uint32_t)(modifier & 0xffffffffULL));
    close(fd);

    /* Y_INVERT only when the producer wrote bottom-up (ANGLE). Vulkan staging
     * blits are top-down and must not set the flag. */
    uint32_t flags = 0;
    if (sc->orientation == ILAND_WL_SWAPCHAIN_BOTTOM_UP)
        flags = ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT;
    slot->buffer = zwp_linux_buffer_params_v1_create_immed(
        params, (int32_t)sc->width, (int32_t)sc->height,
        DRM_FORMAT_ARGB8888, flags);
    zwp_linux_buffer_params_v1_destroy(params);

    if (!slot->buffer) {
        DisplaySurface_destroy(&slot->info);
        return -1;
    }

    wl_proxy_set_queue((struct wl_proxy *)slot->buffer, sc->ws->queue);
    wl_buffer_add_listener(slot->buffer, &buffer_listener, slot);
    slot->busy = 0;
    return 0;
}

static void slots_free(IlandWlSwapchain *sc)
{
    for (int i = 0; i < ILAND_WL_SWAPCHAIN_DEPTH; i++)
        slot_free(&sc->slots[i]);
}

static int slots_alloc(IlandWlSwapchain *sc)
{
    for (int i = 0; i < ILAND_WL_SWAPCHAIN_DEPTH; i++) {
        if (slot_alloc(sc, &sc->slots[i]) != 0) {
            /* One usable slot still renders (without pipelining); zero does
             * not. */
            if (i == 0) {
                slots_free(sc);
                return -1;
            }
            break;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Swapchain
 * ------------------------------------------------------------------------- */

IlandWlSwapchain *iland_wl_swapchain_create_for_surface(
    IlandWlWinsys *ws, struct wl_surface *surface, uint32_t width,
    uint32_t height, int orientation)
{
    if (!ws || !ws->dmabuf || !surface || width == 0 || height == 0)
        return NULL;

    IlandWlSwapchain *sc = calloc(1, sizeof(*sc));
    if (!sc)
        return NULL;

    sc->ws = ws;
    sc->win = NULL;
    sc->surface = surface;
    sc->width = width;
    sc->height = height;
    sc->orientation = (orientation == ILAND_WL_SWAPCHAIN_BOTTOM_UP)
                          ? ILAND_WL_SWAPCHAIN_BOTTOM_UP
                          : ILAND_WL_SWAPCHAIN_TOP_DOWN;

    if (slots_alloc(sc) != 0) {
        free(sc);
        return NULL;
    }

    return sc;
}

IlandWlSwapchain *iland_wl_swapchain_create(IlandWlWinsys *ws,
                                            struct wl_egl_window *win)
{
    if (!ws || !ws->dmabuf || !iland_wl_egl_window_is_valid(win))
        return NULL;

    struct wl_surface *surface = iland_wl_egl_window_get_surface(win);
    if (!surface)
        return NULL;

    int w = 0, h = 0;
    iland_wl_egl_window_get_size(win, &w, &h);
    if (w <= 0 || h <= 0)
        return NULL;

    IlandWlSwapchain *sc = iland_wl_swapchain_create_for_surface(
        ws, surface, (uint32_t)w, (uint32_t)h, ILAND_WL_SWAPCHAIN_BOTTOM_UP);
    if (!sc)
        return NULL;
    sc->win = win;
    return sc;
}

void iland_wl_swapchain_destroy(IlandWlSwapchain *sc)
{
    if (!sc)
        return;
    slots_free(sc);
    free(sc);
}

void iland_wl_swapchain_get_size(const IlandWlSwapchain *sc,
                                 uint32_t *width, uint32_t *height)
{
    if (!sc)
        return;
    if (width)
        *width = sc->width;
    if (height)
        *height = sc->height;
}

IOSurfaceRef iland_wl_swapchain_iosurface(const IlandWlSwapchain *sc, int slot)
{
    if (!sc || slot < 0 || slot >= ILAND_WL_SWAPCHAIN_DEPTH)
        return NULL;
    return sc->slots[slot].info.surface;
}

int iland_wl_swapchain_acquire(IlandWlSwapchain *sc)
{
    if (!sc)
        return -1;

    for (int attempt = 0; attempt < 64; attempt++) {
        for (int i = 0; i < ILAND_WL_SWAPCHAIN_DEPTH; i++) {
            if (sc->slots[i].buffer && !sc->slots[i].busy)
                return i;
        }
        wl_display_flush(sc->ws->display);
        if (wl_display_dispatch_queue(sc->ws->display, sc->ws->queue) < 0)
            break;
    }

    /* Compositor is holding every slot (or the connection died). Overwriting
     * the oldest tears; blocking forever hangs the client's render thread. */
    return sc->slots[0].buffer ? 0 : -1;
}

int iland_wl_swapchain_post(IlandWlSwapchain *sc, int slot)
{
    if (!sc || slot < 0 || slot >= ILAND_WL_SWAPCHAIN_DEPTH)
        return -1;

    IlandWlSlot *s = &sc->slots[slot];
    if (!s->buffer)
        return -1;

    int dx = sc->pending_dx;
    int dy = sc->pending_dy;
    sc->pending_dx = 0;
    sc->pending_dy = 0;

    wl_surface_attach(sc->surface, s->buffer, dx, dy);

    if (wl_proxy_get_version((struct wl_proxy *)sc->surface) >=
        WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
        wl_surface_damage_buffer(sc->surface, 0, 0,
                                 (int32_t)sc->width, (int32_t)sc->height);
    } else {
        wl_surface_damage(sc->surface, 0, 0,
                          (int32_t)sc->width, (int32_t)sc->height);
    }

    wl_surface_commit(sc->surface);
    wl_display_flush(sc->ws->display);

    s->busy = 1;
    if (sc->win)
        iland_wl_egl_window_set_attached(sc->win, (int)sc->width,
                                         (int)sc->height);
    return 0;
}

int iland_wl_swapchain_present_pixels(IlandWlSwapchain *sc, const void *pixels,
                                      uint32_t stride_bytes)
{
    if (!sc || !pixels || stride_bytes == 0)
        return -1;

    int slot = iland_wl_swapchain_acquire(sc);
    if (slot < 0)
        return -1;

    IlandWlSlot *s = &sc->slots[slot];
    IOSurfaceRef io = s->info.surface;
    if (!io || !s->buffer)
        return -1;

    if (IOSurfaceLock(io, 0, NULL) != kIOReturnSuccess)
        return -1;

    void *base = IOSurfaceGetBaseAddress(io);
    size_t dst_stride = IOSurfaceGetBytesPerRow(io);
    size_t row_bytes = (size_t)sc->width * 4u;
    if (!base || dst_stride < row_bytes || stride_bytes < row_bytes) {
        IOSurfaceUnlock(io, 0, NULL);
        return -1;
    }

    const uint8_t *src = (const uint8_t *)pixels;
    uint8_t *dst = (uint8_t *)base;
    for (uint32_t y = 0; y < sc->height; y++) {
        memcpy(dst + (size_t)y * dst_stride,
               src + (size_t)y * stride_bytes, row_bytes);
    }
    IOSurfaceUnlock(io, 0, NULL);

    return iland_wl_swapchain_post(sc, slot);
}

int iland_wl_swapchain_resize(IlandWlSwapchain *sc, uint32_t width,
                              uint32_t height)
{
    if (!sc || width == 0 || height == 0)
        return 0;
    if (sc->width == width && sc->height == height)
        return 0;

    slots_free(sc);
    sc->width = width;
    sc->height = height;
    if (slots_alloc(sc) != 0)
        return -1;
    return 1;
}

int iland_wl_swapchain_check_resize(IlandWlSwapchain *sc)
{
    if (!sc || !sc->win)
        return 0;

    int dx = 0, dy = 0;
    if (!iland_wl_egl_window_take_resize(sc->win, &dx, &dy))
        return 0;

    sc->pending_dx += dx;
    sc->pending_dy += dy;

    int w = (int)sc->width, h = (int)sc->height;
    iland_wl_egl_window_get_size(sc->win, &w, &h);
    if (w <= 0 || h <= 0)
        return 0;
    if ((uint32_t)w == sc->width && (uint32_t)h == sc->height)
        return 0;

    slots_free(sc);
    sc->width = (uint32_t)w;
    sc->height = (uint32_t)h;
    (void)slots_alloc(sc);

    return 1;
}

/*
 * Hand the EGL shim its entry points. The shim lives in the other archive and
 * cannot name these symbols without forcing every KMS-only client to link
 * Wayland, so linking this archive at all is what switches
 * EGL_PLATFORM_WAYLAND on. See iland_wl_ops.h.
 */
static const IlandWlOps iland_wl_ops_table = {
    .winsys_create          = iland_wl_winsys_create,
    .winsys_destroy         = iland_wl_winsys_destroy,
    .swapchain_create       = iland_wl_swapchain_create,
    .swapchain_destroy      = iland_wl_swapchain_destroy,
    .swapchain_get_size     = iland_wl_swapchain_get_size,
    .swapchain_acquire      = iland_wl_swapchain_acquire,
    .swapchain_iosurface    = iland_wl_swapchain_iosurface,
    .swapchain_post         = iland_wl_swapchain_post,
    .swapchain_check_resize = iland_wl_swapchain_check_resize,
    .egl_window_is_valid    = iland_wl_egl_window_is_valid,
};

__attribute__((constructor))
static void iland_wl_ops_register(void)
{
    iland_wl_ops = &iland_wl_ops_table;
}
