#ifndef EGL_SHIM_H
#define EGL_SHIM_H

#include <gbm.h>
#include <EGL/egl.h>

/* Wayland winsys types stay opaque here; see shims/egl/include/iland_wl_winsys.h. */
struct wl_display;
struct wl_egl_window;
typedef struct IlandWlWinsys IlandWlWinsys;
typedef struct IlandWlSwapchain IlandWlSwapchain;

/*
 * Which native platform the client handed us. GBM is iland's userspace
 * KMS/DRM path (kmscube and friends, EGL_DEFAULT_DISPLAY / gbm_device);
 * WAYLAND is a real Wayland client rendering onto Wawona's compositor
 * (eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wl_display, ...)).
 */
typedef enum EGLShimDisplayKind {
    EGL_SHIM_DISPLAY_GBM     = 0,
    EGL_SHIM_DISPLAY_WAYLAND = 1,
} EGLShimDisplayKind;

typedef struct EGLShimDisplay {
    EGLDisplay angle_display;
    struct gbm_device *gbm_device;

    EGLShimDisplayKind kind;
    struct wl_display *wl_display;
    IlandWlWinsys *wl_winsys;      /* bound lazily at eglInitialize */
} EGLShimDisplay;

typedef struct EGLShimSurface {
    EGLSurface angle_surface;          /* ANGLE pbuffer used by the copy path */
    struct gbm_surface *gbm_surface;
    uint32_t width;
    uint32_t height;

    /* Zero-copy path (ILAND_EGL_ZEROCOPY=1): instead of glReadPixels + a CPU
     * channel-swap into the IOSurface, ANGLE renders directly into an
     * IOSurface-backed Metal texture via EGL_ANGLE_iosurface_client_buffer.
     * One ANGLE pbuffer is cached per gbm bo (must cover GBM_NUM_BUFFERS = 4)
     * or, on Wayland, per swapchain slot. Lazily created on first present. */
    int        zerocopy;
    EGLConfig  config;
    EGLSurface iosurf_pbuffers[4];

    /* Where the client actually draws, blitted into the presented IOSurface at
     * swap. An IOSurface pbuffer has no depth or stencil however the config was
     * chosen, so rendering straight into one silently breaks GL_DEPTH_TEST; a
     * plain pbuffer of the same config gets both. NULL when the driver cannot
     * blit (GLES2-only), which falls back to drawing into the IOSurface. */
    EGLSurface render_pbuffer;

    /* Destination of that blit: the slot's IOSurface bound as a rectangle
     * texture (eglBindTexImage) and attached to this framebuffer, which is how
     * EGL_ANGLE_iosurface_client_buffer is meant to be consumed. Blitting
     * between two surfaces' *default* framebuffers instead wrote nothing and
     * raised no GL error. Created lazily in the client's context. */
    unsigned int blit_fbo;
    unsigned int blit_tex;

    /* Wayland window surface. The swapchain owns the IOSurfaces and their
     * wl_buffers; iosurf_pbuffers[] above caches the ANGLE render target bound
     * to each slot. */
    int wayland;
    struct wl_egl_window *wl_window;
    IlandWlSwapchain *wl_swapchain;
    int wl_slot;
} EGLShimSurface;

#endif
