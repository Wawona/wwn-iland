#ifndef ILAND_ANGLE_STATIC
#include <dlfcn.h>
#endif
#include <TargetConditionals.h>

#ifdef ILAND_ANGLE_STATIC
/* Rename before any Khronos header include (egl_shim.h pulls in EGL/egl.h). */
#define eglGetDisplay                  angle_eglGetDisplay
#define eglInitialize                  angle_eglInitialize
#define eglTerminate                   angle_eglTerminate
#define eglGetError                    angle_eglGetError
#define eglQueryString                 angle_eglQueryString
#define eglGetConfigs                  angle_eglGetConfigs
#define eglChooseConfig                angle_eglChooseConfig
#define eglGetConfigAttrib             angle_eglGetConfigAttrib
#define eglCreateContext               angle_eglCreateContext
#define eglDestroyContext              angle_eglDestroyContext
#define eglCreateWindowSurface         angle_eglCreateWindowSurface
#define eglDestroySurface              angle_eglDestroySurface
#define eglMakeCurrent                 angle_eglMakeCurrent
#define eglSwapBuffers                 angle_eglSwapBuffers
#define eglBindAPI                     angle_eglBindAPI
#define eglWaitGL                      angle_eglWaitGL
#define eglSwapInterval                angle_eglSwapInterval
#define eglCreatePbufferSurface        angle_eglCreatePbufferSurface
#define eglCreatePbufferFromClientBuffer angle_eglCreatePbufferFromClientBuffer
#define eglGetCurrentContext           angle_eglGetCurrentContext
#define eglBindTexImage                angle_eglBindTexImage
#define eglReleaseTexImage             angle_eglReleaseTexImage
#define eglGetProcAddress              angle_eglGetProcAddress
#endif

#include <egl_shim.h>
#include <gbm_priv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <IOSurface/IOSurfaceRef.h>
#include <Accelerate/Accelerate.h>

/* Wayland-EGL winsys: IOSurface-backed wl_buffers posted via linux-dmabuf.
 * Apple-only for now — the Android equivalent posts AHardwareBuffer instead. */
#if defined(__APPLE__) && !defined(ILAND_NO_WL_WINSYS)
#define ILAND_HAVE_WL_WINSYS 1
#include "iland_wayland_egl.h"
#include "iland_wl_winsys.h"
#include "iland_wl_ops.h"

/* NULL unless libiland_wayland_egl.a is linked, which is what makes this a
 * Wayland-capable build; see iland_wl_ops.h. Calling the winsys by name would
 * put an undefined symbol in every KMS-only client, so the names below are
 * redirected through the table and the entry points check iland_wl_ops first. */
const IlandWlOps *iland_wl_ops = NULL;

#define iland_wl_winsys_destroy(...)       iland_wl_ops->winsys_destroy(__VA_ARGS__)
#define iland_wl_swapchain_create(...)     iland_wl_ops->swapchain_create(__VA_ARGS__)
#define iland_wl_swapchain_destroy(...)    iland_wl_ops->swapchain_destroy(__VA_ARGS__)
#define iland_wl_swapchain_get_size(...)   iland_wl_ops->swapchain_get_size(__VA_ARGS__)
#define iland_wl_swapchain_acquire(...)    iland_wl_ops->swapchain_acquire(__VA_ARGS__)
#define iland_wl_swapchain_iosurface(...)  iland_wl_ops->swapchain_iosurface(__VA_ARGS__)
#define iland_wl_swapchain_post(...)       iland_wl_ops->swapchain_post(__VA_ARGS__)
#define iland_wl_swapchain_check_resize(...) \
    iland_wl_ops->swapchain_check_resize(__VA_ARGS__)
#define iland_wl_egl_window_is_valid(...)  iland_wl_ops->egl_window_is_valid(__VA_ARGS__)
#endif

#ifdef ILAND_ANGLE_STATIC
#include <GLES2/gl2.h>
#undef eglGetDisplay
#undef eglInitialize
#undef eglTerminate
#undef eglGetError
#undef eglQueryString
#undef eglGetConfigs
#undef eglChooseConfig
#undef eglGetConfigAttrib
#undef eglCreateContext
#undef eglDestroyContext
#undef eglCreateWindowSurface
#undef eglDestroySurface
#undef eglMakeCurrent
#undef eglSwapBuffers
#undef eglBindAPI
#undef eglWaitGL
#undef eglSwapInterval
#undef eglCreatePbufferSurface
#undef eglCreatePbufferFromClientBuffer
#undef eglGetCurrentContext
#undef eglGetProcAddress
#undef eglBindTexImage
#undef eglReleaseTexImage
#endif

static void *g_angle_handle = NULL;

#ifdef ILAND_ANGLE_STATIC
/* Renamed ANGLE entry points from the block above — use for typeof/load. */
#define ILAND_ANGLE_SYM(name) angle_##name
#else
#define ILAND_ANGLE_SYM(name) name
#endif

#define ANGLE_FN(name) static __typeof__(&ILAND_ANGLE_SYM(name)) real_##name = NULL

/* Bit used to mark duplicate EGLConfigs that report XRGB8888 */
#define XRGB_DUP_BIT ((EGLConfig)(uintptr_t)0x80000000)

ANGLE_FN(eglGetDisplay);
ANGLE_FN(eglInitialize);
ANGLE_FN(eglTerminate);
ANGLE_FN(eglGetError);
ANGLE_FN(eglQueryString);
ANGLE_FN(eglGetConfigs);
ANGLE_FN(eglChooseConfig);
ANGLE_FN(eglGetConfigAttrib);
ANGLE_FN(eglCreateContext);
ANGLE_FN(eglDestroyContext);
ANGLE_FN(eglCreateWindowSurface);
ANGLE_FN(eglDestroySurface);
ANGLE_FN(eglMakeCurrent);
ANGLE_FN(eglSwapBuffers);
ANGLE_FN(eglBindAPI);
ANGLE_FN(eglWaitGL);
ANGLE_FN(eglSwapInterval);
ANGLE_FN(eglCreatePbufferSurface);
ANGLE_FN(eglCreatePbufferFromClientBuffer);
ANGLE_FN(eglGetCurrentContext);
ANGLE_FN(eglGetProcAddress);
ANGLE_FN(eglBindTexImage);
ANGLE_FN(eglReleaseTexImage);

static void (*g_glReadPixels)(int, int, int, int, unsigned int, unsigned int, void *) = NULL;
static void (*g_glFinish)(void) = NULL;
/* Prefer a fence over glFinish: Finish stalls the CPU until every prior
 * command retires, which is what made swaps feel like half-rate when the
 * present path was already GPU-bound. */
#ifndef EGL_SYNC_FENCE
#define EGL_SYNC_FENCE 0x30F9
#endif
#ifndef EGL_SYNC_FLUSH_COMMANDS_BIT
#define EGL_SYNC_FLUSH_COMMANDS_BIT 0x0001
#endif
#ifndef EGL_FOREVER
#define EGL_FOREVER 0xFFFFFFFFFFFFFFFFull
#endif
typedef void *WwnEglSync;
static WwnEglSync (*g_eglCreateSync)(EGLDisplay, EGLenum, const intptr_t *) = NULL;
static EGLint (*g_eglClientWaitSync)(EGLDisplay, WwnEglSync, EGLint, uint64_t) = NULL;
static EGLBoolean (*g_eglDestroySync)(EGLDisplay, WwnEglSync) = NULL;

