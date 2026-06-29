#include "DisplaySurface.h"

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

DisplaySurfaceInfo DisplaySurface_create(uint32_t width, uint32_t height, WSPixelFormat fmt)
{
    DisplaySurfaceInfo info = { 0 };
    if (fmt < 1 || fmt > 11)
        return info;

    uint32_t bpe = bytes_per_element(fmt);
    IOSurfaceRef surf = ILandIOSurfaceCreate(width, height, bpe);
    if (!surf)
        return info;

    info.surface = surf;
    info.width = width;
    info.height = height;
    info.pixelFormat = 0x41524742; /* BGRA */
    info.bytesPerElement = bpe;
    info.wsFormat = fmt;
    return info;
}

void DisplaySurface_destroy(DisplaySurfaceInfo *info)
{
    if (!info)
        return;
    if (info->surface)
        ILandIOSurfaceRelease(info->surface);
    memset(info, 0, sizeof(*info));
}
