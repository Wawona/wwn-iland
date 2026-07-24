#include "iosurface_compat.h"

#include <android/hardware_buffer.h>
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
    AHardwareBuffer *hardware_buffer;
    uint8_t *mapped_data;
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
    AHardwareBuffer_Desc desc = {
        .width = width,
        .height = height,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                 AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                 AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                 AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
        .stride = 0,
        .rfu0 = 0,
        .rfu1 = 0,
    };
    if (bpe != 4 ||
        AHardwareBuffer_allocate(&desc, &surf->hardware_buffer) != 0 ||
        !surf->hardware_buffer) {
        free(surf);
        return NULL;
    }
    AHardwareBuffer_describe(surf->hardware_buffer, &desc);
    surf->stride = desc.stride * bpe;
    surf->alloc_size = (size_t)surf->stride * height;

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
    AHardwareBuffer_release(surf->hardware_buffer);
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
    return surf ? surf->mapped_data : NULL;
}

AHardwareBuffer *ILandIOSurfaceGetHardwareBuffer(IOSurfaceRef surf)
{
    return surf ? surf->hardware_buffer : NULL;
}

void ILandIOSurfaceLock(IOSurfaceRef surf)
{
    if (!surf || surf->mapped_data)
        return;
    void *address = NULL;
    if (AHardwareBuffer_lock(surf->hardware_buffer,
                             AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                                 AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                             -1, NULL, &address) == 0)
        surf->mapped_data = address;
}

void ILandIOSurfaceUnlock(IOSurfaceRef surf)
{
    if (!surf || !surf->mapped_data)
        return;
    AHardwareBuffer_unlock(surf->hardware_buffer, NULL);
    surf->mapped_data = NULL;
}
