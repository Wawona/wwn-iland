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
#define eglQueryContext                angle_eglQueryContext
#define eglQuerySurface                angle_eglQuerySurface
#define eglReleaseThread               angle_eglReleaseThread
#endif

#include <egl_shim.h>
#include <gbm_priv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <IOSurface/IOSurfaceRef.h>
#include <Accelerate/Accelerate.h>

/* Wayland-EGL winsys: IOSurface (Apple) / AHardwareBuffer (Android) posted
 * via linux-dmabuf. Depth-blit (zc_*) helpers are compiled unconditionally so
 * the GBM zerocopy path still works under ILAND_NO_WL_WINSYS (Mode B). */
#if (defined(__APPLE__) || defined(__ANDROID__)) && !defined(ILAND_NO_WL_WINSYS)
#define ILAND_HAVE_WL_WINSYS 1
#include "iland_wayland_egl.h"
#include "iland_wl_winsys.h"
#include "iland_wl_ops.h"

/* NULL unless libiland_wayland_egl.a is linked, which is what makes this a
 * Wayland-capable build; see iland_wl_ops.h. The pointer itself lives in
 * iland_wl_ops.c so cores that skip egl.c still export it. */
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
#undef eglQueryContext
#undef eglQuerySurface
#undef eglReleaseThread
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
ANGLE_FN(eglQueryContext);
ANGLE_FN(eglQuerySurface);
ANGLE_FN(eglReleaseThread);
/* typeof(&angle_eglBindTexImage) would require that symbol at link time on
 * some toolchains; visionOS ANGLE only exports the unprefixed names. */
static EGLBoolean (*real_eglBindTexImage)(EGLDisplay, EGLSurface, EGLint) = NULL;
static EGLBoolean (*real_eglReleaseTexImage)(EGLDisplay, EGLSurface, EGLint) =
    NULL;

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
static void (*g_glEnable)(unsigned int) = NULL;
static void (*g_glDisable)(unsigned int) = NULL;
static unsigned char (*g_glIsEnabled)(unsigned int) = NULL;
/* Android CPU-readback fallback (#140): give a client's EGLImage texture real
 * RGBA storage when the AHB native-buffer import is unavailable. */
static void (*g_glTexImage2D)(unsigned int, int, int, int, int, int,
                              unsigned int, unsigned int, const void *) = NULL;

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
#ifndef EGL_BIND_TO_TEXTURE_TARGET_ANGLE
#define EGL_BIND_TO_TEXTURE_TARGET_ANGLE    0x348D
#endif
#ifndef EGL_TEXTURE_2D
#define EGL_TEXTURE_2D                      0x305F
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT                         0x80E1
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE                    0x1401
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D                       0x0DE1
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D               0x8069
#endif

/* EGL_EXT_image_dma_buf_import — enums are not in stock EGL/egl.h. iland
 * provides this extension over ANGLE by resolving the "dma_buf" back to the
 * IOSurface the modifier encodes (see eglCreateImageKHR below). */
#ifndef EGL_KHR_image
typedef void *EGLImageKHR;
#define EGL_NO_IMAGE_KHR                    ((EGLImageKHR)0)
#endif
#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT               0x3270
#endif
#ifndef EGL_LINUX_DRM_FOURCC_EXT
#define EGL_LINUX_DRM_FOURCC_EXT            0x3271
#endif
#ifndef EGL_DMA_BUF_PLANE0_FD_EXT
#define EGL_DMA_BUF_PLANE0_FD_EXT           0x3272
#endif
#ifndef EGL_DMA_BUF_PLANE0_OFFSET_EXT
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT       0x3273
#endif
#ifndef EGL_DMA_BUF_PLANE0_PITCH_EXT
#define EGL_DMA_BUF_PLANE0_PITCH_EXT        0x3274
#endif
#ifndef EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
#define EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT  0x3443
#endif
#ifndef EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
#define EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT  0x3444
#endif

/* Same fourccs and IOSurface-in-modifier convention as Wawona's
 * zwp_linux_dmabuf_v1 bind (linux_dmabuf.rs). High bit set, IOSurface id in
 * the low 63 bits. LINEAR is not advertised. */
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888                 0x34325241u
#endif
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888                 0x34325258u
#endif
#define ILAND_IOSURFACE_MODIFIER            0x8000000000000000ULL

static const EGLint kIlandDmabufFormats[] = {
    (EGLint)DRM_FORMAT_ARGB8888,
    (EGLint)DRM_FORMAT_XRGB8888,
};

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
#if !defined(__ANDROID__)
static const uint8_t kRGBAToBGRAMap[4] = { 2, 1, 0, 3 };
#endif

/* RGBA→BGRA after glReadPixels. Accelerate on Apple; scalar on Android
 * (no Accelerate.framework in the NDK). */
static void swap_rgba_to_bgra(uint8_t *dst8, uint32_t w, uint32_t h,
                              size_t dst_pitch_bytes)
{
#if defined(__ANDROID__)
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = dst8 + (size_t)y * dst_pitch_bytes;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *px = row + (size_t)x * 4;
            uint8_t r = px[0], b = px[2];
            px[0] = b;
            px[2] = r;
        }
    }
#else
    vImage_Buffer buf = {
        .data     = dst8,
        .width    = w,
        .height   = h,
        .rowBytes = dst_pitch_bytes,
    };
    vImagePermuteChannels_ARGB8888(&buf, &buf, kRGBAToBGRAMap, 0);
#endif
}

static int graphics_policy_allows_angle(void)
{
    const char *disabled = getenv("WWN_DISABLE_EGL");
    const char *driver = getenv("WWN_OPENGL_DRIVER");
    if (disabled && disabled[0] == '1') return 0;
    if (!driver || !driver[0]) return 1;
    if (strcmp(driver, "none") == 0) return 0;
#if defined(__ANDROID__)
    /* iland's EGL shim + in-process GL clients (kmscube, opengl-cube,
     * simple-egl) resolve gl* against bundled libGLESv2_angle. Vendor
     * META-EGL ("OpenGL Driver=system") cannot share that context and has
     * no EGL_ANGLE_iosurface_client_buffer — always back the shim with
     * ANGLE on Android. */
    return strcmp(driver, "angle") == 0 || strcmp(driver, "system") == 0;
#else
    /* Apple / desktop GLES path is ANGLE only. */
    return strcmp(driver, "angle") == 0;
#endif
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
    LOAD(eglQueryContext);
    LOAD(eglQuerySurface);
    LOAD(eglReleaseThread);
    /* Do not take &angle_eglBindTexImage — visionOS static ANGLE still exports
     * these two without the angle_ prefix, so a direct reference fails link.
     * Resolve via ANGLE's own GetProcAddress instead. */
    real_eglBindTexImage = real_eglGetProcAddress
        ? (__typeof__(real_eglBindTexImage))real_eglGetProcAddress("eglBindTexImage")
        : NULL;
    real_eglReleaseTexImage = real_eglGetProcAddress
        ? (__typeof__(real_eglReleaseTexImage))
              real_eglGetProcAddress("eglReleaseTexImage")
        : NULL;

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
    g_glTexImage2D = glTexImage2D;
    g_glEnable = glEnable;
    g_glDisable = glDisable;
    g_glIsEnabled = glIsEnabled;
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
    /*
     * Prefer the framework-wrapped ANGLE binary: App Store packaging forbids
     * loose .dylib files inside Frameworks/ (TN2435) — ASC's validator
     * misreads them as pre-ABI Swift runtime dylibs and rejects the whole
     * ipa with rotating ITMS-90426/90429/90433. Device bundles ship ANGLE
     * only as libEGL.framework/libGLESv2.framework; the flat paths remain
     * as a fallback for simulator/dev bundles that still carry them.
     */
    static const char *candidates[] = {
        "@executable_path/Frameworks/libEGL.framework/libEGL",
        "@executable_path/Frameworks/libEGL.dylib",
        "libEGL.dylib",
        NULL,
    };
    for (size_t i = 0; candidates[i]; i++) {
        g_angle_handle = open_angle_library(candidates[i]);
        if (g_angle_handle) break;
    }
#elif defined(__ANDROID__)
    /*
     * Always map the shim onto bundled ANGLE — even when OpenGL Driver=system.
     * libwawona NEEDED libGLESv2_angle; pairing it with META-EGL yields
     * eglCreateWindowSurface / shader failures. Prefer an already-mapped
     * image (RTLD_NOLOAD) so we share one ANGLE with System.loadLibrary.
     */
    {
        static const char *angle_candidates[] = {
            "libEGL_angle.so",
            "libEGL.so",
            NULL,
        };
        for (size_t i = 0; angle_candidates[i]; i++) {
            g_angle_handle =
                dlopen(angle_candidates[i], RTLD_NOW | RTLD_NOLOAD);
            if (g_angle_handle) break;
        }
        if (!g_angle_handle) {
            for (size_t i = 0; angle_candidates[i]; i++) {
                g_angle_handle = open_angle_library(angle_candidates[i]);
                if (g_angle_handle) break;
            }
        }
    }
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
        /* Public libEGL.dylib in Wawona.app is the iland Wayland-EGL shim.
         * ANGLE is renamed libEGL_angle.dylib so this dlopen cannot recurse. */
        "@rpath/libEGL_angle.dylib",
        "@executable_path/../Frameworks/libEGL_angle.dylib",
        "@executable_path/Frameworks/libEGL_angle.dylib",
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
    LOAD(eglQueryContext);
    LOAD(eglQuerySurface);
    LOAD(eglReleaseThread);
    LOAD(eglBindTexImage);
    LOAD(eglReleaseTexImage);

    return 0;
}