/* Flush GPU work for the posted buffer without a full-pipeline glFinish. */
static void zc_flush_gpu(EGLDisplay angle_dpy)
{
    if (g_eglCreateSync && g_eglClientWaitSync && g_eglDestroySync && angle_dpy) {
        WwnEglSync sync = g_eglCreateSync(angle_dpy, EGL_SYNC_FENCE, NULL);
        if (sync) {
            g_eglClientWaitSync(angle_dpy, sync, EGL_SYNC_FLUSH_COMMANDS_BIT,
                                EGL_FOREVER);
            g_eglDestroySync(angle_dpy, sync);
            return;
        }
    }
    if (g_glFinish) g_glFinish();
    else if (real_eglWaitGL) real_eglWaitGL();
}
/* GLES3; NULL on a GLES2-only driver, which then keeps the old direct-render
 * path (and its missing depth buffer) — see zc_render_pbuffer. */
static void (*g_glBindFramebuffer)(unsigned int, unsigned int) = NULL;
static void (*g_glGetIntegerv)(unsigned int, int *) = NULL;
static void (*g_glBlitFramebuffer)(int, int, int, int, int, int, int, int,
                                   unsigned int, unsigned int) = NULL;
static unsigned int (*g_glGetError)(void) = NULL;
/* Reaching the IOSurface as an FBO colour attachment — see zc_blit_to_slot. */
static void (*g_glGenTextures)(int, unsigned int *) = NULL;
static void (*g_glBindTexture)(unsigned int, unsigned int) = NULL;
static void (*g_glGenFramebuffers)(int, unsigned int *) = NULL;
static void (*g_glFramebufferTexture2D)(unsigned int, unsigned int,
                                        unsigned int, unsigned int, int) = NULL;
static unsigned int (*g_glCheckFramebufferStatus)(unsigned int) = NULL;
static void (*g_glDeleteTextures)(int, const unsigned int *) = NULL;
static void (*g_glDeleteFramebuffers)(int, const unsigned int *) = NULL;

/* EGL_ANGLE_iosurface_client_buffer constants (ANGLE-specific; not in stock
 * EGL/egl.h). Values are stable across ANGLE releases. */
#ifndef EGL_IOSURFACE_ANGLE
#define EGL_IOSURFACE_ANGLE                 0x3454
#endif
#ifndef EGL_IOSURFACE_PLANE_ANGLE
#define EGL_IOSURFACE_PLANE_ANGLE           0x345A
#endif
#ifndef EGL_TEXTURE_RECTANGLE_ANGLE
#define EGL_TEXTURE_RECTANGLE_ANGLE         0x345B
#endif
#ifndef EGL_TEXTURE_TYPE_ANGLE
#define EGL_TEXTURE_TYPE_ANGLE              0x345C
#endif
#ifndef EGL_TEXTURE_INTERNAL_FORMAT_ANGLE
#define EGL_TEXTURE_INTERNAL_FORMAT_ANGLE   0x345D
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT                         0x80E1
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE                    0x1401
#endif

/* Set once at first surface creation from $ILAND_EGL_ZEROCOPY. */
static int g_zerocopy_enabled = -1;

static int zerocopy_enabled(void)
{
    if (g_zerocopy_enabled < 0) {
        /*
         * Zero-copy is the DEFAULT: the surface renders straight into a
         * GL_BGRA_EXT IOSurface presented as MTLPixelFormatBGRA8Unorm. The
         * legacy glReadPixels(GL_RGBA) + vImagePermuteChannels copy path
         * double-swaps R/B on Apple/ANGLE (where the readback is already
         * BGRA), tinting nested Weston purple. Opt OUT with
         * ILAND_EGL_ZEROCOPY=0; the copy path remains as a fallback when
         * ANGLE cannot bind the IOSurface.
         */
        const char *e = getenv("ILAND_EGL_ZEROCOPY");
        g_zerocopy_enabled = (e && e[0] == '0') ? 0 : 1;
    }
    return g_zerocopy_enabled;
}

/* Thread-local reusable pixel buffer to avoid malloc/free per frame */
static __thread void  *g_pixels    = NULL;
static __thread size_t g_pixels_sz = 0;

static inline uint32_t rgba_to_bgra(uint32_t rgba)
{
    return (rgba & 0xFF00FF00u) | ((rgba >> 16) & 0xFFu) | ((rgba & 0xFFu) << 16);
}

/* Permute map: RGBA → BGRA (swap byte 0 and byte 2) */
static const uint8_t kRGBAToBGRAMap[4] = { 2, 1, 0, 3 };

static int graphics_policy_allows_angle(void)
{
    const char *disabled = getenv("WWN_DISABLE_EGL");
    const char *driver = getenv("WWN_OPENGL_DRIVER");
    if (disabled && disabled[0] == '1') return 0;
    if (!driver || !driver[0]) return 1;
    return strcmp(driver, "angle") == 0;
}

#ifdef ILAND_ANGLE_STATIC
static int load_angle(void)
{
    if (!graphics_policy_allows_angle()) return -1;
    if (g_angle_handle) return 0;
    g_angle_handle = (void *)1;

#define LOAD(name) real_##name = ILAND_ANGLE_SYM(name)

    LOAD(eglGetDisplay);
    LOAD(eglInitialize);
    LOAD(eglTerminate);
    LOAD(eglGetError);
    LOAD(eglQueryString);
    LOAD(eglGetConfigs);
    LOAD(eglChooseConfig);
    LOAD(eglGetConfigAttrib);
    LOAD(eglCreateContext);
    LOAD(eglDestroyContext);
    LOAD(eglCreateWindowSurface);
    LOAD(eglDestroySurface);
    LOAD(eglMakeCurrent);
    LOAD(eglSwapBuffers);
    LOAD(eglBindAPI);
    LOAD(eglWaitGL);
    LOAD(eglSwapInterval);
    LOAD(eglCreatePbufferSurface);
    LOAD(eglCreatePbufferFromClientBuffer);
    LOAD(eglGetCurrentContext);
    LOAD(eglGetProcAddress);
    LOAD(eglBindTexImage);
    LOAD(eglReleaseTexImage);

    return 0;
}

static void load_gles2(void)
{
    if (g_glReadPixels) return;
    g_glReadPixels = glReadPixels;
    g_glFinish = glFinish;
    g_glBindFramebuffer = glBindFramebuffer;
    g_glGetIntegerv = glGetIntegerv;
    g_glGetError = glGetError;
    g_glGenTextures = glGenTextures;
    g_glBindTexture = glBindTexture;
    g_glGenFramebuffers = glGenFramebuffers;
    g_glFramebufferTexture2D = glFramebufferTexture2D;
    g_glCheckFramebufferStatus = glCheckFramebufferStatus;
    g_glDeleteTextures = glDeleteTextures;
    g_glDeleteFramebuffers = glDeleteFramebuffers;
    if (real_eglGetProcAddress) {
        g_glBlitFramebuffer = (void (*)(int, int, int, int, int, int, int, int,
                                        unsigned int, unsigned int))
            real_eglGetProcAddress("glBlitFramebuffer");
        g_eglCreateSync = (void *)real_eglGetProcAddress("eglCreateSync");
        if (!g_eglCreateSync)
            g_eglCreateSync = (void *)real_eglGetProcAddress("eglCreateSyncKHR");
        g_eglClientWaitSync = (void *)real_eglGetProcAddress("eglClientWaitSync");
        if (!g_eglClientWaitSync)
            g_eglClientWaitSync =
                (void *)real_eglGetProcAddress("eglClientWaitSyncKHR");
        g_eglDestroySync = (void *)real_eglGetProcAddress("eglDestroySync");
        if (!g_eglDestroySync)
            g_eglDestroySync =
                (void *)real_eglGetProcAddress("eglDestroySyncKHR");
    }
}
#else
static void *open_angle_library(const char *path)
{
    void *handle = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (handle) return handle;
    return dlopen(path, RTLD_LAZY);
}

