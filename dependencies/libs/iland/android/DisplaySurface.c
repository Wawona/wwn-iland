#include "DisplaySurface.h"

#include <android/hardware_buffer.h>
#include <string.h>

static uint32_t bytes_per_element(WSPixelFormat fmt)
{
    switch (fmt) {
    case kWSPixelFormatL8:
        return 1;
    case kWSPixelFormatL16:
        return 2;
    case kWSPixelFormatYUV422:
        return 2;
    default:
        return 4;
    }
}

/* Map WSPixelFormat → real AHB format. Unknown → 0 (reject). */
static uint32_t ahb_format_for_ws(WSPixelFormat fmt)
{
    switch (fmt) {
    case kWSPixelFormatBGRA:
        return AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM;
    case kWSPixelFormatARGB2101010:
        return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
    default:
        /* L8/L16/YUV/etc. are not GBM scanout formats on Android yet. */
        return 0;
    }
}

DisplaySurfaceInfo DisplaySurface_create(uint32_t width, uint32_t height, WSPixelFormat fmt)
{
    DisplaySurfaceInfo info = { 0 };
    uint32_t ahb_format = ahb_format_for_ws(fmt);
    if (ahb_format == 0)
        return info;

    uint32_t bpe = bytes_per_element(fmt);
    IOSurfaceRef surf = ILandIOSurfaceCreate(width, height, bpe, ahb_format);
    if (!surf)
        return info;

    info.surface = surf;
    info.width = width;
    info.height = height;
    info.pixelFormat = (fmt == kWSPixelFormatARGB2101010)
                           ? 0x7230316c /* l10r */
                           : 0x41524742; /* BGRA */
    info.bytesPerElement = bpe;
    info.wsFormat = fmt;
    return info;
}

DisplaySurfaceInfo DisplaySurface_create_global(uint32_t width, uint32_t height,
                                                WSPixelFormat fmt)
{
    return DisplaySurface_create(width, height, fmt);
}

void DisplaySurface_destroy(DisplaySurfaceInfo *info)
{
    if (!info)
        return;
    if (info->surface)
        ILandIOSurfaceRelease(info->surface);
    memset(info, 0, sizeof(*info));
}
