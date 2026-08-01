#include "gbm_priv.h"

#include "DisplaySurface.h"
#include "drm_fourcc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void drm_register_gbm_buffer(uint32_t handle, void *surface,
                                    uint32_t format);
extern void drm_unregister_gbm_buffer(uint32_t handle);

static WSPixelFormat iosurface_format_for_drm(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_ARGB8888:
        return kWSPixelFormatBGRA;
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_ARGB2101010:
        return kWSPixelFormatARGB2101010;
    default:
        return 0;
    }
}

static IOSurfaceRef create_iosurface(uint32_t width, uint32_t height, uint32_t format)
{
    WSPixelFormat ws_format = iosurface_format_for_drm(format);
    if (ws_format == 0) {
        errno = EINVAL;
        return NULL;
    }
    DisplaySurfaceInfo dsi = DisplaySurface_create(width, height, ws_format);
    return dsi.surface;
}

struct gbm_device *gbm_create_device(int fd)
{
    struct gbm_device *dev = calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;
    dev->fd = fd;
    dev->refcount = 1;
    return dev;
}

void gbm_device_destroy(struct gbm_device *gbm)
{
    if (!gbm)
        return;
    if (--gbm->refcount > 0)
        return;
    free(gbm);
}

const char *gbm_device_get_backend_name(struct gbm_device *gbm)
{
    return gbm ? "iland-ahb" : NULL;
}

int gbm_device_is_format_supported(struct gbm_device *gbm,
                                   uint32_t format, uint32_t usage)
{
    if (!gbm)
        return 0;
    const uint32_t supported_usage =
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING | GBM_BO_USE_WRITE |
        GBM_BO_USE_LINEAR | GBM_BO_USE_CURSOR_64X64;
    return iosurface_format_for_drm(format) != 0 &&
           (usage & ~supported_usage) == 0;
}

struct gbm_surface *gbm_surface_create(
    struct gbm_device *gbm,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t flags)
{
    struct gbm_surface *surf = calloc(1, sizeof(*surf));
    if (!surf)
        return NULL;

    surf->device = gbm;
    surf->width = width;
    surf->height = height;
    surf->format = format;
    surf->flags = flags;

    for (int i = 0; i < GBM_NUM_BUFFERS; i++) {
        struct gbm_bo *bo = calloc(1, sizeof(*bo));
        if (!bo)
            goto fail;
        bo->device = gbm;
        bo->width = width;
        bo->height = height;
        bo->surface = create_iosurface(width, height, format);
        if (!bo->surface) {
            free(bo);
            goto fail;
        }
        bo->stride = (uint32_t)IOSurfaceGetBytesPerRow(bo->surface);
        bo->format = format;
        drm_register_gbm_buffer((uint32_t)IOSurfaceGetID(bo->surface),
                                (void *)bo->surface, format);
        surf->bos[i] = bo;
    }
    return surf;

fail:
    for (int i = 0; i < GBM_NUM_BUFFERS; i++) {
        if (surf->bos[i]) {
            if (surf->bos[i]->surface) {
                drm_unregister_gbm_buffer((uint32_t)IOSurfaceGetID(surf->bos[i]->surface));
                ILandIOSurfaceRelease(surf->bos[i]->surface);
            }
            free(surf->bos[i]);
        }
    }
    free(surf);
    return NULL;
}

void gbm_surface_destroy(struct gbm_surface *surface)
{
    if (!surface)
        return;
    for (int i = 0; i < GBM_NUM_BUFFERS; i++) {
        if (surface->bos[i]) {
            if (surface->bos[i]->surface) {
                drm_unregister_gbm_buffer((uint32_t)IOSurfaceGetID(surface->bos[i]->surface));
                ILandIOSurfaceRelease(surface->bos[i]->surface);
            }
            free(surface->bos[i]);
        }
    }
    free(surface);
}

struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surface)
{
    if (!surface || surface->count == 0)
        return NULL;
    struct gbm_bo *bo = surface->bos[surface->read_idx];
    surface->read_idx = (surface->read_idx + 1) % GBM_NUM_BUFFERS;
    surface->count--;
    return bo;
}

void gbm_surface_release_buffer(struct gbm_surface *surface, struct gbm_bo *bo)
{
    (void)surface;
    (void)bo;
}

