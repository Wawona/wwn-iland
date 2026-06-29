#ifndef ILAND_DISPLAY_SURFACE_H
#define ILAND_DISPLAY_SURFACE_H

#include "iosurface_compat.h"
#include <stdint.h>

typedef enum WSPixelFormat {
    kWSPixelFormatL8 = 1,
    kWSPixelFormatL16 = 2,
    kWSPixelFormatYUV422 = 3,
    kWSPixelFormatBGRA = 4,
    kWSPixelFormatARGB2101010 = 5,
    kWSPixelFormatRGBA64 = 6,
    kWSPixelFormatRGBAh = 7,
    kWSPixelFormatRGBAf = 8,
    kWSPixelFormatW30r = 9,
    kWSPixelFormatW40a = 10,
    kWSPixelFormatB3A8 = 11,
} WSPixelFormat;

typedef struct DisplaySurfaceInfo {
    IOSurfaceRef surface;
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;
    uint32_t bytesPerElement;
    WSPixelFormat wsFormat;
} DisplaySurfaceInfo;

DisplaySurfaceInfo DisplaySurface_create(uint32_t width, uint32_t height, WSPixelFormat fmt);
void DisplaySurface_destroy(DisplaySurfaceInfo *info);

#endif