static void load_gles2(void)
{
    if (g_glReadPixels) return;
#if TARGET_OS_IPHONE
    /* Framework-wrapped first — see load_angle(): loose Frameworks/*.dylib
     * are banned from App Store bundles (TN2435). */
    static const char *candidates[] = {
        "@executable_path/Frameworks/libGLESv2.framework/libGLESv2",
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
    void *h = NULL;
    {
        /* Match load_angle: always ANGLE GLESv2 on Android. */
        static const char *angle_candidates[] = {
            "libGLESv2_angle.so",
            "libGLESv2.so",
            NULL,
        };
        for (size_t i = 0; angle_candidates[i]; i++) {
            h = dlopen(angle_candidates[i], RTLD_NOW | RTLD_NOLOAD);
            if (h) break;
        }
        if (!h) {
            for (size_t i = 0; angle_candidates[i]; i++) {
                h = open_angle_library(angle_candidates[i]);
                if (h) break;
            }
        }
    }
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
    g_glTexImage2D         = dlsym(h, "glTexImage2D");
    g_glEnable             = dlsym(h, "glEnable");
    g_glDisable            = dlsym(h, "glDisable");
    g_glIsEnabled          = dlsym(h, "glIsEnabled");
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
    EGLShimDisplay *sd = (EGLShimDisplay *)dpy;
    if (!sd || sd->magic != ILAND_EGL_DISPLAY_MAGIC)
        return NULL;
    return sd;
}

#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#endif
#ifndef EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3489
#endif

/* ANGLE on Apple often returns NULL from eglGetDisplay(DEFAULT). Ask for
 * the Metal platform display instead. Never hand out a wrapper whose
 * angle_display is NULL: eglInitialize then reports EGL_BAD_DISPLAY. */
static EGLDisplay angle_default_display(void)
{
    EGLDisplay d = real_eglGetDisplay
        ? real_eglGetDisplay(EGL_DEFAULT_DISPLAY)
        : EGL_NO_DISPLAY;
    if (d)
        return d;
    if (!real_eglGetProcAddress)
        return EGL_NO_DISPLAY;
    typedef EGLDisplay (*get_platform_fn)(EGLenum, void *, const EGLAttrib *);
    get_platform_fn getplat = (get_platform_fn)real_eglGetProcAddress(
        "eglGetPlatformDisplay");
    if (!getplat)
        getplat = (get_platform_fn)real_eglGetProcAddress(
            "eglGetPlatformDisplayEXT");
    if (!getplat)
        return EGL_NO_DISPLAY;
    const EGLAttrib attribs[] = {
        (EGLAttrib)EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        (EGLAttrib)EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
        (EGLAttrib)EGL_NONE,
    };
    d = getplat(EGL_PLATFORM_ANGLE_ANGLE, (void *)EGL_DEFAULT_DISPLAY, attribs);
    if (!d) {
        fprintf(stderr,
                "iland: ANGLE DEFAULT and Metal platform displays are NULL\n");
    }
    return d;
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

/*
 * Every EGLShimDisplay wraps the one process-wide ANGLE EGL_DEFAULT_DISPLAY.
 * eglInitialize/eglTerminate are refcounted per the EGL spec, but ANGLE cannot
 * see that N in-process consumers (the host compositor + each bundled GL
 * client) share the same handle. Without this refcount, the first client to
 * eglTerminate() tore down ANGLE for everyone — a failed gbm-es2-demo init
 * runs its C++ destructor's eglTerminate and used to abort the whole Wawona
 * process. Count live initializations of the shared display here and only
 * dispatch the real eglTerminate when the last holder releases it.
 */
static int g_angle_shared_init_refs = 0;

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    if (load_angle() < 0) return EGL_NO_DISPLAY;

    EGLShimDisplay *dpy = calloc(1, sizeof(*dpy));
    if (!dpy) return EGL_NO_DISPLAY;

    dpy->magic = ILAND_EGL_DISPLAY_MAGIC;
    dpy->kind = EGL_SHIM_DISPLAY_GBM;
    dpy->gbm_device = (struct gbm_device *)display_id;
    dpy->angle_display = angle_default_display();
    if (!dpy->angle_display) {
        free(dpy);
        return EGL_NO_DISPLAY;
    }

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

    /* Apple EGLNativeDisplayType is int; never funnel a gbm_device* through
     * eglGetDisplay or the pointer is truncated. */
    if (platform == EGL_PLATFORM_GBM_KHR ||
        platform == EGL_PLATFORM_ANGLE_ANGLE) {
        EGLShimDisplay *dpy = calloc(1, sizeof(*dpy));
        if (!dpy) return EGL_NO_DISPLAY;
        dpy->magic = ILAND_EGL_DISPLAY_MAGIC;
        dpy->kind = EGL_SHIM_DISPLAY_GBM;
        dpy->gbm_device = (struct gbm_device *)native;
        dpy->angle_display = angle_default_display();
        if (!dpy->angle_display) {
            free(dpy);
            return EGL_NO_DISPLAY;
        }
        return (EGLDisplay)dpy;
    }

    if (platform != EGL_PLATFORM_WAYLAND_KHR)
        return eglGetDisplay((EGLNativeDisplayType)(uintptr_t)native);

#ifdef ILAND_HAVE_WL_WINSYS
    if (!native) return EGL_NO_DISPLAY;

    /* libiland_wayland_egl.a is not linked: this build is KMS-only. */
    if (!iland_wl_ops) return EGL_NO_DISPLAY;

    EGLShimDisplay *dpy = calloc(1, sizeof(*dpy));
    if (!dpy) return EGL_NO_DISPLAY;

    dpy->magic = ILAND_EGL_DISPLAY_MAGIC;
    dpy->kind = EGL_SHIM_DISPLAY_WAYLAND;
    dpy->wl_display = (struct wl_display *)native;
    dpy->angle_display = angle_default_display();
    if (!dpy->angle_display) {
        free(dpy);
        return EGL_NO_DISPLAY;
    }

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

    /* First successful init on this wrapper takes a reference on the shared
     * ANGLE display; repeated eglInitialize on the same wrapper is idempotent
     * (EGL spec) and must not double-count. */
    if (!sd->initialized) {
        sd->initialized = 1;
        g_angle_shared_init_refs++;
    }

#ifdef ILAND_HAVE_WL_WINSYS
    if (sd->kind == EGL_SHIM_DISPLAY_WAYLAND && !sd->wl_winsys) {
        /* Bind linux-dmabuf now so a compositor without it fails here, where
         * clients check, instead of at first swap. */
        sd->wl_winsys = iland_wl_ops->winsys_create(sd->wl_display);
        if (!sd->wl_winsys)
            return EGL_FALSE;
        fprintf(stderr,
                "iland: EGL_PLATFORM_WAYLAND (linux-dmabuf winsys). "
                "ANGLE is the GLES driver, not GBM/KMS.\n");
    }
#endif

    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglTerminate(dpy);

    /* Only the last holder of the shared ANGLE display may really terminate it.
     * A client (e.g. a failed gbm-es2-demo init) tearing down its own wrapper
     * must leave ANGLE alive for the host compositor and other in-process
     * clients. */
    EGLBoolean ret = EGL_TRUE;
    int last = 0;
    if (sd->initialized) {
        sd->initialized = 0;
        if (g_angle_shared_init_refs > 0 && --g_angle_shared_init_refs == 0)
            last = 1;
    }
    if (last) {
        ret = real_eglTerminate(sd->angle_display);
        /* g_pixels is the shared glReadPixels scratch buffer; freeing it while
         * another client is still presenting would corrupt that path, so only
         * release it on the final terminate. */
        if (g_pixels) { free(g_pixels); g_pixels = NULL; g_pixels_sz = 0; }
    }
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

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute,
                           EGLint *value)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd)
        return real_eglQueryContext(dpy, ctx, attribute, value);
    return real_eglQueryContext(sd->angle_display, ctx, attribute, value);
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                           EGLint *value)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    EGLShimSurface *ss = unwrap_surface(surface);
    if (!sd)
        return real_eglQuerySurface(dpy, surface, attribute, value);
    EGLSurface asurf = ss ? ss->angle_surface : surface;
    return real_eglQuerySurface(sd->angle_display, asurf, attribute, value);
}