static int load_angle(void)
{
    if (!graphics_policy_allows_angle()) return -1;
    if (g_angle_handle) return 0;
#if TARGET_OS_IPHONE
    static const char *candidates[] = {
        "@executable_path/Frameworks/libEGL.dylib",
        "libEGL.dylib",
        NULL,
    };
    for (size_t i = 0; candidates[i]; i++) {
        g_angle_handle = open_angle_library(candidates[i]);
        if (g_angle_handle) break;
    }
#elif defined(__ANDROID__)
    /* System ANGLE/GLES from the NDK loader; no bundled Apple-style slice. */
    g_angle_handle = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
#else
    /*
     * Prefer an ANGLE that the host process has already mapped. A macOS app
     * bundle links its own copy in Contents/Frameworks, so loading a second
     * image from an absolute path gives dyld two definitions of ANGLE's
     * Objective-C classes (ANGLESwapCGLLayer), which crashes the client. The
     * @rpath name matches the bundled install name, so dyld hands back the
     * existing image instead of mapping another one. The absolute path stays
     * last for unbundled use: CLI tools, and Mode B injection into processes
     * that carry no Wawona rpath.
     */
    static const char *candidates[] = {
        "@rpath/libEGL.dylib",
        "@executable_path/../Frameworks/libEGL.dylib",
        "@executable_path/Frameworks/libEGL.dylib",
        "/opt/local/lib/libEGL.dylib",
        NULL,
    };
    for (size_t i = 0; candidates[i]; i++) {
        g_angle_handle = open_angle_library(candidates[i]);
        if (g_angle_handle) break;
    }
#endif
    if (!g_angle_handle) return -1;

#define LOAD(name) do { \
    real_##name = dlsym(g_angle_handle, #name); \
    if (!real_##name) return -1; \
} while(0)

    LOAD(eglGetDisplay);
    LOAD(eglInitialize);
    LOAD(eglTerminate);
    LOAD(eglGetError);
    LOAD(eglQueryString);
    LOAD(eglGetConfigs);
    LOAD(eglChooseConfig);
    LOAD(eglGetConfigAttrib);
    LOAD(eglCreateContext);
    LOAD(eglDestroyContext);
    LOAD(eglCreateWindowSurface);
    LOAD(eglDestroySurface);
    LOAD(eglMakeCurrent);
    LOAD(eglSwapBuffers);
    LOAD(eglBindAPI);
    LOAD(eglWaitGL);
    LOAD(eglSwapInterval);
    LOAD(eglCreatePbufferSurface);
    LOAD(eglCreatePbufferFromClientBuffer);
    LOAD(eglGetCurrentContext);
    LOAD(eglGetProcAddress);
    LOAD(eglBindTexImage);
    LOAD(eglReleaseTexImage);

    return 0;
}

static void load_gles2(void)
{
    if (g_glReadPixels) return;
#if TARGET_OS_IPHONE
    static const char *candidates[] = {
        "@executable_path/Frameworks/libGLESv2.dylib",
        "libGLESv2.dylib",
        NULL,
    };
    void *h = NULL;
    for (size_t i = 0; candidates[i]; i++) {
        h = open_angle_library(candidates[i]);
        if (h) break;
    }
#elif defined(__ANDROID__)
    void *h = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
#else
    /* Same single-image rule as load_angle(). */
    static const char *candidates[] = {
        "@rpath/libGLESv2.dylib",
        "@executable_path/../Frameworks/libGLESv2.dylib",
        "@executable_path/Frameworks/libGLESv2.dylib",
        "/opt/local/lib/libGLESv2.dylib",
        NULL,
    };
    void *h = NULL;
    for (size_t i = 0; candidates[i]; i++) {
        h = open_angle_library(candidates[i]);
        if (h) break;
    }
#endif
    if (!h) return;
    g_glReadPixels = dlsym(h, "glReadPixels");
    g_glFinish    = dlsym(h, "glFinish");
    g_glBindFramebuffer = dlsym(h, "glBindFramebuffer");
    g_glGetIntegerv     = dlsym(h, "glGetIntegerv");
    g_glBlitFramebuffer = dlsym(h, "glBlitFramebuffer");
    g_glGetError        = dlsym(h, "glGetError");
    g_glGenTextures     = dlsym(h, "glGenTextures");
    g_glBindTexture     = dlsym(h, "glBindTexture");
    g_glGenFramebuffers = dlsym(h, "glGenFramebuffers");
    g_glFramebufferTexture2D   = dlsym(h, "glFramebufferTexture2D");
    g_glCheckFramebufferStatus = dlsym(h, "glCheckFramebufferStatus");
    g_glDeleteTextures     = dlsym(h, "glDeleteTextures");
    g_glDeleteFramebuffers = dlsym(h, "glDeleteFramebuffers");
    if (real_eglGetProcAddress) {
        g_eglCreateSync = (void *)real_eglGetProcAddress("eglCreateSync");
        if (!g_eglCreateSync)
            g_eglCreateSync = (void *)real_eglGetProcAddress("eglCreateSyncKHR");
        g_eglClientWaitSync = (void *)real_eglGetProcAddress("eglClientWaitSync");
        if (!g_eglClientWaitSync)
            g_eglClientWaitSync =
                (void *)real_eglGetProcAddress("eglClientWaitSyncKHR");
        g_eglDestroySync = (void *)real_eglGetProcAddress("eglDestroySync");
        if (!g_eglDestroySync)
            g_eglDestroySync =
                (void *)real_eglGetProcAddress("eglDestroySyncKHR");
    }
}
#endif

static EGLShimDisplay *unwrap_display(EGLDisplay dpy)
{
    return (EGLShimDisplay *)dpy;
}

static EGLShimSurface *unwrap_surface(EGLSurface surf)
{
    return (EGLShimSurface *)surf;
}

/*
 * ANGLE can be absent by policy (OpenGLDriver=none / WWN_DISABLE_EGL) or because
 * no slice loaded, in which case every real_egl* pointer is NULL. Entry points
 * must then report failure rather than dispatch through those pointers: stock
 * clients do not all check their way out. kmscube, for one, calls eglInitialize
 * on the EGL_NO_DISPLAY that eglGetDisplay just handed back, which used to jump
 * to address 0 and take the whole host app down over a preference.
 */
