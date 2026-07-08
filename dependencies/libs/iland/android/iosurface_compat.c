#include "iosurface_compat.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct ILandIOSurface {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t bpe;
    uint32_t stride;
    size_t alloc_size;
    uint8_t *data;
    int refcount;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_next_id = 1;

IOSurfaceRef ILandIOSurfaceCreate(uint32_t width, uint32_t height, uint32_t bpe)
{
    if (width == 0 || height == 0 || bpe == 0)
        return NULL;

    IOSurfaceRef surf = calloc(1, sizeof(*surf));
    if (!surf)
        return NULL;

    surf->width = width;
    surf->height = height;
    surf->bpe = bpe;
    surf->stride = width * bpe;
    surf->alloc_size = (size_t)surf->stride * height;
    surf->data = calloc(1, surf->alloc_size);
    if (!surf->data) {
        free(surf);
        return NULL;
    }

    pthread_mutex_lock(&g_lock);
    surf->id = g_next_id++;
    pthread_mutex_unlock(&g_lock);
    surf->refcount = 1;
    return surf;
}

void ILandIOSurfaceRetain(IOSurfaceRef surf)
{
    if (!surf)
        return;
    surf->refcount++;
}

void ILandIOSurfaceRelease(IOSurfaceRef surf)
{
    if (!surf)
        return;
    if (--surf->refcount > 0)
        return;
    free(surf->data);
    free(surf);
}

uint32_t ILandIOSurfaceGetID(IOSurfaceRef surf)
{
    return surf ? surf->id : 0;
}

uint32_t ILandIOSurfaceGetWidth(IOSurfaceRef surf)
{
    return surf ? surf->width : 0;
}

uint32_t ILandIOSurfaceGetHeight(IOSurfaceRef surf)
{
    return surf ? surf->height : 0;
}

size_t ILandIOSurfaceGetBytesPerRow(IOSurfaceRef surf)
{
    return surf ? surf->stride : 0;
}

size_t ILandIOSurfaceGetAllocSize(IOSurfaceRef surf)
{
    return surf ? surf->alloc_size : 0;
}

void *ILandIOSurfaceGetBaseAddress(IOSurfaceRef surf)
{
    return surf ? surf->data : NULL;
}

void ILandIOSurfaceLock(IOSurfaceRef surf)
{
    (void)surf;
}

void ILandIOSurfaceUnlock(IOSurfaceRef surf)
{
    (void)surf;
}
