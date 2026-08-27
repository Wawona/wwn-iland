#include "iosurface_stub.h"

#include <stdlib.h>
#include <string.h>

struct ILandWatchSurface {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t bpe;
    uint32_t stride;
    size_t size;
    void *base;
    int refs;
};

static uint32_t g_next_id = 1;
static IOSurfaceRef g_table[256];

IOSurfaceRef ILandWatchSurfaceCreate(uint32_t width, uint32_t height, uint32_t bpe)
{
    if (width == 0 || height == 0 || bpe == 0)
        return NULL;
    struct ILandWatchSurface *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->width = width;
    s->height = height;
    s->bpe = bpe;
    s->stride = width * bpe;
    s->size = (size_t)s->stride * height;
    s->base = calloc(1, s->size);
    if (!s->base) {
        free(s);
        return NULL;
    }
    s->id = g_next_id++;
    s->refs = 1;
    if (s->id < 256)
        g_table[s->id] = s;
    return s;
}

IOSurfaceRef ILandWatchSurfaceLookup(uint32_t id)
{
    if (id == 0 || id >= 256)
        return NULL;
    IOSurfaceRef s = g_table[id];
    if (s)
        ILandWatchSurfaceRetain(s);
    return s;
}

void ILandWatchSurfaceRetain(IOSurfaceRef surf)
{
    if (surf)
        surf->refs++;
}

void ILandWatchSurfaceRelease(IOSurfaceRef surf)
{
    if (!surf)
        return;
    if (--surf->refs > 0)
        return;
    if (surf->id < 256 && g_table[surf->id] == surf)
        g_table[surf->id] = NULL;
    free(surf->base);
    free(surf);
}

uint32_t ILandWatchSurfaceGetID(IOSurfaceRef surf)
{
    return surf ? surf->id : 0;
}

uint32_t ILandWatchSurfaceGetWidth(IOSurfaceRef surf)
{
    return surf ? surf->width : 0;
}

uint32_t ILandWatchSurfaceGetHeight(IOSurfaceRef surf)
{
    return surf ? surf->height : 0;
}

size_t ILandWatchSurfaceGetBytesPerRow(IOSurfaceRef surf)
{
    return surf ? surf->stride : 0;
}

size_t ILandWatchSurfaceGetAllocSize(IOSurfaceRef surf)
{
    return surf ? surf->size : 0;
}

void *ILandWatchSurfaceGetBaseAddress(IOSurfaceRef surf)
{
    return surf ? surf->base : NULL;
}

int ILandWatchSurfaceLock(IOSurfaceRef surf)
{
    (void)surf;
    return 0;
}

int ILandWatchSurfaceUnlock(IOSurfaceRef surf)
{
    (void)surf;
    return 0;
}