#define WWN_REQUIRE_ANGLE(fail_value) \
    do { if (load_angle() < 0) return (fail_value); } while (0)

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    if (load_angle() < 0) return EGL_NO_DISPLAY;

    EGLShimDisplay *dpy = calloc(1, sizeof(*dpy));
    if (!dpy) return EGL_NO_DISPLAY;

    dpy->kind = EGL_SHIM_DISPLAY_GBM;
    dpy->gbm_device = (struct gbm_device *)display_id;
    dpy->angle_display = real_eglGetDisplay(EGL_DEFAULT_DISPLAY);

    return (EGLDisplay)dpy;
}

#ifndef EGL_PLATFORM_WAYLAND_KHR
#define EGL_PLATFORM_WAYLAND_KHR 0x31D8
#endif
#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR     0x31D7
#endif

/*
 * Wayland clients reach us here rather than through eglGetDisplay: a
 * wl_display* and a gbm_device* are both bare pointers, so the platform enum is
 * the only way to tell which winsys the client means.
 */
static EGLDisplay shim_get_platform_display(EGLenum platform, void *native)
{
    if (load_angle() < 0) return EGL_NO_DISPLAY;

    if (platform != EGL_PLATFORM_WAYLAND_KHR)
        return eglGetDisplay((EGLNativeDisplayType)native);

#ifdef ILAND_HAVE_WL_WINSYS
    if (!native) return EGL_NO_DISPLAY;

    /* libiland_wayland_egl.a is not linked: this build is KMS-only. */
    if (!iland_wl_ops) return EGL_NO_DISPLAY;

    EGLShimDisplay *dpy = calloc(1, sizeof(*dpy));
    if (!dpy) return EGL_NO_DISPLAY;

    dpy->kind = EGL_SHIM_DISPLAY_WAYLAND;
    dpy->wl_display = (struct wl_display *)native;
    dpy->angle_display = real_eglGetDisplay(EGL_DEFAULT_DISPLAY);

    return (EGLDisplay)dpy;
#else
    (void)native;
    return EGL_NO_DISPLAY;
#endif
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display,
                                 const EGLAttrib *attrib_list)
{
    (void)attrib_list;
    return shim_get_platform_display(platform, native_display);
}

EGLDisplay eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                    const EGLint *attrib_list)
{
    (void)attrib_list;
    return shim_get_platform_display(platform, native_display);
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    load_gles2();
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglInitialize(dpy, major, minor);
    if (!real_eglInitialize(sd->angle_display, major, minor))
        return EGL_FALSE;

#ifdef ILAND_HAVE_WL_WINSYS
    if (sd->kind == EGL_SHIM_DISPLAY_WAYLAND && !sd->wl_winsys) {
        /* Bind linux-dmabuf now so a compositor without it fails here, where
         * clients check, instead of at first swap. */
        sd->wl_winsys = iland_wl_ops->winsys_create(sd->wl_display);
        if (!sd->wl_winsys)
            return EGL_FALSE;
    }
#endif

    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglTerminate(dpy);
    EGLBoolean ret = real_eglTerminate(sd->angle_display);
    if (g_pixels) { free(g_pixels); g_pixels = NULL; g_pixels_sz = 0; }
#ifdef ILAND_HAVE_WL_WINSYS
    if (sd->wl_winsys) iland_wl_winsys_destroy(sd->wl_winsys);
#endif
    free(sd);
    return ret;
}

EGLint eglGetError(void)
{
    if (!real_eglGetError) return EGL_SUCCESS;
    return real_eglGetError();
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    /*
     * Client extension string (EGL 1.5 / EGL_EXT_client_extensions). Clients
     * probe this before they have a display to decide whether they may call
     * eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, ...) — weston's
     * weston_platform_get_egl_display does exactly that, and without an answer
     * it falls back to eglGetDisplay and loses the platform distinction.
     */
    if (dpy == EGL_NO_DISPLAY) {
#ifdef ILAND_HAVE_WL_WINSYS
        if (name == EGL_EXTENSIONS && iland_wl_ops)
            return "EGL_EXT_client_extensions EGL_EXT_platform_base "
                   "EGL_KHR_platform_wayland EGL_EXT_platform_wayland";
#endif
        return NULL;
    }
    if (!real_eglQueryString) return NULL;
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglQueryString(dpy, name);
    return real_eglQueryString(sd->angle_display, name);
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                          EGLint config_size, EGLint *num_config)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglGetConfigs(dpy, configs, config_size, num_config);
    return real_eglGetConfigs(sd->angle_display, configs, config_size, num_config);
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                            EGLConfig *configs, EGLint config_size,
                            EGLint *num_config)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglChooseConfig(dpy, attrib_list, configs,
                                          config_size, num_config);

    int n_attribs = 0;
    if (attrib_list) {
        const EGLint *p = attrib_list;
        while (p[0] != EGL_NONE) { n_attribs += 2; p += 2; }
        n_attribs += 1;
    }

    /* Stack buffer for typical attrib lists (up to 32 pairs) */
    EGLint stack_buf[65];
    EGLint *mod_attribs;
    int use_heap = ((n_attribs + 1) > (int)(sizeof(stack_buf) / sizeof(stack_buf[0])));
    if (use_heap) {
        mod_attribs = malloc((n_attribs + 1) * sizeof(EGLint));
        if (!mod_attribs) return EGL_FALSE;
    } else {
        mod_attribs = stack_buf;
    }

    if (attrib_list) {
        memcpy(mod_attribs, attrib_list, (n_attribs + 1) * sizeof(EGLint));
        for (int i = 0; mod_attribs[i] != EGL_NONE; i += 2) {
            if (mod_attribs[i] == EGL_SURFACE_TYPE) {
                mod_attribs[i + 1] = EGL_PBUFFER_BIT;
                break;
            }
        }
    }

    EGLBoolean ret = real_eglChooseConfig(sd->angle_display, mod_attribs,
                                           configs, config_size, num_config);
    if (use_heap) free(mod_attribs);
    return ret;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                               EGLint attribute, EGLint *value)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglGetConfigAttrib(dpy, config, attribute, value);

    if (attribute == EGL_SURFACE_TYPE) {
        if (!real_eglGetConfigAttrib(sd->angle_display, config,
                                      attribute, value))
            return EGL_FALSE;
        if (*value & EGL_PBUFFER_BIT)
            *value |= EGL_WINDOW_BIT;
        return EGL_TRUE;
    }

    if (attribute == EGL_NATIVE_VISUAL_ID) {
        static const EGLint rgba_attrs[] = {
            EGL_ALPHA_SIZE, EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE
        };
        EGLint rgba[4];
        for (int i = 0; i < 4; i++) {
            if (!real_eglGetConfigAttrib(sd->angle_display, config,
                                          rgba_attrs[i], &rgba[i]))
                return EGL_FALSE;
        }
        if (rgba[1] == 8 && rgba[2] == 8 && rgba[3] == 8) {
            *value = rgba[0] == 8 ? 0x34325241  /* AR24 → ARGB8888 */
                                  : 0x34325258; /* XR24 → XRGB8888 */
            return EGL_TRUE;
        }
    }

    return real_eglGetConfigAttrib(sd->angle_display, config, attribute, value);
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                             EGLContext share_context,
                             const EGLint *attrib_list)
{
    WWN_REQUIRE_ANGLE(EGL_NO_CONTEXT);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglCreateContext(dpy, config, share_context, attrib_list);
    return real_eglCreateContext(sd->angle_display, config, share_context, attrib_list);
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglDestroyContext(dpy, ctx);
    return real_eglDestroyContext(sd->angle_display, ctx);
}

