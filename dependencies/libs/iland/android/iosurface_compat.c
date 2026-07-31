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
#define ILAND_IO_REGISTRY_CAP 256
static IOSurfaceRef g_registry[ILAND_IO_REGISTRY_CAP];

static void registry_put(IOSurfaceRef surf)
{
    if (!surf || surf->id == 0)
        return;
    uint32_t slot = surf->id % ILAND_IO_REGISTRY_CAP;
    /* Linear probe within the table; ids are dense so collisions are rare. */
    for (uint32_t i = 0; i < ILAND_IO_REGISTRY_CAP; i++) {
        uint32_t idx = (slot + i) % ILAND_IO_REGISTRY_CAP;
        if (!g_registry[idx] || g_registry[idx] == surf) {
            g_registry[idx] = surf;
            return;
        }
    }
}

static void registry_remove(IOSurfaceRef surf)
{
    if (!surf || surf->id == 0)
        return;
    for (uint32_t i = 0; i < ILAND_IO_REGISTRY_CAP; i++) {
        if (g_registry[i] == surf) {
            g_registry[i] = NULL;
            return;
        }
    }
}

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
    if (surf->id == 0)
        surf->id = g_next_id++;
    registry_put(surf);
    pthread_mutex_unlock(&g_lock);
    surf->refcount = 1;
    return surf;
}

IOSurfaceRef ILandIOSurfaceLookup(uint32_t id)
{
    if (id == 0)
        return NULL;
    pthread_mutex_lock(&g_lock);
    IOSurfaceRef found = NULL;
    for (uint32_t i = 0; i < ILAND_IO_REGISTRY_CAP; i++) {
        if (g_registry[i] && g_registry[i]->id == id) {
            found = g_registry[i];
            found->refcount++;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return found;
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
    pthread_mutex_lock(&g_lock);
    if (--surf->refcount > 0) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    registry_remove(surf);
    pthread_mutex_unlock(&g_lock);
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

int ILandIOSurfaceLock(IOSurfaceRef surf)
{
    if (!surf)
        return -1;
    if (surf->mapped_data)
        return 0;
    void *address = NULL;
    if (AHardwareBuffer_lock(surf->hardware_buffer,
                             AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
                                 AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                             -1, NULL, &address) != 0)
        return -1;
    surf->mapped_data = address;
    return 0;
}

int ILandIOSurfaceUnlock(IOSurfaceRef surf)
{
    if (!surf || !surf->mapped_data)
        return 0;
    AHardwareBuffer_unlock(surf->hardware_buffer, NULL);
    surf->mapped_data = NULL;
    return 0;
}
