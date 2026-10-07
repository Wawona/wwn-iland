/* FROZEN. Authority moved to github.com/Wawona/wwn-iomfb-rs.
 * Do not grow this trampoline or guess new ABI.
 */
#include <CoreFoundation/CoreFoundation.h>
#import <IOSurface/IOSurfaceRef.h>
#import <Metal/Metal.h>
#import <CoreGraphics/CoreGraphics.h>
#import <dlfcn.h>
#import <errno.h>
#import <os/lock.h>
#import <os/log.h>
#import <pthread.h>
#import <time.h>
#import <unistd.h>

#import "wwn_iland_iomfb.h"
#import "IOMobileFramebuffer.h"

typedef IOMobileFramebufferReturn (*FnGetMain)(IOMobileFramebufferRef *);
typedef IOMobileFramebufferReturn (*FnGetSecondary)(IOMobileFramebufferRef *);
typedef IOMobileFramebufferReturn (*FnGetSize)(
    IOMobileFramebufferRef,
    IOMobileFramebufferDisplaySize *);
typedef IOMobileFramebufferReturn (*FnSwapBegin)(IOMobileFramebufferRef, int *);
typedef IOMobileFramebufferReturn (*FnSwapEnd)(IOMobileFramebufferRef);
typedef IOMobileFramebufferReturn (*FnSwapWait)(IOMobileFramebufferRef, int, int);
typedef IOMobileFramebufferReturn (*FnSwapSetLayer)(
    IOMobileFramebufferRef,
    int,
    IOSurfaceRef,
    CGRect,
    CGRect,
    int);
typedef IOMobileFramebufferReturn (*FnDefaultSurface)(
    IOMobileFramebufferRef,
    int,
    IOSurfaceRef *);
typedef IOMobileFramebufferReturn (*FnPowerSave)(IOMobileFramebufferRef, int);

typedef struct WWNIOMFBPlatformSession {
    IOMobileFramebufferRef display;
    IOSurfaceRef surfaces[WWN_IOMFB_LAYER_COUNT];
    IOSurfaceRef lastSurface;
    IOSurfaceRef defaultSurface;
    uint32_t width;
    uint32_t height;
    FnSwapBegin swapBegin;
    FnSwapEnd swapEnd;
    FnSwapWait swapWait;
    FnSwapSetLayer swapSetLayer;
    FnDefaultSurface getDefaultSurface;
    FnPowerSave powerSave;
    id device;
    id queue;
    id textures[WWN_IOMFB_LAYER_COUNT];
    bool restored;
    bool exclusive;
    bool holdRunning;
    pthread_t holdThread;
    os_unfair_lock presentLock;
    uint64_t lastPresentNs;
} WWNIOMFBPlatformSession;

static void wwn_iomfb_session_destroy(WWNIOMFBPlatformSession *session)
{
    if (!session)
        return;
    session->exclusive = false;
    if (session->holdRunning) {
        pthread_join(session->holdThread, NULL);
        session->holdRunning = false;
    }
    if (session->lastSurface) {
        CFRelease(session->lastSurface);
        session->lastSurface = NULL;
    }
    if (session->defaultSurface) {
        CFRelease(session->defaultSurface);
        session->defaultSurface = NULL;
    }
    for (int i = 0; i < WWN_IOMFB_LAYER_COUNT; ++i) {
        if (session->surfaces[i]) {
            CFRelease(session->surfaces[i]);
            session->surfaces[i] = NULL;
        }
        if (session->textures[i]) {
            CFRelease((__bridge CFTypeRef)session->textures[i]);
            session->textures[i] = nil;
        }
    }
    if (session->queue) {
        CFRelease((__bridge CFTypeRef)session->queue);
        session->queue = nil;
    }
    if (session->device) {
        CFRelease((__bridge CFTypeRef)session->device);
        session->device = nil;
    }
    free(session);
}

static void write_error(char *error, size_t capacity, const char *message) {
    if (!error || capacity == 0) return;
    if (!message) message = "unknown IOMFB error";
    snprintf(error, capacity, "%s", message);
}

static IOSurfaceRef make_surface(uint32_t width, uint32_t height) {
    const uint32_t bytesPerElement = 4;
    const uint32_t pixelFormat = (uint32_t)'BGRA';
    CFNumberRef nw = CFNumberCreate(NULL, kCFNumberIntType, &width);
    CFNumberRef nh = CFNumberCreate(NULL, kCFNumberIntType, &height);
    CFNumberRef npf = CFNumberCreate(NULL, kCFNumberSInt32Type, &pixelFormat);
    CFNumberRef nbpe = CFNumberCreate(NULL, kCFNumberIntType, &bytesPerElement);
    const void *keys[] = {kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfacePixelFormat,
                          kIOSurfaceBytesPerElement};
    const void *vals[] = {nw, nh, npf, nbpe};
    CFDictionaryRef props =
        CFDictionaryCreate(NULL, keys, vals, 4, &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);
    CFRelease(nw);
    CFRelease(nh);
    CFRelease(npf);
    CFRelease(nbpe);
    return IOSurfaceCreate(props);
}