/* Zero-copy: get/create the ANGLE IOSurface-client-buffer pbuffer for buffer
 * slot `idx`. ANGLE renders the default framebuffer straight into the
 * IOSurface-backed Metal texture — no glReadPixels, no CPU channel swap.
 * Slots are gbm bos on the KMS path and swapchain buffers on Wayland. */
static EGLSurface zc_pbuffer_for_iosurface(EGLShimDisplay *sd,
                                           EGLShimSurface *ss,
                                           int idx, IOSurfaceRef io)
{
    if (idx < 0 || idx >= (int)(sizeof(ss->iosurf_pbuffers) /
                                sizeof(ss->iosurf_pbuffers[0])))
        return EGL_NO_SURFACE;
    if (ss->iosurf_pbuffers[idx]) return ss->iosurf_pbuffers[idx];

    if (!io || !real_eglCreatePbufferFromClientBuffer) return EGL_NO_SURFACE;

    const EGLint attribs[] = {
        EGL_WIDTH,                          (EGLint)ss->width,
        EGL_HEIGHT,                         (EGLint)ss->height,
        EGL_IOSURFACE_PLANE_ANGLE,          0,
        EGL_TEXTURE_TARGET,                 EGL_TEXTURE_RECTANGLE_ANGLE,
        EGL_TEXTURE_INTERNAL_FORMAT_ANGLE,  GL_BGRA_EXT,
        EGL_TEXTURE_FORMAT,                 EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TYPE_ANGLE,             GL_UNSIGNED_BYTE,
        EGL_NONE
    };

    /* GL renders bottom-up into these, so the IOSurface is upside down relative
     * to how anything downstream samples it. EGL_ANGLE_surface_orientation
     * would let ANGLE invert while rendering, but the Metal backend rejects it
     * on IOSurface pbuffers, so the flip is handled where the buffer is
     * consumed: the KMS presenter's blit shader, and — for Wayland — the
     * dmabuf Y_INVERT flag the winsys sets on the wl_buffer. */
    EGLSurface s = real_eglCreatePbufferFromClientBuffer(
        sd->angle_display, EGL_IOSURFACE_ANGLE,
        (EGLClientBuffer)io, ss->config, attribs);
    ss->iosurf_pbuffers[idx] = s;
    return s;
}

static EGLSurface zc_pbuffer_for_bo(EGLShimDisplay *sd, EGLShimSurface *ss,
                                    int idx, struct gbm_bo *bo)
{
    return zc_pbuffer_for_iosurface(sd, ss, idx, gbm_bo_get_iosurface(bo));
}

#ifdef ILAND_HAVE_WL_WINSYS
/* GLES3 enums; GLES2/gl2.h predates them. */
#define WWN_GL_READ_FRAMEBUFFER          0x8CA8
#define WWN_GL_DRAW_FRAMEBUFFER          0x8CA9
#define WWN_GL_READ_FRAMEBUFFER_BINDING  0x8CAA
#define WWN_GL_DRAW_FRAMEBUFFER_BINDING  0x8CA6
#define WWN_GL_COLOR_BUFFER_BIT          0x4000
#define WWN_GL_NEAREST                   0x2600
#define WWN_GL_COLOR_ATTACHMENT0         0x8CE0
#define WWN_GL_FRAMEBUFFER_COMPLETE      0x8CD5
/* ANGLE's rectangle texture target, the one an IOSurface pbuffer binds to. */
#define WWN_GL_TEXTURE_RECTANGLE            0x84F5
#define WWN_GL_TEXTURE_BINDING_RECTANGLE    0x84F6

/* An ANGLE pbuffer wrapping an IOSurface has the IOSurface as its colour
 * attachment and nothing else: no depth, no stencil, whatever the config asked
 * for. glGetFramebufferAttachmentParameteriv still answers from the config, so
 * a client sees a depth attachment, enables GL_DEPTH_TEST, and gets a scene
 * where far triangles paint over near ones — which reads as broken face
 * culling, not as a missing buffer.
 *
 * So the client renders into an ordinary pbuffer of the same config, which does
 * get its depth and stencil, and each swap blits the colour into the slot's
 * IOSurface. One offscreen surface for the whole swapchain (it is consumed every
 * frame) and one full-surface GPU blit per frame; no CPU copy, so the dmabuf
 * export stays zero-copy. The blit is straight, not flipped: both sides are GL
 * surfaces, so the IOSurface is bottom-up exactly as before.
 *
 * NULL if GLES3 blitting is unavailable, in which case callers render into the
 * IOSurface directly and depth stays broken. */
static EGLSurface zc_render_pbuffer(EGLShimDisplay *sd, EGLShimSurface *ss)
{
    if (ss->render_pbuffer) return ss->render_pbuffer;
    const char *blit = getenv("ILAND_EGL_DEPTH_BLIT");
    if (blit && blit[0] == '0') {
        fprintf(stderr, "iland: depth blit disabled, rendering into the "
                        "IOSurface directly (GL_DEPTH_TEST will not work)\n");
        return NULL;
    }
    if (!g_glBlitFramebuffer || !g_glBindFramebuffer || !g_glGetIntegerv) {
        fprintf(stderr, "iland: no GLES3 blit, rendering into the IOSurface "
                        "directly (GL_DEPTH_TEST will not work)\n");
        return NULL;
    }
    if (!real_eglCreatePbufferSurface) return NULL;

    const EGLint attribs[] = {
        EGL_WIDTH,  (EGLint)ss->width,
        EGL_HEIGHT, (EGLint)ss->height,
        EGL_NONE
    };
    EGLSurface pb = real_eglCreatePbufferSurface(sd->angle_display,
                                                ss->config, attribs);
    if (pb == EGL_NO_SURFACE) {
        fprintf(stderr, "iland: render pbuffer %ux%u failed, rendering into the "
                        "IOSurface directly (GL_DEPTH_TEST will not work)\n",
                ss->width, ss->height);
        return NULL;
    }
    EGLint depth = -1;
    if (real_eglGetConfigAttrib)
        real_eglGetConfigAttrib(sd->angle_display, ss->config,
                                EGL_DEPTH_SIZE, &depth);
    fprintf(stderr, "iland: rendering into a %ux%u pbuffer, depth %d bits, "
                    "blitting to the presented IOSurface\n",
            ss->width, ss->height, depth);
    ss->render_pbuffer = pb;
    return pb;
}

/* Sample the middle of a presented IOSurface after the GPU work has landed, so
 * a blank window can be attributed: content here means the compositor's import
 * is at fault, all-zero means this side never wrote the buffer. Costs a CPU map
 * of one page, so it runs for the first few frames of ILAND_EGL_DEBUG=1 only. */