EGLBoolean eglReleaseThread(void)
{
    WWN_REQUIRE_ANGLE(EGL_FALSE);
    if (!real_eglReleaseThread)
        return EGL_TRUE;
    return real_eglReleaseThread();
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
    /*
     * Client extension string (EGL 1.5 / EGL_EXT_client_extensions). Clients
     * probe this before they have a display to decide whether they may call
     * eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, ...) — weston's
     * weston_platform_get_egl_display does exactly that, and without an answer
     * it falls back to eglGetDisplay and loses the platform distinction.
     *
     * Mode B (ILAND_NO_WL_WINSYS) still needs client extensions so weston
     * DRM/GL does not fall through a NULL eglQueryString(EGL_NO_DISPLAY).
     */
    if (dpy == EGL_NO_DISPLAY) {
        if (name == EGL_EXTENSIONS) {
#ifdef ILAND_HAVE_WL_WINSYS
            if (iland_wl_ops)
                return "EGL_EXT_client_extensions EGL_EXT_platform_base "
                       "EGL_KHR_platform_wayland EGL_EXT_platform_wayland "
                       "EGL_KHR_platform_gbm EGL_MESA_platform_gbm "
                       "EGL_EXT_platform_gbm";
#endif
            /* Mode B / KMS: weston DRM asks for platform_gbm after
             * EGL_EXT_platform_base (egl-glue.c). Advertise it so setup
             * does not fail with "EGL does not support gbm platform". */
            return "EGL_EXT_client_extensions EGL_EXT_platform_base "
                   "EGL_KHR_platform_gbm EGL_MESA_platform_gbm "
                   "EGL_EXT_platform_gbm";
        }
        if (name == EGL_VERSION)
            return "1.5";
        return NULL;
    }
    if (!real_eglQueryString) return NULL;
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return real_eglQueryString(dpy, name);

    /* Advertise iland's dma_buf import so KMS/GBM clients (gbm-es2-demo) take
     * their real EGLImage path instead of bailing at the extension check. The
     * eglCreateImageKHR above backs it with the bo's IOSurface. */
    if (name == EGL_EXTENSIONS) {
        const char *base = real_eglQueryString(sd->angle_display, EGL_EXTENSIONS);
        static char *augmented = NULL;
        static const char *augmented_base = NULL;
        static const char kAdd[] =
            "EGL_EXT_image_dma_buf_import EGL_EXT_image_dma_buf_import_modifiers";
        if (base && !strstr(base, "EGL_EXT_image_dma_buf_import")) {
            if (!augmented || augmented_base != base) {
                free(augmented);
                size_t n = strlen(base) + 1 + sizeof(kAdd) + 1;
                augmented = malloc(n);
                if (augmented) {
                    snprintf(augmented, n, "%s %s", base, kAdd);
                    augmented_base = base;
                }
            }
            if (augmented) return augmented;
        }
        return base;
    }
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

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint *attrib_list)
{
    WWN_REQUIRE_ANGLE(EGL_NO_SURFACE);
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd)
        return real_eglCreatePbufferSurface(dpy, config, attrib_list);
    return real_eglCreatePbufferSurface(sd->angle_display, config, attrib_list);
}

/* ANGLE's rectangle texture target (CGL / older macOS ANGLE). Metal ANGLE
 * reports EGL_TEXTURE_2D via EGL_BIND_TO_TEXTURE_TARGET_ANGLE instead. */
#define WWN_GL_TEXTURE_RECTANGLE            0x84F5
#define WWN_GL_TEXTURE_BINDING_RECTANGLE    0x84F6

/* EGL_ANGLE_iosurface_client_buffer: TEXTURE_TARGET must equal the config's
 * EGL_BIND_TO_TEXTURE_TARGET_ANGLE. Metal returns EGL_TEXTURE_2D; hardcoding
 * RECTANGLE yields EGL_BAD_ATTRIBUTE and eglCreateWindowSurface fails after
 * the compositor has already mapped the xdg toplevel (a flash of a host
 * window, then the client exits). */
static EGLint zc_egl_texture_target(EGLShimDisplay *sd, EGLConfig config)
{
    EGLint target = 0;
    if (real_eglGetConfigAttrib &&
        real_eglGetConfigAttrib(sd->angle_display, config,
                                EGL_BIND_TO_TEXTURE_TARGET_ANGLE, &target) &&
        (target == EGL_TEXTURE_2D || target == EGL_TEXTURE_RECTANGLE_ANGLE))
        return target;
    return EGL_TEXTURE_RECTANGLE_ANGLE;
}

static unsigned int zc_gl_texture_target(EGLint egl_target)
{
    return (egl_target == EGL_TEXTURE_2D) ? (unsigned int)GL_TEXTURE_2D
                                          : WWN_GL_TEXTURE_RECTANGLE;
}

static unsigned int zc_gl_texture_binding(unsigned int gl_target)
{
    return (gl_target == (unsigned int)GL_TEXTURE_2D)
               ? (unsigned int)GL_TEXTURE_BINDING_2D
               : WWN_GL_TEXTURE_BINDING_RECTANGLE;
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

    EGLint egl_tex_target = zc_egl_texture_target(sd, ss->config);
    if (!ss->blit_gl_target)
        ss->blit_gl_target = zc_gl_texture_target(egl_tex_target);

    const EGLint attribs[] = {
        EGL_WIDTH,                          (EGLint)ss->width,
        EGL_HEIGHT,                         (EGLint)ss->height,
        EGL_IOSURFACE_PLANE_ANGLE,          0,
        EGL_TEXTURE_TARGET,                 egl_tex_target,
        EGL_TEXTURE_INTERNAL_FORMAT_ANGLE,  GL_BGRA_EXT,
        EGL_TEXTURE_FORMAT,                 EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TYPE_ANGLE,             GL_UNSIGNED_BYTE,
        EGL_NONE
    };

    /* GL's default framebuffer is bottom-up. Wayland-EGL flips dest Y in
     * zc_blit_to_slot so the posted IOSurface is top-down. GBM/KMS keeps
     * identity blit and lets the Metal presenter flip. */
    EGLSurface s = real_eglCreatePbufferFromClientBuffer(
        sd->angle_display, EGL_IOSURFACE_ANGLE,
        (EGLClientBuffer)io, ss->config, attribs);
    if (s == EGL_NO_SURFACE) {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            fprintf(stderr,
                    "iland: eglCreatePbufferFromClientBuffer(IOSurface) failed "
                    "(0x%04x) with EGL_TEXTURE_TARGET=0x%x\n",
                    real_eglGetError ? real_eglGetError() : 0,
                    (unsigned)egl_tex_target);
        }
    }
    ss->iosurf_pbuffers[idx] = s;
    return s;
}