static bool valid_surface(
    WWNIOMFBPlatformSession *session,
    IOSurfaceRef surface) {
    if (!surface) return false;
    if (IOSurfaceGetWidth(surface) != session->width ||
        IOSurfaceGetHeight(surface) != session->height) {
        return false;
    }
    OSType format = IOSurfaceGetPixelFormat(surface);
    return format == 'BGRA' || format == 'ARGB';
}

static int present_surface(
    WWNIOMFBPlatformSession *session,
    IOSurfaceRef surface,
    WwnIomfbDamage damage,
    uint64_t frame,
    const char *route,
    const char *copy) {
    if (!session || session->restored || !valid_surface(session, surface)) {
        return EINVAL;
    }
    os_unfair_lock_lock(&session->presentLock);
    int token = 0;
    IOMobileFramebufferReturn begin = session->swapBegin(session->display, &token);
    if (begin != 0) {
        os_unfair_lock_unlock(&session->presentLock);
        return begin;
    }
    CGRect full = CGRectMake(0, 0, session->width, session->height);
    IOMobileFramebufferReturn set =
        session->swapSetLayer(session->display, 0, surface, full, full, 0);
    IOMobileFramebufferReturn end = session->swapEnd(session->display);
    IOMobileFramebufferReturn wait = 0;
    if (end == 0 && session->swapWait) {
        wait = session->swapWait(
            session->display, token, WWN_IOMFB_WAIT_UNTIL_DISPLAYED);
    }
    struct timespec now = {0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    session->lastPresentNs =
        (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
    if (set == 0 && end == 0) {
        if (session->lastSurface != surface) {
            if (session->lastSurface) {
                CFRelease(session->lastSurface);
            }
            session->lastSurface = (IOSurfaceRef)CFRetain(surface);
        }
    }
    os_unfair_lock_unlock(&session->presentLock);
    if (frame < 8 || set != 0 || end != 0 || wait != 0) {
        os_log(OS_LOG_DEFAULT,
               "wwn.iomfb op=present backing_id=%{public}u frame=%{public}llu "
               "route=%{public}s copy=%{public}s damage=%{public}u,%{public}u,"
               "%{public}u,%{public}u swap=%{public}d/%{public}d wait=%{public}d",
               IOSurfaceGetID(surface), frame, route, copy,
               damage.x, damage.y, damage.width, damage.height, set, end, wait);
    }
    if (set != 0) return set;
    if (end != 0) return end;
    return wait;
}

static void *wwn_iomfb_hold_main(void *arg) {
    WWNIOMFBPlatformSession *session = (WWNIOMFBPlatformSession *)arg;
    uint64_t holdFrame = 0;
    /* Re-present only when iOS may have stolen scanout (no client swap
     * for about one 60 Hz period). Busy swapping fights Weston. */
    const uint64_t staleNs = 12ull * 1000ull * 1000ull;
    while (session->exclusive && !session->restored) {
        struct timespec now = {0};
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t ns =
            (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
        IOSurfaceRef surface = session->lastSurface;
        if (surface && session->lastPresentNs != 0 &&
            ns - session->lastPresentNs >= staleNs) {
            (void)present_surface(
                session, surface, (WwnIomfbDamage){0, 0, 0, 0}, holdFrame,
                "exclusive-hold", "zero");
            holdFrame += 1;
        } else {
            usleep(4000);
        }
    }
    return NULL;
}

void *wwn_iomfb_platform_open(
    uint32_t *out_width,
    uint32_t *out_height,
    char *error,
    size_t error_capacity) {
    void *framework = dlopen(
        "/System/Library/PrivateFrameworks/IOMobileFramebuffer.framework/"
        "IOMobileFramebuffer",
        RTLD_NOW | RTLD_LOCAL);
    if (!framework) {
        write_error(error, error_capacity, "IOMobileFramebuffer load failed");
        return NULL;
    }
    FnGetMain getMain = (FnGetMain)dlsym(
        framework, "IOMobileFramebufferGetMainDisplay");
    FnGetSecondary getSecondary = (FnGetSecondary)dlsym(
        framework, "IOMobileFramebufferGetSecondaryDisplay");
    FnGetSize getSize = (FnGetSize)dlsym(
        framework, "IOMobileFramebufferGetDisplaySize");
    FnSwapBegin swapBegin = (FnSwapBegin)dlsym(
        framework, "IOMobileFramebufferSwapBegin");
    FnSwapEnd swapEnd = (FnSwapEnd)dlsym(
        framework, "IOMobileFramebufferSwapEnd");
    FnSwapWait swapWait = (FnSwapWait)dlsym(
        framework, "IOMobileFramebufferSwapWait");
    FnSwapSetLayer swapSetLayer = (FnSwapSetLayer)dlsym(
        framework, "IOMobileFramebufferSwapSetLayer");
    FnDefaultSurface getDefaultSurface = (FnDefaultSurface)dlsym(
        framework, "IOMobileFramebufferGetLayerDefaultSurface");
    FnPowerSave powerSave = (FnPowerSave)dlsym(
        framework, "IOMobileFramebufferEnableDisableVideoPowerSavings");
    typedef IOMobileFramebufferReturn (*FnPowerChange)(
        IOMobileFramebufferRef, int);
    FnPowerChange powerChange = (FnPowerChange)dlsym(
        framework, "IOMobileFramebufferRequestPowerChange");
    if (!getMain || !getSize || !swapBegin || !swapEnd || !swapSetLayer) {
        write_error(error, error_capacity, "IOMobileFramebuffer symbols missing");
        return NULL;
    }

    IOMobileFramebufferRef display = NULL;
    IOMobileFramebufferReturn result = getMain(&display);
    if ((result != 0 || !display) && getSecondary) {
        result = getSecondary(&display);
    }
    if (result != 0 || !display) {
        write_error(error, error_capacity, "IOMobileFramebuffer display unavailable");
        return NULL;
    }
    IOMobileFramebufferDisplaySize size = {0};
    result = getSize(display, &size);
    if (result != 0 || size.width == 0 || size.height == 0) {
        write_error(error, error_capacity, "IOMobileFramebuffer size unavailable");
        return NULL;
    }
    uint32_t width = (uint32_t)size.width;
    uint32_t height = (uint32_t)size.height;

    WWNIOMFBPlatformSession *session = calloc(1, sizeof(*session));
    session->display = display;
    session->width = width;
    session->height = height;
    session->swapBegin = swapBegin;
    session->swapEnd = swapEnd;
    session->swapWait = swapWait;
    session->swapSetLayer = swapSetLayer;
    session->getDefaultSurface = getDefaultSurface;
    session->powerSave = powerSave;
    session->presentLock = OS_UNFAIR_LOCK_INIT;
    /* Capture SpringBoard's CA surface for restore. Never paint into it. */
    if (getDefaultSurface) {
        IOSurfaceRef def = NULL;
        if (getDefaultSurface(display, 0, &def) == 0 && def) {
            session->defaultSurface = (IOSurfaceRef)CFRetain(def);
        }
    }
    /* Keep the panel out of idle power-save while we own scanout. */
    if (powerSave) {
        (void)powerSave(display, 0);
    }
    if (powerChange) {
        (void)powerChange(display, 1);
    }
    bool allocated = true;
    for (int i = 0; i < WWN_IOMFB_LAYER_COUNT; ++i) {
        session->surfaces[i] = make_surface(width, height);
        if (!session->surfaces[i]) {
            allocated = false;
        }
    }
    if (!allocated) {
        write_error(error, error_capacity, "IOMFB IOSurface allocation failed");
        return NULL;
    }

    session->device = MTLCreateSystemDefaultDevice();
    session->queue = [session->device newCommandQueue];
    if (session->device && session->queue) {
        for (int i = 0; i < WWN_IOMFB_LAYER_COUNT; ++i) {
            MTLTextureDescriptor *descriptor =
                [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                 width:width
                                                height:height
                                             mipmapped:NO];
            descriptor.storageMode = MTLStorageModeShared;
            descriptor.usage =
                MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            session->textures[i] =
                [session->device newTextureWithDescriptor:descriptor
                                                iosurface:session->surfaces[i]
                                                    plane:0];
        }
    }
    *out_width = width;
    *out_height = height;
    os_log(OS_LOG_DEFAULT,
           "wwn.iomfb op=open result=ok width=%{public}u height=%{public}u "
           "metal=%{public}d swapWait=%{public}d defaultSurface=%{public}d "
           "buffers=%d",
           width, height, session->device != nil,
           swapWait != NULL, session->defaultSurface != NULL,
           WWN_IOMFB_LAYER_COUNT);
    return (void *)session;
}

int32_t wwn_iomfb_platform_acquire(
    void *opaque,
    uint32_t index,
    WwnIomfbSurface *out_surface) {
    if (!opaque || !out_surface || index >= WWN_IOMFB_LAYER_COUNT) {
        return EINVAL;
    }
    WWNIOMFBPlatformSession *session =
        (WWNIOMFBPlatformSession *)opaque;
    IOSurfaceRef surface = session->surfaces[index];
    if (!surface) return ENODEV;
    *out_surface = (WwnIomfbSurface){
        .iosurface = (void *)surface,
        .id = IOSurfaceGetID(surface),
        .width = (uint32_t)IOSurfaceGetWidth(surface),
        .height = (uint32_t)IOSurfaceGetHeight(surface),
        .bytes_per_row = (uint32_t)IOSurfaceGetBytesPerRow(surface),
    };
    return 0;
}

int32_t wwn_iomfb_platform_present_surface(
    void *opaque,
    void *surface_pointer,
    WwnIomfbDamage damage,
    uint64_t frame) {
    if (!opaque || !surface_pointer) return EINVAL;
    WWNIOMFBPlatformSession *session =
        (WWNIOMFBPlatformSession *)opaque;
    IOSurfaceRef surface = (IOSurfaceRef)surface_pointer;
    return present_surface(
        session, surface, damage, frame, "iosurface-direct", "zero");
}

int32_t wwn_iomfb_platform_present_texture(
    void *opaque,
    void *texture_pointer,
    WwnIomfbDamage damage,
    uint64_t frame,
    uint32_t destination_index) {
    if (!opaque || !texture_pointer ||
        destination_index >= WWN_IOMFB_LAYER_COUNT) {
        return EINVAL;
    }
    WWNIOMFBPlatformSession *session =
        (WWNIOMFBPlatformSession *)opaque;
    id<MTLTexture> source = (__bridge id<MTLTexture>)texture_pointer;
    if (source.iosurface) {
        return present_surface(
            session, source.iosurface, damage, frame, "metal-iosurface-direct", "zero");
    }
    if (!session->queue || !session->textures[destination_index] ||
        source.width != session->width || source.height != session->height ||
        source.pixelFormat != MTLPixelFormatBGRA8Unorm) {
        return ENOTSUP;
    }

    id<MTLCommandBuffer> commands = [session->queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    [blit copyFromTexture:source
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(session->width, session->height, 1)
                toTexture:session->textures[destination_index]
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [commands commit];
    [commands waitUntilCompleted];
    if (commands.status == MTLCommandBufferStatusError) return EIO;
    return present_surface(
        session,
        session->surfaces[destination_index],
        damage,
        frame,
        "metal-private-gpu-blit",
        "gpu");
}

int32_t wwn_iomfb_platform_set_exclusive(void *opaque, int32_t exclusive) {
    if (!opaque) return EINVAL;
    WWNIOMFBPlatformSession *session =
        (WWNIOMFBPlatformSession *)opaque;
    bool want = exclusive != 0;
    if (session->exclusive == want) {
        return 0;
    }
    session->exclusive = want;
    if (want) {
        if (!session->holdRunning) {
            session->holdRunning = true;
            if (pthread_create(&session->holdThread, NULL, wwn_iomfb_hold_main,
                               (__bridge void *)session) != 0) {
                session->holdRunning = false;
                session->exclusive = false;
                return EAGAIN;
            }
        }
        os_log(OS_LOG_DEFAULT, "wwn.iomfb op=exclusive result=on");
        return 0;
    }
    if (session->holdRunning) {
        pthread_join(session->holdThread, NULL);
        session->holdRunning = false;
    }
    os_log(OS_LOG_DEFAULT, "wwn.iomfb op=exclusive result=off");
    return 0;
}

int32_t wwn_iomfb_platform_restore(void *opaque) {
    if (!opaque) return EINVAL;
    WWNIOMFBPlatformSession *session =
        (WWNIOMFBPlatformSession *)opaque;
    if (session->restored) return 0;
    (void)wwn_iomfb_platform_set_exclusive(opaque, 0);
    if (session->powerSave) {
        (void)session->powerSave(session->display, 1);
    }
    int token = 0;
    IOMobileFramebufferReturn begin =
        session->swapBegin(session->display, &token);
    IOMobileFramebufferReturn set = 0;
    IOMobileFramebufferReturn end = 0;
    if (begin == 0) {
        IOSurfaceRef restoreSurface = session->defaultSurface;
        CGRect full = CGRectMake(0, 0, session->width, session->height);
        if (restoreSurface) {
            set = session->swapSetLayer(
                session->display, 0, restoreSurface, full, full, 0);
        } else {
            set = session->swapSetLayer(
                session->display, 0, NULL, CGRectZero, CGRectZero, 0);
        }
        end = session->swapEnd(session->display);
        if (end == 0 && session->swapWait) {
            (void)session->swapWait(
                session->display, token, WWN_IOMFB_WAIT_UNTIL_DISPLAYED);
        }
    }
    session->restored = true;
    os_log(OS_LOG_DEFAULT,
           "wwn.iomfb op=restore result=%{public}d/%{public}d/%{public}d",
           begin, set, end);
    if (begin != 0) return begin;
    if (set != 0) return set;
    return end;
}

void wwn_iomfb_platform_destroy(void *opaque) {
    wwn_iomfb_session_destroy((WWNIOMFBPlatformSession *)opaque);
}