IOSurfaceRef gbm_bo_get_iosurface(struct gbm_bo *bo)
{
    return bo ? bo->surface : NULL;
}

struct gbm_bo *gbm_surface_get_write_bo(struct gbm_surface *surface)
{
    return surface ? surface->bos[surface->write_idx] : NULL;
}

void gbm_surface_advance_write(struct gbm_surface *surface)
{
    if (!surface)
        return;
    surface->write_idx = (surface->write_idx + 1) % GBM_NUM_BUFFERS;
    surface->count++;
}

uint32_t gbm_bo_get_width(struct gbm_bo *bo)
{
    return bo ? bo->width : 0;
}

uint32_t gbm_bo_get_height(struct gbm_bo *bo)
{
    return bo ? bo->height : 0;
}

uint32_t gbm_bo_get_stride(struct gbm_bo *bo)
{
    return bo ? bo->stride : 0;
}

uint32_t gbm_bo_get_format(struct gbm_bo *bo)
{
    return bo ? bo->format : 0;
}

struct gbm_device *gbm_bo_get_device(struct gbm_bo *bo)
{
    return bo ? bo->device : NULL;
}

union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo)
{
    union gbm_bo_handle h = { 0 };
    if (bo && bo->surface)
        h.u32 = (uint32_t)IOSurfaceGetID(bo->surface);
    return h;
}

void gbm_bo_set_user_data(struct gbm_bo *bo, void *data, void (*destroy)(struct gbm_bo *, void *))
{
    if (!bo)
        return;
    bo->user_data = data;
    bo->destroy_user_data = destroy;
}

void *gbm_bo_get_user_data(struct gbm_bo *bo)
{
    return bo ? bo->user_data : NULL;
}

struct gbm_bo *gbm_bo_create(
    struct gbm_device *gbm,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t flags)
{
    (void)flags;
    struct gbm_bo *bo = calloc(1, sizeof(*bo));
    if (!bo)
        return NULL;
    bo->device = gbm;
    bo->width = width;
    bo->height = height;
    bo->surface = create_iosurface(width, height, format);
    if (!bo->surface) {
        free(bo);
        return NULL;
    }
    bo->stride = (uint32_t)IOSurfaceGetBytesPerRow(bo->surface);
    bo->format = format;
    drm_register_gbm_buffer((uint32_t)IOSurfaceGetID(bo->surface),
                            (void *)bo->surface, format);
    return bo;
}

struct gbm_bo *gbm_bo_create_with_modifiers(
    struct gbm_device *gbm,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    const uint64_t *modifiers,
    const unsigned int count)
{
    (void)modifiers;
    (void)count;
    return gbm_bo_create(gbm, width, height, format, 0);
}

struct gbm_bo *gbm_bo_create_with_modifiers2(
    struct gbm_device *gbm,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    const uint64_t *modifiers,
    uint32_t count,
    uint32_t flags)
{
    (void)modifiers;
    (void)count;
    (void)flags;
    return gbm_bo_create(gbm, width, height, format, 0);
}

void gbm_bo_destroy(struct gbm_bo *bo)
{
    if (!bo)
        return;
    if (bo->surface) {
        if (bo->map_count > 0)
            IOSurfaceUnlock(bo->surface, 0, NULL);
        drm_unregister_gbm_buffer((uint32_t)IOSurfaceGetID(bo->surface));
        ILandIOSurfaceRelease(bo->surface);
    }
    free(bo);
}

int gbm_bo_get_fd(struct gbm_bo *bo)
{
    /* Placeholder fd — compositor keys off the IOSurface/AHB id in the
     * modifier, same as Apple. Never a real dma-buf. */
    if (!bo || !bo->surface) {
        errno = EINVAL;
        return -1;
    }
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        errno = ENOSYS;
    return fd;
}

int gbm_bo_get_fd_for_plane(struct gbm_bo *bo, int plane)
{
    if (plane != 0) {
        errno = EINVAL;
        return -1;
    }
    return gbm_bo_get_fd(bo);
}

int gbm_bo_get_plane_count(struct gbm_bo *bo)
{
    (void)bo;
    return 1;
}

uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane)
{
    (void)plane;
    return bo ? bo->stride : 0;
}

uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane)
{
    (void)bo;
    (void)plane;
    return 0;
}