static EGLSurface zc_pbuffer_for_bo(EGLShimDisplay *sd, EGLShimSurface *ss,
                                    int idx, struct gbm_bo *bo)
{
    return zc_pbuffer_for_iosurface(sd, ss, idx, gbm_bo_get_iosurface(bo));
}

/*
 * EGL_EXT_image_dma_buf_import over iland.
 *
 * gbm-es2-demo (a DRM/KMS/GBM client, like it is on Linux over waypipe) imports
 * its scanout gbm bo as an EGLImage, binds it to a GL texture, and renders the
 * cube into it through an FBO. On Linux that import is a real dma_buf; on iland
 * the buffer is an IOSurface and the "dma_buf fd" is a /dev/null placeholder —
 * gbm_bo_get_modifier() carries the IOSurface id instead (shims/gbm/gbm.m).
 * We resolve that id back to the IOSurface and hand ANGLE an
 * EGL_ANGLE_iosurface_client_buffer pbuffer, exactly as the zero-copy window
 * path does, so the client keeps its own dma_buf-import render path while iland
 * supplies the buffer underneath it. Without this, ANGLE/SwiftShader lack
 * EGL_EXT_image_dma_buf_import and the client cannot render at all.
 */
typedef struct EGLShimImage {
    EGLShimDisplay *sd;
    IOSurfaceRef    io;        /* +1 ref held from IOSurfaceLookup */
    EGLSurface      pbuffer;   /* Apple: ANGLE IOSurface-client-buffer pbuffer */
    EGLImageKHR     angle_image; /* Android: real ANGLE EGLImage from AHB */
#if defined(__ANDROID__)
    /* CPU-readback fallback (#140): the emulator's ANGLE runs on SwiftShader
     * software Vulkan, which lacks VK_ANDROID_external_memory_android_hardware_
     * buffer, so the AHB cannot be a GPU render target. The client then renders
     * into a plain GL texture (given storage in glEGLImageTargetTexture2DOES),
     * and drmModePageFlip copies that texture into the scanout AHB by CPU. */
    int             cpu_fallback;  /* AHB import failed; readback on present */
    unsigned int    client_tex;    /* client texture bound in the target call */
    uint32_t        surface_id;    /* IOSurfaceGetID key for the present hook */
    int             width, height;
    struct EGLShimImage *reg_next; /* CPU-fallback registry linkage */
#endif
} EGLShimImage;

#if defined(__ANDROID__)
/*
 * Android has no EGL_ANGLE_iosurface_client_buffer (egl.c: the Wayland zerocopy
 * path notes ANGLE/META-EGL lack it), so the Apple pbuffer import above cannot
 * work here. Instead import the AHardwareBuffer that backs the iland IOSurface
 * as a *real* ANGLE EGLImage via EGL_ANDROID_image_native_buffer +
 * eglGetNativeClientBufferANDROID. The client contract is unchanged
 * (EGL_LINUX_DMA_BUF_EXT + bit-63 IOSurface modifier); only the platform
 * substitution beneath it differs, which is the port-faithful thing to do.
 */
#ifndef EGL_NATIVE_BUFFER_ANDROID
#define EGL_NATIVE_BUFFER_ANDROID 0x3140
#endif
#ifndef EGL_IMAGE_PRESERVED_KHR
#define EGL_IMAGE_PRESERVED_KHR 0x30D2
#endif
static EGLClientBuffer (*g_eglGetNativeClientBufferANDROID)(const void *) = NULL;
static EGLImageKHR (*g_real_eglCreateImageKHR)(EGLDisplay, EGLContext, EGLenum,
                                               EGLClientBuffer, const EGLint *) =
    NULL;
static EGLBoolean (*g_real_eglDestroyImageKHR)(EGLDisplay, EGLImageKHR) = NULL;
static void (*g_real_glEGLImageTargetTexture2DOES)(unsigned int, void *) = NULL;

/* Resolve the AHB-import entry points from ANGLE once. Returns 0 on success. */
static int android_ahb_import_load(void)
{
    if (g_eglGetNativeClientBufferANDROID && g_real_eglCreateImageKHR &&
        g_real_eglDestroyImageKHR && g_real_glEGLImageTargetTexture2DOES)
        return 0;
    if (!real_eglGetProcAddress) return -1;
    if (!g_eglGetNativeClientBufferANDROID)
        g_eglGetNativeClientBufferANDROID = (EGLClientBuffer(*)(const void *))
            real_eglGetProcAddress("eglGetNativeClientBufferANDROID");
    if (!g_real_eglCreateImageKHR)
        g_real_eglCreateImageKHR =
            (EGLImageKHR(*)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer,
                            const EGLint *))
                real_eglGetProcAddress("eglCreateImageKHR");
    if (!g_real_eglDestroyImageKHR)
        g_real_eglDestroyImageKHR = (EGLBoolean(*)(EGLDisplay, EGLImageKHR))
            real_eglGetProcAddress("eglDestroyImageKHR");
    if (!g_real_glEGLImageTargetTexture2DOES)
        g_real_glEGLImageTargetTexture2DOES = (void (*)(unsigned int, void *))
            real_eglGetProcAddress("glEGLImageTargetTexture2DOES");
    return (g_eglGetNativeClientBufferANDROID && g_real_eglCreateImageKHR &&
            g_real_eglDestroyImageKHR && g_real_glEGLImageTargetTexture2DOES)
               ? 0
               : -1;
}

/* GL enums used by the CPU-readback fallback. Named to avoid clashing with the
 * WWN_GL_* set defined further down (past the EGLImage functions). */
#define WWN_AHBFB_TEXTURE_2D          0x0DE1
#define WWN_AHBFB_TEXTURE_BINDING_2D  0x8069
#define WWN_AHBFB_RGBA                0x1908
#define WWN_AHBFB_UNSIGNED_BYTE       0x1401
#define WWN_AHBFB_FRAMEBUFFER         0x8D40
#define WWN_AHBFB_FRAMEBUFFER_BINDING 0x8CA6
#define WWN_AHBFB_COLOR_ATTACHMENT0   0x8CE0

/* Registry of CPU-fallback images keyed by scanout IOSurface id. The DRM
 * page-flip path (drm_linux.c) calls iland_egl_flush_scanout_if_pending() with
 * the id of the buffer being scanned out; we look the image up here and copy
 * its client texture into the AHB. Both the client's GL work and the flip run
 * on the same client thread, so the client's GL context is current at flush. */
static pthread_mutex_t g_cpu_fb_lock = PTHREAD_MUTEX_INITIALIZER;
static EGLShimImage   *g_cpu_fb_images = NULL;
static unsigned int    g_cpu_fb_readfbo = 0; /* lazily created readback FBO */

static void cpu_fallback_register(EGLShimImage *img)
{
    pthread_mutex_lock(&g_cpu_fb_lock);
    img->reg_next = g_cpu_fb_images;
    g_cpu_fb_images = img;
    pthread_mutex_unlock(&g_cpu_fb_lock);
}

static void cpu_fallback_unregister(EGLShimImage *img)
{
    pthread_mutex_lock(&g_cpu_fb_lock);
    EGLShimImage **pp = &g_cpu_fb_images;
    while (*pp) {
        if (*pp == img) { *pp = img->reg_next; break; }
        pp = &(*pp)->reg_next;
    }
    pthread_mutex_unlock(&g_cpu_fb_lock);
}
#endif

/* An IOSurface-bindable, texture-renderable config. ANGLE's default display is
 * process-wide, so cache the first match. */