static void zc_probe_iosurface(IOSurfaceRef io, const char *what)
{
    static int probes_left = -1;
    if (probes_left < 0) {
        const char *e = getenv("ILAND_EGL_DEBUG");
        probes_left = (e && e[0] == '1') ? 3 : 0;
    }
    if (probes_left == 0 || !io) return;
    probes_left--;

    if (IOSurfaceLock(io, kIOSurfaceLockReadOnly, NULL) != kIOReturnSuccess) {
        fprintf(stderr, "iland: %s IOSurface lock failed\n", what);
        return;
    }
    size_t w = IOSurfaceGetWidth(io), h = IOSurfaceGetHeight(io);
    size_t stride = IOSurfaceGetBytesPerRow(io);
    const uint8_t *base = IOSurfaceGetBaseAddress(io);
    uint32_t centre = 0, corner = 0;
    if (base && w && h) {
        memcpy(&centre, base + (h / 2) * stride + (w / 2) * 4, 4);
        memcpy(&corner, base + 4 * stride + 4 * 4, 4);
    }
    fprintf(stderr, "iland: %s %zux%zu centre=0x%08x near-corner=0x%08x\n",
            what, w, h, centre, corner);
    IOSurfaceUnlock(io, kIOSurfaceLockReadOnly, NULL);
}

static void zc_report_gl_error(const char *what)
{
    if (!g_glGetError) return;
    unsigned int err = g_glGetError();
    if (!err) return;
    static unsigned int reported = 0;
    if (reported == err) return;
    reported = err;
    fprintf(stderr, "iland: %s failed, GL error 0x%04x\n", what, err);
}

/* Copy the render pbuffer's colour into `dst_pb` (a slot's IOSurface). The
 * client's context stays current on the render pbuffer throughout: the
 * destination is reached as a texture, not as another surface's default
 * framebuffer. Binding the IOSurface pbuffer as the draw *surface* and blitting
 * default-to-default silently wrote nothing (no GL error, IOSurface all zero),
 * whereas eglBindTexImage + an FBO colour attachment is the usage
 * EGL_ANGLE_iosurface_client_buffer documents, and eglReleaseTexImage is what
 * publishes the writes to the IOSurface. */
static void zc_blit_to_slot(EGLShimDisplay *sd, EGLShimSurface *ss,
                            EGLSurface dst_pb)
{
    if (!ss->render_pbuffer || !dst_pb) return;
    if (!real_eglBindTexImage || !real_eglReleaseTexImage) return;
    if (!g_glGenTextures || !g_glBindTexture || !g_glGenFramebuffers ||
        !g_glFramebufferTexture2D)
        return;

    if (!ss->blit_tex) {
        g_glGenTextures(1, &ss->blit_tex);
        g_glGenFramebuffers(1, &ss->blit_fbo);
        if (!ss->blit_tex || !ss->blit_fbo) return;
    }

    int prev_draw = 0, prev_read = 0, prev_tex = 0;
    g_glGetIntegerv(WWN_GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);
    g_glGetIntegerv(WWN_GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    g_glGetIntegerv(WWN_GL_TEXTURE_BINDING_RECTANGLE, &prev_tex);

    g_glBindTexture(WWN_GL_TEXTURE_RECTANGLE, ss->blit_tex);
    if (!real_eglBindTexImage(sd->angle_display, dst_pb, EGL_BACK_BUFFER)) {
        g_glBindTexture(WWN_GL_TEXTURE_RECTANGLE, (unsigned int)prev_tex);
        static int warned = 0;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "iland: eglBindTexImage on the presented IOSurface "
                            "failed (0x%04x)\n",
                    real_eglGetError ? real_eglGetError() : 0);
        }
        return;
    }

    g_glBindFramebuffer(WWN_GL_DRAW_FRAMEBUFFER, ss->blit_fbo);
    g_glFramebufferTexture2D(WWN_GL_DRAW_FRAMEBUFFER, WWN_GL_COLOR_ATTACHMENT0,
                             WWN_GL_TEXTURE_RECTANGLE, ss->blit_tex, 0);
    g_glBindFramebuffer(WWN_GL_READ_FRAMEBUFFER, 0);

    if (g_glCheckFramebufferStatus) {
        unsigned int status = g_glCheckFramebufferStatus(WWN_GL_DRAW_FRAMEBUFFER);
        if (status != WWN_GL_FRAMEBUFFER_COMPLETE) {
            static unsigned int warned = 0;
            if (warned != status) {
                warned = status;
                fprintf(stderr, "iland: IOSurface blit target incomplete "
                                "(0x%04x)\n", status);
            }
        }
    }

    g_glBlitFramebuffer(0, 0, (int)ss->width, (int)ss->height,
                        0, 0, (int)ss->width, (int)ss->height,
                        WWN_GL_COLOR_BUFFER_BIT, WWN_GL_NEAREST);
    zc_report_gl_error("blit into the presented IOSurface");

    /* Detach before release so the texture does not outlive the binding. */
    g_glFramebufferTexture2D(WWN_GL_DRAW_FRAMEBUFFER, WWN_GL_COLOR_ATTACHMENT0,
                             WWN_GL_TEXTURE_RECTANGLE, 0, 0);
    g_glBindFramebuffer(WWN_GL_DRAW_FRAMEBUFFER, (unsigned int)prev_draw);
    g_glBindFramebuffer(WWN_GL_READ_FRAMEBUFFER, (unsigned int)prev_read);

    real_eglReleaseTexImage(sd->angle_display, dst_pb, EGL_BACK_BUFFER);
    g_glBindTexture(WWN_GL_TEXTURE_RECTANGLE, (unsigned int)prev_tex);
}

/* Only safe with the client's context still current, which holds for the
 * eglDestroySurface path a client runs on its own thread. */
static void zc_drop_blit_objects(EGLShimSurface *ss)
{
    if (ss->blit_fbo && g_glDeleteFramebuffers)
        g_glDeleteFramebuffers(1, &ss->blit_fbo);
    if (ss->blit_tex && g_glDeleteTextures)
        g_glDeleteTextures(1, &ss->blit_tex);
    ss->blit_fbo = 0;
    ss->blit_tex = 0;
}

static void zc_drop_pbuffers(EGLShimDisplay *sd, EGLShimSurface *ss)
{
    for (size_t i = 0; i < sizeof(ss->iosurf_pbuffers) /
                           sizeof(ss->iosurf_pbuffers[0]); i++) {
        if (ss->iosurf_pbuffers[i]) {
            real_eglDestroySurface(sd->angle_display, ss->iosurf_pbuffers[i]);
            ss->iosurf_pbuffers[i] = NULL;
        }
    }
    /* Sized to the old surface, so it goes too; EGL defers the destroy while it
     * is still current, and wl_bind_slot rebinds the replacement. */
    if (ss->render_pbuffer) {
        real_eglDestroySurface(sd->angle_display, ss->render_pbuffer);
        ss->render_pbuffer = NULL;
    }
}

/* Make swapchain slot `slot` the one the next swap presents. With the render
 * pbuffer in play the drawing target never changes, so this is bookkeeping;
 * without it the slot's IOSurface *is* the default framebuffer and the context
 * has to be re-made current, as when advancing a gbm bo. */