uint64_t gbm_bo_get_modifier(struct gbm_bo *bo)
{
    /* Wawona / wwn-waypipe IOSurface dmabuf convention (#86):
     * high bit marks IOSurface import; low 63 bits carry IOSurfaceGetID. */
    if (!bo || !bo->surface)
        return 0;
    uint64_t id = (uint64_t)IOSurfaceGetID(bo->surface);
    return 0x8000000000000000ULL | (id & 0x7fffffffffffffffULL);
}

int gbm_device_get_fd(struct gbm_device *gbm)
{
    return gbm ? gbm->fd : -1;
}

int gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count)
{
    if (!bo || !bo->surface) {
        errno = EINVAL;
        return -1;
    }
    IOSurfaceLock(bo->surface, 0, NULL);
    void *base = IOSurfaceGetBaseAddress(bo->surface);
    size_t avail = (size_t)bo->stride * bo->height;
    if (count > avail)
        count = avail;
    memcpy(base, buf, count);
    IOSurfaceUnlock(bo->surface, 0, NULL);
    return 0;
}

static uint32_t gbm_bo_bpp(struct gbm_bo *bo)
{
    /* 8888 and 2101010 are both 4 bytes/pixel (Apple map math). */
    (void)bo;
    return 4;
}

void *gbm_bo_map(struct gbm_bo *bo,
                 uint32_t x, uint32_t y,
                 uint32_t width, uint32_t height,
                 uint32_t flags, uint32_t *stride,
                 void **map_data)
{
    (void)flags;
    if (!bo || !bo->surface || x > bo->width || y > bo->height ||
        width > bo->width - x || height > bo->height - y) {
        errno = EINVAL;
        return NULL;
    }
    if (bo->map_count++ == 0)
        IOSurfaceLock(bo->surface, 0, NULL);
    void *base = IOSurfaceGetBaseAddress(bo->surface);
    if (!base) {
        if (--bo->map_count == 0)
            IOSurfaceUnlock(bo->surface, 0, NULL);
        errno = EIO;
        return NULL;
    }
    if (stride)
        *stride = bo->stride;
    if (map_data)
        *map_data = bo;
    return (uint8_t *)base + (size_t)y * bo->stride +
           (size_t)x * gbm_bo_bpp(bo);
}

void gbm_bo_unmap(struct gbm_bo *bo, void *map_data)
{
    if (!bo || map_data != bo || bo->map_count == 0)
        return;
    if (--bo->map_count == 0)
        IOSurfaceUnlock(bo->surface, 0, NULL);
}

struct gbm_bo *gbm_bo_import(struct gbm_device *gbm, uint32_t type, void *buffer, uint32_t usage)
{
    (void)usage;
    if (!gbm || type != GBM_BO_IMPORT_FD_MODIFIER || !buffer) {
        errno = EINVAL;
        return NULL;
    }
    struct gbm_import_fd_modifier_data *data = buffer;
    if (data->num_fds != 1 ||
        (data->modifier & 0x8000000000000000ULL) == 0) {
        errno = EINVAL;
        return NULL;
    }
    uint32_t surface_id =
        (uint32_t)(data->modifier & 0x7fffffffffffffffULL);
    IOSurfaceRef surface = ILandIOSurfaceLookup(surface_id);
    if (!surface) {
        errno = ENOENT;
        return NULL;
    }

    struct gbm_bo *bo = calloc(1, sizeof(*bo));
    if (!bo) {
        ILandIOSurfaceRelease(surface);
        errno = ENOMEM;
        return NULL;
    }
    bo->device = gbm;
    bo->width = data->width;
    bo->height = data->height;
    bo->stride = data->strides[0] > 0 ? (uint32_t)data->strides[0]
                                      : (uint32_t)IOSurfaceGetBytesPerRow(surface);
    bo->format = data->format;
    bo->surface = surface;
    drm_register_gbm_buffer(surface_id, (void *)surface, data->format);
    return bo;
}

struct gbm_surface *gbm_surface_create_with_modifiers(
    struct gbm_device *gbm,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    const uint64_t *modifiers,
    const unsigned int count)
{
    (void)modifiers;
    (void)count;
    return gbm_surface_create(gbm, width, height, format, 0);
}

union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane)
{
    (void)plane;
    return gbm_bo_get_handle(bo);
}