static EGLConfig image_iosurface_config(EGLShimDisplay *sd)
{
    static EGLConfig cached = NULL;
    static int tried = 0;
    if (tried) return cached;
    tried = 1;
    if (!real_eglChooseConfig) return NULL;
    const EGLint attrs[] = {
        EGL_SURFACE_TYPE,          EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,       EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE, 8,
        EGL_BIND_TO_TEXTURE_RGBA,  EGL_TRUE,
        EGL_NONE
    };
    EGLConfig c = NULL;
    EGLint n = 0;
    if (real_eglChooseConfig(sd->angle_display, attrs, &c, 1, &n) && n == 1)
        cached = c;
    return cached;
}

/* Strong: the shim owns the public EGL_KHR_image entry points. When ANGLE is
 * linked as a static archive (visionOS), rename-angle-symbols.sh moves ANGLE's
 * copies to _angle_* so there is exactly one link-time definition — the
 * IOSurface dma_buf path below. When ANGLE is a dylib (iOS/Android) this is
 * already the sole provider. Call into ANGLE via eglGetProcAddress, never the
 * renamed link symbols. */
EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                              EGLClientBuffer buffer, const EGLint *attrib_list)
{
    (void)ctx; (void)buffer;
    if (load_angle() < 0) return EGL_NO_IMAGE_KHR;
    EGLShimDisplay *sd = unwrap_display(dpy);
    if (!sd) return EGL_NO_IMAGE_KHR;

    /* Only iland's userspace KMS/GBM dma_buf import is handled here. */
    if (target != EGL_LINUX_DMA_BUF_EXT) return EGL_NO_IMAGE_KHR;

    EGLint width = 0, height = 0;
    uint32_t mod_lo = 0, mod_hi = 0;
    for (const EGLint *a = attrib_list; a && a[0] != EGL_NONE; a += 2) {
        switch (a[0]) {
        case EGL_WIDTH:  width  = a[1]; break;
        case EGL_HEIGHT: height = a[1]; break;
        case EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT: mod_lo = (uint32_t)a[1]; break;
        case EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT: mod_hi = (uint32_t)a[1]; break;
        default: break;
        }
    }

    uint64_t modifier = ((uint64_t)mod_hi << 32) | (uint64_t)mod_lo;
    if (!(modifier & 0x8000000000000000ULL)) {
        fprintf(stderr,
                "iland: eglCreateImageKHR(dma_buf) requires the iland IOSurface "
                "modifier (EGL_DMA_BUF_PLANE0_MODIFIER_{LO,HI}_EXT)\n");
        return EGL_NO_IMAGE_KHR;
    }
    uint32_t surface_id = (uint32_t)(modifier & 0x7fffffffffffffffULL);
    IOSurfaceRef io = IOSurfaceLookup(surface_id);
    if (!io) {
        fprintf(stderr,
                "iland: eglCreateImageKHR: IOSurfaceLookup(%u) failed\n",
                surface_id);
        return EGL_NO_IMAGE_KHR;
    }

#if defined(__ANDROID__)
    {
        AHardwareBuffer *ahb = ILandIOSurfaceGetHardwareBuffer(io);
        if (!ahb || android_ahb_import_load() != 0) {
            fprintf(stderr,
                    "iland: eglCreateImageKHR: no AHardwareBuffer / ANGLE lacks "
                    "EGL_ANDROID_image_native_buffer\n");
            CFRelease(io);
            return EGL_NO_IMAGE_KHR;
        }
        EGLClientBuffer cb = g_eglGetNativeClientBufferANDROID(ahb);
        if (!cb) {
            fprintf(stderr,
                    "iland: eglGetNativeClientBufferANDROID failed\n");
            CFRelease(io);
            return EGL_NO_IMAGE_KHR;
        }
        /* EGL_ANDROID_image_native_buffer accepts EGL_IMAGE_PRESERVED_KHR, but
         * the Android emulator's ANGLE rejects it with EGL_BAD_PARAMETER; a
         * render-target scanout buffer does not need preserved contents, so try
         * the empty attrib list first and only fall back to PRESERVED if the
         * driver actually wants it. */
        const EGLint img_attrs_none[] = {EGL_NONE};
        const EGLint img_attrs_pres[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
                                         EGL_NONE};
        EGLImageKHR angle_img = g_real_eglCreateImageKHR(
            sd->angle_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, cb,
            img_attrs_none);
        if (angle_img == EGL_NO_IMAGE_KHR)
            angle_img = g_real_eglCreateImageKHR(
                sd->angle_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, cb,
                img_attrs_pres);
        if (angle_img == EGL_NO_IMAGE_KHR) {
            /* Software GPU (emulator SwiftShader Vulkan) cannot import the AHB
             * as a GPU render target. Rather than fail the unmodified client
             * (which then aborts with "cannot create framebuffer"), fall back to
             * a CPU-readback path: the client renders into a plain GL texture
             * and drmModePageFlip copies it into the scanout AHB (#140). Only
             * the platform beneath the client changes — port-faithful. */
            EGLShimImage *fb = calloc(1, sizeof(*fb));
            if (!fb) { CFRelease(io); return EGL_NO_IMAGE_KHR; }
            fb->sd = sd;
            fb->io = io;                 /* keeps the AHB alive behind the image */
            fb->pbuffer = EGL_NO_SURFACE;
            fb->angle_image = EGL_NO_IMAGE_KHR;
            fb->cpu_fallback = 1;
            fb->surface_id = surface_id;
            fb->width  = width  > 0 ? width  : (int)IOSurfaceGetWidth(io);
            fb->height = height > 0 ? height : (int)IOSurfaceGetHeight(io);
            cpu_fallback_register(fb);
            static int warned = 0;
            if (!warned) {
                warned = 1;
                fprintf(stderr,
                        "iland: AHB native-buffer import unavailable "
                        "(0x%04x); using CPU-readback present for gbm "
                        "(#140)\n",
                        real_eglGetError ? real_eglGetError() : 0);
            }
            return (EGLImageKHR)fb;
        }
        EGLShimImage *img = calloc(1, sizeof(*img));
        if (!img) {
            g_real_eglDestroyImageKHR(sd->angle_display, angle_img);
            CFRelease(io);
            return EGL_NO_IMAGE_KHR;
        }
        img->sd = sd;
        img->io = io;              /* keeps the AHB alive behind the image */
        img->pbuffer = EGL_NO_SURFACE;
        img->angle_image = angle_img;
        return (EGLImageKHR)img;
    }
#else
    EGLConfig config = image_iosurface_config(sd);
    if (!config || !real_eglCreatePbufferFromClientBuffer) {
        CFRelease(io);
        return EGL_NO_IMAGE_KHR;
    }
    if (width <= 0)  width  = (EGLint)IOSurfaceGetWidth(io);
    if (height <= 0) height = (EGLint)IOSurfaceGetHeight(io);

    EGLint egl_tex_target = zc_egl_texture_target(sd, config);
    const EGLint pb_attribs[] = {
        EGL_WIDTH,                          width,
        EGL_HEIGHT,                         height,
        EGL_IOSURFACE_PLANE_ANGLE,          0,
        EGL_TEXTURE_TARGET,                 egl_tex_target,
        EGL_TEXTURE_INTERNAL_FORMAT_ANGLE,  GL_BGRA_EXT,
        EGL_TEXTURE_FORMAT,                 EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TYPE_ANGLE,             GL_UNSIGNED_BYTE,
        EGL_NONE
    };
    EGLSurface pb = real_eglCreatePbufferFromClientBuffer(
        sd->angle_display, EGL_IOSURFACE_ANGLE, (EGLClientBuffer)io, config,
        pb_attribs);
    if (pb == EGL_NO_SURFACE) {
        fprintf(stderr,
                "iland: eglCreateImageKHR: IOSurface pbuffer failed (0x%04x)\n",
                real_eglGetError ? real_eglGetError() : 0);
        CFRelease(io);
        return EGL_NO_IMAGE_KHR;
    }

    EGLShimImage *img = calloc(1, sizeof(*img));
    if (!img) {
        real_eglDestroySurface(sd->angle_display, pb);
        CFRelease(io);
        return EGL_NO_IMAGE_KHR;
    }
    img->sd = sd;
    img->io = io;
    img->pbuffer = pb;
    return (EGLImageKHR)img;
#endif
}