static EGLBoolean wl_bind_slot(EGLShimDisplay *sd, EGLShimSurface *ss, int slot)
{
    IOSurfaceRef io = iland_wl_swapchain_iosurface(ss->wl_swapchain, slot);
    EGLSurface pb = zc_pbuffer_for_iosurface(sd, ss, slot, io);
    if (!pb) return EGL_FALSE;

    ss->wl_slot = slot;

    EGLSurface prev_render = ss->render_pbuffer;
    EGLSurface render = zc_render_pbuffer(sd, ss);
    if (render) {
        ss->angle_surface = render;
        /* A resize replaced it, so the context is still drawing into the old
         * one; first bind needs no help, the client makes current itself. */
        if (render != prev_render) {
            EGLContext cur = real_eglGetCurrentContext
                                 ? real_eglGetCurrentContext()
                                 : EGL_NO_CONTEXT;
            if (cur != EGL_NO_CONTEXT)
                real_eglMakeCurrent(sd->angle_display, render, render, cur);
        }
        return EGL_TRUE;
    }

    ss->angle_surface = pb;

    EGLContext cur = real_eglGetCurrentContext ? real_eglGetCurrentContext()
                                               : EGL_NO_CONTEXT;
    if (cur != EGL_NO_CONTEXT)
        real_eglMakeCurrent(sd->angle_display, pb, pb, cur);
    return EGL_TRUE;
}
#endif

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                   EGLNativeWindowType win,
                                   const EGLint *attrib_list)
{
    WWN_REQUIRE_ANGLE(EGL_NO_SURFACE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglCreateWindowSurface(dpy, config, win, attrib_list);

#ifdef ILAND_HAVE_WL_WINSYS
    if (sd->kind == EGL_SHIM_DISPLAY_WAYLAND) {
        struct wl_egl_window *wlwin = (struct wl_egl_window *)win;
        if (!sd->wl_winsys || !iland_wl_egl_window_is_valid(wlwin))
            return EGL_NO_SURFACE;

        EGLShimSurface *ws = calloc(1, sizeof(*ws));
        if (!ws) return EGL_NO_SURFACE;

        ws->wayland = 1;
        ws->zerocopy = 1;
        ws->config = config;
        ws->wl_window = wlwin;
        ws->wl_swapchain = iland_wl_swapchain_create(sd->wl_winsys, wlwin);
        if (!ws->wl_swapchain) {
            free(ws);
            return EGL_NO_SURFACE;
        }
        iland_wl_swapchain_get_size(ws->wl_swapchain, &ws->width, &ws->height);

        /* Bind slot 0 without a current context; eglMakeCurrent picks it up. */
        IOSurfaceRef io = iland_wl_swapchain_iosurface(ws->wl_swapchain, 0);
        EGLSurface pb = zc_pbuffer_for_iosurface(sd, ws, 0, io);
        if (!pb) {
            iland_wl_swapchain_destroy(ws->wl_swapchain);
            free(ws);
            return EGL_NO_SURFACE;
        }
        ws->wl_slot = 0;
        /* Draw into a depth-capable pbuffer from the first frame, not just from
         * the first swap; the IOSurface pbuffer above is only a blit target. */
        EGLSurface render = zc_render_pbuffer(sd, ws);
        ws->angle_surface = render ? render : pb;
        return (EGLSurface)ws;
    }
#endif

    struct gbm_surface *gs = (struct gbm_surface *)win;
    if (!gs) return EGL_NO_SURFACE;

    EGLShimSurface *ss = calloc(1, sizeof(*ss));
    if (!ss) return EGL_NO_SURFACE;

    ss->gbm_surface = gs;
    ss->width  = gbm_bo_get_width(gs->bos[0]);
    ss->height = gbm_bo_get_height(gs->bos[0]);
    ss->config = config;

    if (zerocopy_enabled()) {
        /* Bind the current write bo's IOSurface as the ANGLE render target. */
        ss->zerocopy = 1;
        struct gbm_bo *wbo = gbm_surface_get_write_bo(gs);
        EGLSurface pb = zc_pbuffer_for_bo(sd, ss, gs->write_idx, wbo);
        if (pb) {
            /* Draw into a depth-capable pbuffer and blit per swap where the
             * driver allows; the bo's IOSurface alone has no depth. */
            EGLSurface render = zc_render_pbuffer(sd, ss);
            ss->angle_surface = render ? render : pb;
            return (EGLSurface)ss;
        }
        /* Fall back to the copy path if ANGLE can't bind the IOSurface. */
        ss->zerocopy = 0;
    }

    EGLint pb_attribs[] = {
        EGL_WIDTH,  (EGLint)ss->width,
        EGL_HEIGHT, (EGLint)ss->height,
        EGL_NONE
    };

    ss->angle_surface = real_eglCreatePbufferSurface(sd->angle_display,
                                                       config, pb_attribs);
    if (!ss->angle_surface) {
        free(ss);
        return EGL_NO_SURFACE;
    }

    return (EGLSurface)ss;
}

/*
 * eglQueryString(EGL_NO_DISPLAY) above advertises EGL_EXT_platform_base, so a
 * client that follows the extension — weston's
 * weston_platform_create_egl_surface does — creates its window surface through
 * this entry point rather than eglCreateWindowSurface. Leaving it unimplemented
 * meant ANGLE answered for it and was handed a wl_egl_window (or a gbm_surface)
 * it knows nothing about. Native-window handling is identical either way, so
 * both spellings forward to the shim's own eglCreateWindowSurface.
 */
EGLSurface eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                            void *native_window,
                                            const EGLint *attrib_list)
{
    return eglCreateWindowSurface(dpy, config,
                                  (EGLNativeWindowType)native_window,
                                  attrib_list);
}

EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                          void *native_window,
                                          const EGLAttrib *attrib_list)
{
    /* EGL 1.5 widens the attribute values to EGLAttrib. Narrow them back for
     * the EXT-shaped path; every attribute either side of this shim is a small
     * enum or pixel count, and a list long enough to overflow this is a client
     * bug rather than something to silently truncate. */
    EGLint narrowed[33];
    const EGLint *attrs = NULL;
    if (attrib_list) {
        size_t n = 0;
        while (attrib_list[n] != EGL_NONE &&
               n + 2 < sizeof(narrowed) / sizeof(narrowed[0])) {
            narrowed[n] = (EGLint)attrib_list[n];
            narrowed[n + 1] = (EGLint)attrib_list[n + 1];
            n += 2;
        }
        narrowed[n] = EGL_NONE;
        attrs = narrowed;
    }
    return eglCreateWindowSurface(dpy, config,
                                  (EGLNativeWindowType)native_window, attrs);
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglDestroySurface(dpy, surface);

    EGLShimSurface *ss = unwrap_surface(surface);
    if (!ss) return real_eglDestroySurface(dpy, surface);

#ifdef ILAND_HAVE_WL_WINSYS
    if (ss->wayland) {
        zc_drop_blit_objects(ss);
        zc_drop_pbuffers(sd, ss);
        iland_wl_swapchain_destroy(ss->wl_swapchain);
        free(ss);
        return EGL_TRUE;
    }
#endif

    if (ss->zerocopy) {
        zc_drop_blit_objects(ss);
        zc_drop_pbuffers(sd, ss);
    } else {
        real_eglDestroySurface(sd->angle_display, ss->angle_surface);
    }
    free(ss);
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                           EGLSurface read, EGLContext ctx)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglMakeCurrent(dpy, draw, read, ctx);

    EGLShimSurface *sdraw = unwrap_surface(draw);
    EGLShimSurface *sread = unwrap_surface(read);

    EGLSurface adraw = sdraw ? sdraw->angle_surface : draw;
    EGLSurface aread = sread ? sread->angle_surface : read;

    return real_eglMakeCurrent(sd->angle_display, adraw, aread, ctx);
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglSwapBuffers(dpy, surface);

    EGLShimSurface *ss = unwrap_surface(surface);
    if (!ss) return real_eglSwapBuffers(dpy, surface);

