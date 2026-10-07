#include "DisplaySurface.h"

#include <IOSurface/IOSurfaceRef.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>

static const uint32_t s_bytesPerElement[] = {
    [kWSPixelFormatL8 - 1] = 1,
    [kWSPixelFormatL16 - 1] = 2,
    [kWSPixelFormatYUV422 - 1] = 2,
    [kWSPixelFormatBGRA - 1] = 4,
    [kWSPixelFormatARGB2101010 - 1] = 4,
    [kWSPixelFormatRGBA64 - 1] = 8,
    [kWSPixelFormatRGBAh - 1] = 8,
    [kWSPixelFormatRGBAf - 1] = 16,
    [kWSPixelFormatW30r - 1] = 4,
    [kWSPixelFormatW40a - 1] = 8,
    [kWSPixelFormatB3A8 - 1] = 5,
};

static const uint32_t s_iosurfaceFormat[] = {
    [kWSPixelFormatL8 - 1] = 'L008',
    [kWSPixelFormatL16 - 1] = 'L008',
    [kWSPixelFormatYUV422 - 1] = '2vuy',
    [kWSPixelFormatBGRA - 1] = 'BGRA',
    [kWSPixelFormatARGB2101010 - 1] = 'l10r',
    [kWSPixelFormatRGBA64 - 1] = 'l64r',
    [kWSPixelFormatRGBAh - 1] = 'RGhA',
    [kWSPixelFormatRGBAf - 1] = 'RGfA',
    [kWSPixelFormatW30r - 1] = 'w30r',
    [kWSPixelFormatW40a - 1] = 'w40a',
    [kWSPixelFormatB3A8 - 1] = 'b3a8',
};

static DisplaySurfaceInfo displaySurfaceCreate(uint32_t width, uint32_t height,
                                               WSPixelFormat fmt, int global)
{
    DisplaySurfaceInfo info = {0};
    if (fmt < 1 || fmt > 11)
        return info;

    uint32_t bpe = s_bytesPerElement[fmt - 1];
    uint32_t fcc = s_iosurfaceFormat[fmt - 1];

    CFNumberRef nw = CFNumberCreate(NULL, kCFNumberIntType, &width);
    CFNumberRef nh = CFNumberCreate(NULL, kCFNumberIntType, &height);
    CFNumberRef nbpe = CFNumberCreate(NULL, kCFNumberIntType, &bpe);
    CFNumberRef nfcc = CFNumberCreate(NULL, kCFNumberSInt32Type, &fcc);
    int cacheMode = 0x700;
    CFNumberRef ncache = CFNumberCreate(NULL, kCFNumberIntType, &cacheMode);

    const void *keys[] = {
        kIOSurfaceWidth,
        kIOSurfaceHeight,
        kIOSurfaceBytesPerElement,
        kIOSurfacePixelFormat,
        CFSTR("IOSurfaceCacheMode"),
    };
    const void *vals[] = {nw, nh, nbpe, nfcc, ncache};
    CFDictionaryRef props =
        CFDictionaryCreate(NULL, keys, vals, 5, &kCFTypeDictionaryKeyCallBacks,
                           &kCFTypeDictionaryValueCallBacks);
    CFRelease(nw);
    CFRelease(nh);
    CFRelease(nbpe);
    CFRelease(nfcc);
    CFRelease(ncache);

    if (global) {
        CFMutableDictionaryRef mut =
            CFDictionaryCreateMutableCopy(NULL, 0, props);
        CFRelease(props);
        props = mut;
        int yes = 1;
        CFNumberRef ng = CFNumberCreate(NULL, kCFNumberIntType, &yes);
        CFDictionarySetValue(mut, CFSTR("IOSurfaceIsGlobal"), ng);
        CFRelease(ng);
    }

    IOSurfaceRef surf = IOSurfaceCreate(props);
    CFRelease(props);
    if (!surf)
        return info;

    info.surface = surf;
    info.width = width;
    info.height = height;
    info.pixelFormat = fcc;
    info.bytesPerElement = bpe;
    info.wsFormat = fmt;
    return info;
}

DisplaySurfaceInfo DisplaySurface_create(uint32_t width, uint32_t height, WSPixelFormat fmt)
{
    return displaySurfaceCreate(width, height, fmt, 0);
}

DisplaySurfaceInfo DisplaySurface_create_global(uint32_t width, uint32_t height,
                                                WSPixelFormat fmt)
{
    return displaySurfaceCreate(width, height, fmt, 1);
}

void DisplaySurface_destroy(DisplaySurfaceInfo *info)
{
    if (!info)
        return;
    if (info->surface) {
        CFRelease(info->surface);
        info->surface = NULL;
    }
    memset(info, 0, sizeof(*info));
}