/* Strong: see eglCreateImageKHR (ANGLE static copies namespaced to _angle_*). */
EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image)
{
    (void)dpy;
    EGLShimImage *img = (EGLShimImage *)image;
    if (!img) return EGL_FALSE;
#if defined(__ANDROID__)
    if (img->cpu_fallback)
        cpu_fallback_unregister(img);
    if (img->sd && img->angle_image && g_real_eglDestroyImageKHR)
        g_real_eglDestroyImageKHR(img->sd->angle_display, img->angle_image);
#endif
    if (img->sd && img->pbuffer && img->pbuffer != EGL_NO_SURFACE) {
        if (real_eglReleaseTexImage)
            real_eglReleaseTexImage(img->sd->angle_display, img->pbuffer,
                                    EGL_BACK_BUFFER);
        if (real_eglDestroySurface)
            real_eglDestroySurface(img->sd->angle_display, img->pbuffer);
    }
    if (img->io) CFRelease(img->io);
    free(img);
    return EGL_TRUE;
}

/* glEGLImageTargetTexture2DOES: bind the IOSurface behind the image to the
 * texture the caller has bound in the current context. ANGLE's IOSurface
 * client-buffer textures are colour-renderable, so the demo can attach the
 * resulting texture to an FBO and render straight into the scanout IOSurface —
 * the same present path kmscube uses. */
/* Strong: see eglCreateImageKHR (ANGLE static copies namespaced to _angle_*). */
void glEGLImageTargetTexture2DOES(unsigned int target, void *image)
{
    EGLShimImage *img = (EGLShimImage *)image;
    if (!img || !img->sd) return;
#if defined(__ANDROID__)
    /* Android import is a real ANGLE EGLImage — bind it to the currently bound
     * texture via ANGLE's own entry point, not the IOSurface pbuffer path. */
    if (img->angle_image && g_real_glEGLImageTargetTexture2DOES) {
        g_real_glEGLImageTargetTexture2DOES(target, img->angle_image);
        return;
    }
    /* CPU-readback fallback (#140): no dmabuf alias is possible, so give the
     * client's currently bound texture real RGBA8 storage and remember its id.
     * The client attaches it to an FBO and renders; drmModePageFlip reads it
     * back into the scanout AHB. */
    if (img->cpu_fallback && g_glTexImage2D && g_glGetIntegerv) {
        int bound = 0;
        g_glGetIntegerv(WWN_AHBFB_TEXTURE_BINDING_2D, &bound);
        img->client_tex = (unsigned int)bound;
        g_glTexImage2D(target ? target : WWN_AHBFB_TEXTURE_2D, 0, WWN_AHBFB_RGBA,
                       img->width, img->height, 0, WWN_AHBFB_RGBA,
                       WWN_AHBFB_UNSIGNED_BYTE, NULL);
        return;
    }
#endif
    (void)target;
    if (!img->pbuffer || img->pbuffer == EGL_NO_SURFACE || !real_eglBindTexImage)
        return;
    real_eglBindTexImage(img->sd->angle_display, img->pbuffer, EGL_BACK_BUFFER);
}

/* GLES3 enums; GLES2/gl2.h predates them. Depth-blit helpers below serve both
 * the Wayland-EGL winsys and the GBM zerocopy path — keep them outside
 * ILAND_HAVE_WL_WINSYS so Mode B (ILAND_NO_WL_WINSYS) still compiles. */
#define WWN_GL_READ_FRAMEBUFFER          0x8CA8
#define WWN_GL_DRAW_FRAMEBUFFER          0x8CA9
#define WWN_GL_READ_FRAMEBUFFER_BINDING  0x8CAA
#define WWN_GL_DRAW_FRAMEBUFFER_BINDING  0x8CA6
#define WWN_GL_COLOR_BUFFER_BIT          0x4000
#define WWN_GL_NEAREST                   0x2600
#define WWN_GL_COLOR_ATTACHMENT0         0x8CE0
#define WWN_GL_FRAMEBUFFER_COMPLETE      0x8CD5
#define WWN_GL_SCISSOR_TEST              0x0C11
#define WWN_GL_BLEND                     0x0BE2

#if defined(__ANDROID__)
/* Called from the DRM page-flip path (drm_linux.c) just before the buffer is
 * presented. If the scanout IOSurface is backed by a CPU-fallback EGLImage
 * (#140: AHB native-buffer import unavailable on the software-GPU emulator),
 * read the client's render texture back into the AHB so the compositor sees the
 * frame. The client's GL context is current here (it renders then flips on the
 * same thread), matching the eglSwapBuffers CPU-copy precedent above. No-op when
 * the id has no pending fallback image (the common HW-import / Apple case). */