#ifdef ILAND_HAVE_WL_WINSYS
    if (ss->wayland) {
        /* The frame is in the render pbuffer (or, without one, already in the
         * slot's IOSurface). Land the GPU work, hand the buffer to the
         * compositor, then draw into a released slot. */
        if (ss->render_pbuffer)
            zc_blit_to_slot(sd, ss,
                            ss->iosurf_pbuffers[ss->wl_slot]);

        zc_flush_gpu(sd->angle_display);

        zc_probe_iosurface(iland_wl_swapchain_iosurface(ss->wl_swapchain,
                                                        ss->wl_slot),
                           "posting");

        iland_wl_swapchain_post(ss->wl_swapchain, ss->wl_slot);

        if (iland_wl_swapchain_check_resize(ss->wl_swapchain)) {
            /* New IOSurfaces: the cached pbuffers point at freed surfaces. */
            zc_drop_pbuffers(sd, ss);
            iland_wl_swapchain_get_size(ss->wl_swapchain,
                                        &ss->width, &ss->height);
        }

        int slot = iland_wl_swapchain_acquire(ss->wl_swapchain);
        if (slot < 0) return EGL_FALSE;
        return wl_bind_slot(sd, ss, slot);
    }
#endif

    struct gbm_surface *gs = ss->gbm_surface;

    if (ss->zerocopy) {
        /* Content is in the render pbuffer, or — without one — already in the
         * current write bo's IOSurface. Make sure the GPU work has landed,
         * publish the bo, and bind the next write bo for the next frame. */
        if (ss->render_pbuffer)
            zc_blit_to_slot(sd, ss, ss->iosurf_pbuffers[gs->write_idx]);

        zc_flush_gpu(sd->angle_display);

        gbm_surface_advance_write(gs);

        struct gbm_bo *next = gbm_surface_get_write_bo(gs);
        EGLSurface next_pb = zc_pbuffer_for_bo(sd, ss, gs->write_idx, next);
        if (next_pb && !ss->render_pbuffer) {
            ss->angle_surface = next_pb;
            EGLContext cur = real_eglGetCurrentContext
                                 ? real_eglGetCurrentContext()
                                 : EGL_NO_CONTEXT;
            real_eglMakeCurrent(sd->angle_display, next_pb, next_pb, cur);
        }
        return EGL_TRUE;
    }

    struct gbm_bo *bo = gbm_surface_get_write_bo(gs);
    IOSurfaceRef iosurf = gbm_bo_get_iosurface(bo);

    uint32_t w = ss->width;
    uint32_t h = ss->height;
    size_t total = (size_t)w * h * 4;

    if (!g_glReadPixels) return real_eglSwapBuffers(sd->angle_display, ss->angle_surface);

    /* Reuse thread-local buffer to avoid malloc/free per frame */
    if (g_pixels_sz < total) {
        void *p = realloc(g_pixels, total);
        if (!p) return real_eglSwapBuffers(sd->angle_display, ss->angle_surface);
        g_pixels = p;
        g_pixels_sz = total;
    }

    /* Read pixels BEFORE swap — back buffer content is undefined after */
    g_glReadPixels(0, 0, (int)w, (int)h, 0x1908, 0x1401, g_pixels);

    EGLBoolean ret = real_eglSwapBuffers(sd->angle_display, ss->angle_surface);
    if (!ret) return ret;

    IOSurfaceLock(iosurf, 0, NULL);
    uint8_t *dst8 = (uint8_t *)IOSurfaceGetBaseAddress(iosurf);
    size_t dst_pitch_bytes = IOSurfaceGetBytesPerRow(iosurf);
    const uint8_t *src8 = (const uint8_t *)g_pixels;

    /* Copy rows with vertical flip (source is bottom-up from GL) */
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *s = src8 + (size_t)(h - 1 - y) * w * 4;
        uint8_t *d = dst8 + (size_t)y * dst_pitch_bytes;
        memcpy(d, s, (size_t)w * 4);
    }

    /* Channel swap RGBA→BGRA using Accelerate (SIMD on Apple Silicon) */
    vImage_Buffer buf = {
        .data     = dst8,
        .width    = w,
        .height   = h,
        .rowBytes = dst_pitch_bytes,
    };
    vImagePermuteChannels_ARGB8888(&buf, &buf, kRGBAToBGRAMap, 0);

    IOSurfaceUnlock(iosurf, 0, NULL);

    gbm_surface_advance_write(gs);

    return EGL_TRUE;
}

EGLBoolean eglBindAPI(EGLenum api)
{
    if (!real_eglBindAPI) return EGL_FALSE;
    return real_eglBindAPI(api);
}

EGLBoolean eglWaitGL(void)
{
    if (!real_eglWaitGL) return EGL_FALSE;
    return real_eglWaitGL();
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglSwapInterval(dpy, interval);
    return real_eglSwapInterval(sd->angle_display, interval);
}

/* Entry points this shim replaces. ANGLE's eglGetProcAddress answers for these
 * too, with its own versions, which know nothing about gbm surfaces or
 * EGL_PLATFORM_WAYLAND — a client that resolves them dynamically would escape
 * the shim and get NULL displays. weston-simple-egl does exactly that, via
 * shared/platform.h's eglGetProcAddress("eglGetPlatformDisplayEXT"). */
static const struct {
    const char *name;
    void *fn;
} kShimEntryPoints[] = {
    { "eglGetDisplay",            (void *)eglGetDisplay },
    { "eglGetPlatformDisplay",    (void *)eglGetPlatformDisplay },
    { "eglGetPlatformDisplayEXT", (void *)eglGetPlatformDisplayEXT },
    { "eglInitialize",            (void *)eglInitialize },
    { "eglTerminate",             (void *)eglTerminate },
    { "eglQueryString",           (void *)eglQueryString },
    { "eglChooseConfig",          (void *)eglChooseConfig },
    { "eglGetConfigAttrib",       (void *)eglGetConfigAttrib },
    { "eglCreateWindowSurface",   (void *)eglCreateWindowSurface },
    { "eglCreatePlatformWindowSurface",
      (void *)eglCreatePlatformWindowSurface },
    { "eglCreatePlatformWindowSurfaceEXT",
      (void *)eglCreatePlatformWindowSurfaceEXT },
    { "eglDestroySurface",        (void *)eglDestroySurface },
    { "eglMakeCurrent",           (void *)eglMakeCurrent },
    { "eglSwapBuffers",           (void *)eglSwapBuffers },
    { "eglSwapInterval",          (void *)eglSwapInterval },
    { "eglGetError",              (void *)eglGetError },
    { "eglGetProcAddress",        (void *)eglGetProcAddress },
};

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    if (!procname || load_angle() < 0 || !real_eglGetProcAddress)
        return NULL;

    for (size_t i = 0; i < sizeof(kShimEntryPoints) / sizeof(kShimEntryPoints[0]);
         i++) {
        if (strcmp(procname, kShimEntryPoints[i].name) == 0)
            return (__eglMustCastToProperFunctionPointerType)
                kShimEntryPoints[i].fn;
    }

    return real_eglGetProcAddress(procname);
}