void iland_egl_flush_scanout_if_pending(uint32_t surface_id)
{
    pthread_mutex_lock(&g_cpu_fb_lock);
    EGLShimImage *img = g_cpu_fb_images;
    while (img && img->surface_id != surface_id) img = img->reg_next;
    pthread_mutex_unlock(&g_cpu_fb_lock);
    if (!img || !img->cpu_fallback || !img->client_tex || !img->io) return;
    if (!g_glReadPixels || !g_glGenFramebuffers || !g_glBindFramebuffer ||
        !g_glFramebufferTexture2D || !g_glGetIntegerv)
        return;

    const int w = img->width, h = img->height;
    if (w <= 0 || h <= 0) return;
    const size_t total = (size_t)w * h * 4;
    if (g_pixels_sz < total) {
        void *p = realloc(g_pixels, total);
        if (!p) return;
        g_pixels = p;
        g_pixels_sz = total;
    }

    int prev_fbo = 0;
    g_glGetIntegerv(WWN_AHBFB_FRAMEBUFFER_BINDING, &prev_fbo);
    if (!g_cpu_fb_readfbo) {
        g_glGenFramebuffers(1, &g_cpu_fb_readfbo);
        if (!g_cpu_fb_readfbo) return;
    }
    g_glBindFramebuffer(WWN_AHBFB_FRAMEBUFFER, g_cpu_fb_readfbo);
    g_glFramebufferTexture2D(WWN_AHBFB_FRAMEBUFFER, WWN_AHBFB_COLOR_ATTACHMENT0,
                             WWN_AHBFB_TEXTURE_2D, img->client_tex, 0);
    if (g_glCheckFramebufferStatus) {
        unsigned int st = g_glCheckFramebufferStatus(WWN_AHBFB_FRAMEBUFFER);
        if (st != WWN_GL_FRAMEBUFFER_COMPLETE) {
            g_glFramebufferTexture2D(WWN_AHBFB_FRAMEBUFFER,
                                     WWN_AHBFB_COLOR_ATTACHMENT0,
                                     WWN_AHBFB_TEXTURE_2D, 0, 0);
            g_glBindFramebuffer(WWN_AHBFB_FRAMEBUFFER, (unsigned int)prev_fbo);
            return;
        }
    }

    g_glReadPixels(0, 0, w, h, WWN_AHBFB_RGBA, WWN_AHBFB_UNSIGNED_BYTE,
                   g_pixels);

    g_glFramebufferTexture2D(WWN_AHBFB_FRAMEBUFFER, WWN_AHBFB_COLOR_ATTACHMENT0,
                             WWN_AHBFB_TEXTURE_2D, 0, 0);
    g_glBindFramebuffer(WWN_AHBFB_FRAMEBUFFER, (unsigned int)prev_fbo);

    IOSurfaceLock(img->io, 0, NULL);
    uint8_t *dst8 = (uint8_t *)IOSurfaceGetBaseAddress(img->io);
    size_t pitch = IOSurfaceGetBytesPerRow(img->io);
    const uint8_t *src8 = (const uint8_t *)g_pixels;
    if (dst8 && pitch > 0) {
        for (int y = 0; y < h; y++) {
            const uint8_t *s = src8 + (size_t)(h - 1 - y) * (size_t)w * 4;
            uint8_t *d = dst8 + (size_t)y * pitch;
            memcpy(d, s, (size_t)w * 4);
        }
        swap_rgba_to_bgra(dst8, (uint32_t)w, (uint32_t)h, pitch);
    }
    IOSurfaceUnlock(img->io, 0, NULL);
}
#endif

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
    if (ss->wayland)
        fprintf(stderr,
                "iland: Wayland-EGL %ux%u: ANGLE GLES pbuffer (depth %d) "
                "blitted to IOSurface, posted as linux-dmabuf on the "
                "wl_surface (not GBM/KMS)\n",
                ss->width, ss->height, depth);
    else
        fprintf(stderr,
                "iland: GBM/KMS %ux%u: ANGLE GLES pbuffer (depth %d) "
                "blitted to the presented IOSurface\n",
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

    /* Success is 0 (IOReturn). Do not use kIOReturnSuccess — it lives in
     * IOKit, which iOS/tvOS/watchOS SDKs do not expose to this TU. */
    if (IOSurfaceLock(io, kIOSurfaceLockReadOnly, NULL) != 0) {
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

    if (!ss->blit_gl_target)
        ss->blit_gl_target = WWN_GL_TEXTURE_RECTANGLE;

    if (!ss->blit_tex) {
        g_glGenTextures(1, &ss->blit_tex);
        g_glGenFramebuffers(1, &ss->blit_fbo);
        if (!ss->blit_tex || !ss->blit_fbo) return;
    }

    const unsigned int gl_target = ss->blit_gl_target;
    const unsigned int gl_binding = zc_gl_texture_binding(gl_target);

    int prev_draw = 0, prev_read = 0, prev_tex = 0;
    g_glGetIntegerv(WWN_GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw);
    g_glGetIntegerv(WWN_GL_READ_FRAMEBUFFER_BINDING, &prev_read);
    g_glGetIntegerv(gl_binding, &prev_tex);

    g_glBindTexture(gl_target, ss->blit_tex);
    if (!real_eglBindTexImage(sd->angle_display, dst_pb, EGL_BACK_BUFFER)) {
        g_glBindTexture(gl_target, (unsigned int)prev_tex);
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
                             gl_target, ss->blit_tex, 0);
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

    /* Smithay (niri) and weston leave SCISSOR_TEST/BLEND on. ANGLE then
     * returns GL_INVALID_OPERATION (0x0502) on this blit. Drain a leftover
     * client error so we do not mis-attribute it. */
    if (g_glGetError)
        (void)g_glGetError();
    unsigned char scissor_on = 0;
    unsigned char blend_on = 0;
    if (g_glIsEnabled) {
        scissor_on = g_glIsEnabled(WWN_GL_SCISSOR_TEST);
        blend_on = g_glIsEnabled(WWN_GL_BLEND);
    }
    if (g_glDisable) {
        if (scissor_on)
            g_glDisable(WWN_GL_SCISSOR_TEST);
        if (blend_on)
            g_glDisable(WWN_GL_BLEND);
    }

    /* Identity blit. GBM/KMS: the Metal presenter Y-flips in its shader.
     * Wayland GLES: keep GL's bottom-up rows and mark WWNBottomUp. Wawona
     * bakes a flipped CGImage; a CALayer Y-scale under geometryFlipped looks
     * like inverted X+Y. */
    g_glBlitFramebuffer(0, 0, (int)ss->width, (int)ss->height,
                        0, 0, (int)ss->width, (int)ss->height,
                        WWN_GL_COLOR_BUFFER_BIT, WWN_GL_NEAREST);
    zc_report_gl_error("blit into the presented IOSurface");

    if (g_glEnable) {
        if (scissor_on)
            g_glEnable(WWN_GL_SCISSOR_TEST);
        if (blend_on)
            g_glEnable(WWN_GL_BLEND);
    }

    /* Detach before release so the texture does not outlive the binding. */
    g_glFramebufferTexture2D(WWN_GL_DRAW_FRAMEBUFFER, WWN_GL_COLOR_ATTACHMENT0,
                             gl_target, 0, 0);
    g_glBindFramebuffer(WWN_GL_DRAW_FRAMEBUFFER, (unsigned int)prev_draw);
    g_glBindFramebuffer(WWN_GL_READ_FRAMEBUFFER, (unsigned int)prev_read);

    real_eglReleaseTexImage(sd->angle_display, dst_pb, EGL_BACK_BUFFER);
    g_glBindTexture(gl_target, (unsigned int)prev_tex);
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

#ifdef ILAND_HAVE_WL_WINSYS
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
    /* A wl_egl_window is never a gbm_surface. weston-simple-egl (and any
     * client that follows shared/platform.h) wants EGL_PLATFORM_WAYLAND;
     * if it fell through eglGetDisplay(wl_display) the wrapper is tagged
     * GBM and the native display pointer was stashed as gbm_device. Recover
     * that instead of page-flipping KMS. */
    if (iland_wl_ops &&
        iland_wl_egl_window_is_valid((struct wl_egl_window *)win)) {
        struct wl_egl_window *wlwin = (struct wl_egl_window *)win;
        if (sd->kind != EGL_SHIM_DISPLAY_WAYLAND) {
            fprintf(stderr,
                    "iland: native window is wl_egl_window; using "
                    "EGL_PLATFORM_WAYLAND (not GBM/KMS)\n");
            sd->kind = EGL_SHIM_DISPLAY_WAYLAND;
            if (!sd->wl_display)
                sd->wl_display = (struct wl_display *)sd->gbm_device;
            if (!sd->wl_winsys && sd->wl_display)
                sd->wl_winsys = iland_wl_ops->winsys_create(sd->wl_display);
        }
        if (!sd->wl_winsys || !iland_wl_egl_window_is_valid(wlwin))
            return EGL_NO_SURFACE;

        EGLShimSurface *ws = calloc(1, sizeof(*ws));
        if (!ws) return EGL_NO_SURFACE;

        ws->wayland = 1;
        ws->zerocopy = 0;
        ws->config = config;
        ws->wl_window = wlwin;
        ws->wl_swapchain = iland_wl_swapchain_create(sd->wl_winsys, wlwin);
        if (!ws->wl_swapchain) {
            free(ws);
            return EGL_NO_SURFACE;
        }
        iland_wl_swapchain_get_size(ws->wl_swapchain, &ws->width, &ws->height);
        ws->wl_slot = 0;

        if (zerocopy_enabled()) {
            /* Bind slot 0 without a current context; eglMakeCurrent picks it up. */
            IOSurfaceRef io = iland_wl_swapchain_iosurface(ws->wl_swapchain, 0);
            EGLSurface pb = zc_pbuffer_for_iosurface(sd, ws, 0, io);
            if (pb) {
                ws->zerocopy = 1;
                /* Draw into a depth-capable pbuffer from the first frame; the
                 * IOSurface pbuffer above is only a blit target. */
                EGLSurface render = zc_render_pbuffer(sd, ws);
                ws->angle_surface = render ? render : pb;
                return (EGLSurface)ws;
            }
            /* Android ANGLE/META-EGL lack EGL_ANGLE_iosurface_client_buffer —
             * fall through to the glReadPixels + AHB copy path (GBM parity). */
            fprintf(stderr,
                    "iland: Wayland zero-copy unavailable; using CPU copy path\n");
        }

        EGLint pb_attribs[] = {
            EGL_WIDTH,  (EGLint)ws->width,
            EGL_HEIGHT, (EGLint)ws->height,
            EGL_NONE
        };
        ws->angle_surface = real_eglCreatePbufferSurface(sd->angle_display,
                                                         config, pb_attribs);
        if (!ws->angle_surface) {
            iland_wl_swapchain_destroy(ws->wl_swapchain);
            free(ws);
            return EGL_NO_SURFACE;
        }
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
        if (ss->zerocopy) {
            zc_drop_blit_objects(ss);
            zc_drop_pbuffers(sd, ss);
        } else if (ss->angle_surface) {
            real_eglDestroySurface(sd->angle_display, ss->angle_surface);
            ss->angle_surface = EGL_NO_SURFACE;
        }
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

    EGLBoolean ok = real_eglMakeCurrent(sd->angle_display, adraw, aread, ctx);
    if (!ok) {
        fprintf(stderr,
                "iland: eglMakeCurrent failed err=0x%x draw=%p read=%p ctx=%p\n",
                real_eglGetError ? (unsigned)real_eglGetError() : 0,
                (void *)adraw, (void *)aread, (void *)ctx);
    }
    return ok;
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
        if (ss->zerocopy) {
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

        /* CPU copy path: same idea as GBM when IOSurface client-buffer fails. */
        IOSurfaceRef iosurf =
            iland_wl_swapchain_iosurface(ss->wl_swapchain, ss->wl_slot);
        uint32_t w = ss->width;
        uint32_t h = ss->height;
        size_t total = (size_t)w * h * 4;

        if (!g_glReadPixels || !iosurf)
            return real_eglSwapBuffers(sd->angle_display, ss->angle_surface);

        if (g_pixels_sz < total) {
            void *p = realloc(g_pixels, total);
            if (!p)
                return real_eglSwapBuffers(sd->angle_display, ss->angle_surface);
            g_pixels = p;
            g_pixels_sz = total;
        }

        g_glReadPixels(0, 0, (int)w, (int)h, 0x1908, 0x1401, g_pixels);

        EGLBoolean ret =
            real_eglSwapBuffers(sd->angle_display, ss->angle_surface);
        if (!ret) return ret;

        IOSurfaceLock(iosurf, 0, NULL);
        uint8_t *dst8 = (uint8_t *)IOSurfaceGetBaseAddress(iosurf);
        size_t dst_pitch_bytes = IOSurfaceGetBytesPerRow(iosurf);
        const uint8_t *src8 = (const uint8_t *)g_pixels;
        if (dst8 && dst_pitch_bytes > 0) {
            /* glReadPixels is bottom-up. Leave it; WWNBottomUp + compositor
             * CGImage flip matches the zerocopy blit. */
            for (uint32_t y = 0; y < h; y++) {
                const uint8_t *s = src8 + (size_t)y * w * 4;
                uint8_t *d = dst8 + (size_t)y * dst_pitch_bytes;
                memcpy(d, s, (size_t)w * 4);
            }
            swap_rgba_to_bgra(dst8, w, h, dst_pitch_bytes);
        }
        IOSurfaceUnlock(iosurf, 0, NULL);

        iland_wl_swapchain_post(ss->wl_swapchain, ss->wl_slot);

        if (iland_wl_swapchain_check_resize(ss->wl_swapchain)) {
            iland_wl_swapchain_get_size(ss->wl_swapchain, &ss->width,
                                        &ss->height);
            if (ss->angle_surface) {
                real_eglDestroySurface(sd->angle_display, ss->angle_surface);
                ss->angle_surface = EGL_NO_SURFACE;
            }
            EGLint pb_attribs[] = {
                EGL_WIDTH,  (EGLint)ss->width,
                EGL_HEIGHT, (EGLint)ss->height,
                EGL_NONE
            };
            ss->angle_surface = real_eglCreatePbufferSurface(
                sd->angle_display, ss->config, pb_attribs);
            if (ss->angle_surface) {
                EGLContext cur = real_eglGetCurrentContext
                                     ? real_eglGetCurrentContext()
                                     : EGL_NO_CONTEXT;
                if (cur != EGL_NO_CONTEXT)
                    real_eglMakeCurrent(sd->angle_display, ss->angle_surface,
                                        ss->angle_surface, cur);
            }
        }

        int slot = iland_wl_swapchain_acquire(ss->wl_swapchain);
        if (slot < 0) return EGL_FALSE;
        ss->wl_slot = slot;
        return EGL_TRUE;
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

    swap_rgba_to_bgra(dst8, w, h, dst_pitch_bytes);

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
 * shared/platform.h's eglGetProcAddress("eglGetPlatformDisplayEXT").
 *
 * smithay / khronos-egl (nested niri) load core EGL 1.5 via GetProcAddress
 * as well. Without eglCreateContext here, ChooseConfig went through the shim
 * (valid ANGLE display) then CreateContext hit ANGLE with the shim pointer
 * and failed EGL_BAD_DISPLAY. */
/* Forward decl: under ILAND_ANGLE_STATIC the ANGLE rename/#undef leaves no
 * prototype from <EGL/egl.h>, and the table below takes our address before
 * the definition. */
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname);

EGLBoolean eglQueryDmaBufFormatsEXT(EGLDisplay dpy, EGLint max_formats,
                                    EGLint *formats, EGLint *num_formats)
{
    const EGLint n = (EGLint)(sizeof(kIlandDmabufFormats) /
                              sizeof(kIlandDmabufFormats[0]));
    (void)dpy;
    if (num_formats)
        *num_formats = n;
    if (max_formats == 0)
        return EGL_TRUE;
    if (max_formats < 0 || !formats)
        return EGL_FALSE;
    EGLint copy = max_formats < n ? max_formats : n;
    for (EGLint i = 0; i < copy; i++)
        formats[i] = kIlandDmabufFormats[i];
    return EGL_TRUE;
}

EGLBoolean eglQueryDmaBufModifiersEXT(EGLDisplay dpy, EGLint format,
                                      EGLint max_modifiers,
                                      uint64_t *modifiers,
                                      EGLBoolean *external_only,
                                      EGLint *num_modifiers)
{
    int supported = 0;
    const EGLint nfmt = (EGLint)(sizeof(kIlandDmabufFormats) /
                                 sizeof(kIlandDmabufFormats[0]));
    (void)dpy;
    for (EGLint i = 0; i < nfmt; i++) {
        if (format == kIlandDmabufFormats[i]) {
            supported = 1;
            break;
        }
    }
    if (!supported) {
        if (num_modifiers)
            *num_modifiers = 0;
        return EGL_TRUE;
    }
    if (num_modifiers)
        *num_modifiers = 1;
    if (max_modifiers == 0)
        return EGL_TRUE;
    if (max_modifiers < 0)
        return EGL_FALSE;
    if (modifiers && max_modifiers >= 1)
        modifiers[0] = ILAND_IOSURFACE_MODIFIER;
    if (external_only && max_modifiers >= 1)
        external_only[0] = EGL_FALSE;
    return EGL_TRUE;
}

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
    { "eglGetConfigs",            (void *)eglGetConfigs },
    { "eglGetConfigAttrib",       (void *)eglGetConfigAttrib },
    { "eglBindAPI",               (void *)eglBindAPI },
    { "eglCreateContext",         (void *)eglCreateContext },
    { "eglDestroyContext",        (void *)eglDestroyContext },
    { "eglCreatePbufferSurface",  (void *)eglCreatePbufferSurface },
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
    { "eglQueryContext",          (void *)eglQueryContext },
    { "eglQuerySurface",          (void *)eglQuerySurface },
    { "eglReleaseThread",         (void *)eglReleaseThread },
    { "eglGetProcAddress",        (void *)eglGetProcAddress },
    /* iland dma_buf import (EGL_EXT_image_dma_buf_import). ANGLE also answers
     * for these but knows nothing about iland's IOSurface-in-modifier scheme,
     * so a client resolving them dynamically must get the shim's versions. */
    { "eglCreateImageKHR",        (void *)eglCreateImageKHR },
    { "eglDestroyImageKHR",       (void *)eglDestroyImageKHR },
    { "eglQueryDmaBufFormatsEXT", (void *)eglQueryDmaBufFormatsEXT },
    { "eglQueryDmaBufModifiersEXT",
      (void *)eglQueryDmaBufModifiersEXT },
    { "glEGLImageTargetTexture2DOES",
      (void *)glEGLImageTargetTexture2DOES },
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
