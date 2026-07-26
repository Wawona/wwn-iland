/*
 * Minimal VK_KHR_wayland_surface + VK_KHR_swapchain over iland's IOSurface
 * dmabuf winsys. See iland_vk_wayland.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#include "iland_vk_wayland.h"
#include "iland_wl_winsys.h"

#define ILAND_VK_WL_MAGIC 0x49564b57u /* 'IVKW' */
#define ILAND_VK_WL_MAX_IMAGES 3

typedef struct {
    uint32_t magic;
    struct wl_display *display;
    struct wl_surface *surface;
} IlandVkWlSurface;

typedef struct {
    uint32_t magic;
    IlandVkWlSurface *surface;
    IlandWlWinsys *winsys;
    IlandWlSwapchain *swapchain;
    VkDevice device;
    VkFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t image_count;
    VkImage images[ILAND_VK_WL_MAX_IMAGES];
    VkDeviceMemory memories[ILAND_VK_WL_MAX_IMAGES];
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    void *staging_map;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkQueue queue;
    uint32_t queue_family;
    uint32_t current_index;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkCreateImage CreateImage;
    PFN_vkDestroyImage DestroyImage;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements;
    PFN_vkAllocateMemory AllocateMemory;
    PFN_vkFreeMemory FreeMemory;
    PFN_vkBindImageMemory BindImageMemory;
    PFN_vkCreateBuffer CreateBuffer;
    PFN_vkDestroyBuffer DestroyBuffer;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements;
    PFN_vkBindBufferMemory BindBufferMemory;
    PFN_vkMapMemory MapMemory;
    PFN_vkUnmapMemory UnmapMemory;
    PFN_vkCreateCommandPool CreateCommandPool;
    PFN_vkDestroyCommandPool DestroyCommandPool;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers;
    PFN_vkBeginCommandBuffer BeginCommandBuffer;
    PFN_vkEndCommandBuffer EndCommandBuffer;
    PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier;
    PFN_vkQueueSubmit QueueSubmit;
    PFN_vkQueueWaitIdle QueueWaitIdle;
    PFN_vkDeviceWaitIdle DeviceWaitIdle;
    PFN_vkGetDeviceQueue GetDeviceQueue;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceMemoryProperties mem_props;
} IlandVkWlSwapchain;

static PFN_vkGetInstanceProcAddr g_real_gipa;

int iland_vk_wayland_present_bgra(struct IlandWlSwapchain *sc, const void *pixels,
                                  uint32_t stride_bytes)
{
    return iland_wl_swapchain_present_pixels(sc, pixels, stride_bytes);
}

static IlandVkWlSurface *as_surface(VkSurfaceKHR surface)
{
    IlandVkWlSurface *s = (IlandVkWlSurface *)(uintptr_t)surface;
    if (!s || s->magic != ILAND_VK_WL_MAGIC)
        return NULL;
    return s;
}

static IlandVkWlSwapchain *as_swapchain(VkSwapchainKHR swapchain)
{
    IlandVkWlSwapchain *sc = (IlandVkWlSwapchain *)(uintptr_t)swapchain;
    if (!sc || sc->magic != ILAND_VK_WL_MAGIC)
        return NULL;
    return sc;
}

static int find_memory_type(const VkPhysicalDeviceMemoryProperties *props,
                            uint32_t type_bits, VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < props->memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (props->memoryTypes[i].propertyFlags & flags) == flags)
            return (int)i;
    }
    return -1;
}

static VkResult VKAPI_CALL iland_CreateWaylandSurfaceKHR(
    VkInstance instance, const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
    (void)instance;
    (void)pAllocator;
    if (!pCreateInfo || !pSurface || !pCreateInfo->display ||
        !pCreateInfo->surface)
        return VK_ERROR_INITIALIZATION_FAILED;

    IlandVkWlSurface *s = calloc(1, sizeof(*s));
    if (!s)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    s->magic = ILAND_VK_WL_MAGIC;
    s->display = pCreateInfo->display;
    s->surface = pCreateInfo->surface;
    *pSurface = (VkSurfaceKHR)(uintptr_t)s;
    return VK_SUCCESS;
}

static void VKAPI_CALL iland_DestroySurfaceKHR(
    VkInstance instance, VkSurfaceKHR surface,
    const VkAllocationCallbacks *pAllocator)
{
    (void)instance;
    (void)pAllocator;
    IlandVkWlSurface *s = as_surface(surface);
    if (s) {
        s->magic = 0;
        free(s);
    }
}

static VkBool32 VKAPI_CALL iland_GetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    struct wl_display *display)
{
    (void)physicalDevice;
    (void)queueFamilyIndex;
    return display != NULL ? VK_TRUE : VK_FALSE;
}

static VkResult VKAPI_CALL iland_GetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    VkSurfaceKHR surface, VkBool32 *pSupported)
{
    (void)physicalDevice;
    (void)queueFamilyIndex;
    if (!pSupported)
        return VK_ERROR_INITIALIZATION_FAILED;
    *pSupported = as_surface(surface) ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL iland_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
    (void)physicalDevice;
    IlandVkWlSurface *s = as_surface(surface);
    if (!s || !pSurfaceCapabilities)
        return VK_ERROR_SURFACE_LOST_KHR;

    memset(pSurfaceCapabilities, 0, sizeof(*pSurfaceCapabilities));
    pSurfaceCapabilities->minImageCount = 2;
    pSurfaceCapabilities->maxImageCount = ILAND_VK_WL_MAX_IMAGES;
    pSurfaceCapabilities->currentExtent.width = 0xFFFFFFFF;
    pSurfaceCapabilities->currentExtent.height = 0xFFFFFFFF;
    pSurfaceCapabilities->minImageExtent.width = 1;
    pSurfaceCapabilities->minImageExtent.height = 1;
    pSurfaceCapabilities->maxImageExtent.width = 16384;
    pSurfaceCapabilities->maxImageExtent.height = 16384;
    pSurfaceCapabilities->maxImageArrayLayers = 1;
    pSurfaceCapabilities->supportedTransforms =
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->currentTransform =
        VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    pSurfaceCapabilities->supportedCompositeAlpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    pSurfaceCapabilities->supportedUsageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL iland_GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats)
{
    (void)physicalDevice;
    if (!as_surface(surface) || !pSurfaceFormatCount)
        return VK_ERROR_SURFACE_LOST_KHR;

    const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = 2;
        return VK_SUCCESS;
    }
    uint32_t n = *pSurfaceFormatCount < 2 ? *pSurfaceFormatCount : 2;
    memcpy(pSurfaceFormats, formats, n * sizeof(formats[0]));
    *pSurfaceFormatCount = n;
    return n < 2 ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult VKAPI_CALL iland_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes)
{
    (void)physicalDevice;
    if (!as_surface(surface) || !pPresentModeCount)
        return VK_ERROR_SURFACE_LOST_KHR;
    const VkPresentModeKHR modes[] = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
    };
    if (!pPresentModes) {
        *pPresentModeCount = 3;
        return VK_SUCCESS;
    }
    uint32_t n = *pPresentModeCount < 3 ? *pPresentModeCount : 3;
    memcpy(pPresentModes, modes, n * sizeof(modes[0]));
    *pPresentModeCount = n;
    return n < 3 ? VK_INCOMPLETE : VK_SUCCESS;
}

static void destroy_swapchain_resources(IlandVkWlSwapchain *sc)
{
    if (!sc || !sc->device)
        return;
    if (sc->DeviceWaitIdle)
        sc->DeviceWaitIdle(sc->device);
    if (sc->staging_map && sc->UnmapMemory)
        sc->UnmapMemory(sc->device, sc->staging_memory);
    sc->staging_map = NULL;
    if (sc->staging && sc->DestroyBuffer)
        sc->DestroyBuffer(sc->device, sc->staging, NULL);
    sc->staging = VK_NULL_HANDLE;
    if (sc->staging_memory && sc->FreeMemory)
        sc->FreeMemory(sc->device, sc->staging_memory, NULL);
    sc->staging_memory = VK_NULL_HANDLE;
    if (sc->command_pool && sc->DestroyCommandPool)
        sc->DestroyCommandPool(sc->device, sc->command_pool, NULL);
    sc->command_pool = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < sc->image_count; i++) {
        if (sc->images[i] && sc->DestroyImage)
            sc->DestroyImage(sc->device, sc->images[i], NULL);
        sc->images[i] = VK_NULL_HANDLE;
        if (sc->memories[i] && sc->FreeMemory)
            sc->FreeMemory(sc->device, sc->memories[i], NULL);
        sc->memories[i] = VK_NULL_HANDLE;
    }
    if (sc->swapchain) {
        iland_wl_swapchain_destroy(sc->swapchain);
        sc->swapchain = NULL;
    }
    if (sc->winsys) {
        iland_wl_winsys_destroy(sc->winsys);
        sc->winsys = NULL;
    }
}

static int load_device_fns(IlandVkWlSwapchain *sc, VkDevice device,
                           PFN_vkGetDeviceProcAddr gdpa)
{
    sc->gdpa = gdpa;
#define LOAD(name)                                                             \
    sc->name = (PFN_vk##name)gdpa(device, "vk" #name);                         \
    if (!sc->name)                                                             \
        return -1;
    LOAD(CreateImage);
    LOAD(DestroyImage);
    LOAD(GetImageMemoryRequirements);
    LOAD(AllocateMemory);
    LOAD(FreeMemory);
    LOAD(BindImageMemory);
    LOAD(CreateBuffer);
    LOAD(DestroyBuffer);
    LOAD(GetBufferMemoryRequirements);
    LOAD(BindBufferMemory);
    LOAD(MapMemory);
    LOAD(UnmapMemory);
    LOAD(CreateCommandPool);
    LOAD(DestroyCommandPool);
    LOAD(AllocateCommandBuffers);
    LOAD(BeginCommandBuffer);
    LOAD(EndCommandBuffer);
    LOAD(CmdCopyImageToBuffer);
    LOAD(CmdPipelineBarrier);
    LOAD(QueueSubmit);
    LOAD(QueueWaitIdle);
    LOAD(DeviceWaitIdle);
    LOAD(GetDeviceQueue);
#undef LOAD
    return 0;
}

static VkResult VKAPI_CALL iland_CreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain)
{
    (void)pAllocator;
    if (!pCreateInfo || !pSwapchain)
        return VK_ERROR_INITIALIZATION_FAILED;

    IlandVkWlSurface *surf = as_surface(pCreateInfo->surface);
    if (!surf)
        return VK_ERROR_SURFACE_LOST_KHR;

    if (!g_real_gipa)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetDeviceProcAddr gdpa =
        (PFN_vkGetDeviceProcAddr)g_real_gipa(VK_NULL_HANDLE,
                                             "vkGetDeviceProcAddr");
    /* Device GIPA usually needs a real instance; try device-level via
     * vkGetDeviceProcAddr from the ICD after CreateDevice — clients pass
     * device functions through our wrap of GetDeviceProcAddr below. */
    if (!gdpa) {
        /* Fall back: some ICDs export it globally. */
        gdpa = (PFN_vkGetDeviceProcAddr)g_real_gipa(
            VK_NULL_HANDLE, "vk_icdGetInstanceProcAddr");
    }

    IlandVkWlSwapchain *sc = calloc(1, sizeof(*sc));
    if (!sc)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    sc->magic = ILAND_VK_WL_MAGIC;
    sc->surface = surf;
    sc->device = device;
    sc->format = pCreateInfo->imageFormat;
    sc->width = pCreateInfo->imageExtent.width;
    sc->height = pCreateInfo->imageExtent.height;
    sc->image_count = pCreateInfo->minImageCount;
    if (sc->image_count < 2)
        sc->image_count = 2;
    if (sc->image_count > ILAND_VK_WL_MAX_IMAGES)
        sc->image_count = ILAND_VK_WL_MAX_IMAGES;
    if (sc->width == 0 || sc->height == 0) {
        free(sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    sc->winsys = iland_wl_winsys_create(surf->display);
    if (!sc->winsys) {
        free(sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    sc->swapchain = iland_wl_swapchain_create_for_surface(
        sc->winsys, surf->surface, sc->width, sc->height,
        ILAND_WL_SWAPCHAIN_TOP_DOWN);
    if (!sc->swapchain) {
        destroy_swapchain_resources(sc);
        free(sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    /* Device function table: require the client to have installed our wrap;
     * resolve via a stored gdpa set by CreateSwapchain's caller through
     * iland_vkGetDeviceProcAddr path. Stored on first GetDeviceProcAddr. */
    extern PFN_vkGetDeviceProcAddr iland_vk_wayland_current_gdpa(void);
    gdpa = iland_vk_wayland_current_gdpa();
    if (!gdpa || load_device_fns(sc, device, gdpa) != 0) {
        fprintf(stderr,
                "iland-vk-wl: CreateSwapchainKHR needs wrapped "
                "vkGetDeviceProcAddr\n");
        destroy_swapchain_resources(sc);
        free(sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    sc->GetDeviceQueue(device, 0, 0, &sc->queue);
    sc->queue_family = 0;

    for (uint32_t i = 0; i < sc->image_count; i++) {
        const VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = sc->format,
            .extent = {sc->width, sc->height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = pCreateInfo->imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        if (sc->CreateImage(device, &image_info, NULL, &sc->images[i]) !=
            VK_SUCCESS) {
            destroy_swapchain_resources(sc);
            free(sc);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        VkMemoryRequirements req;
        sc->GetImageMemoryRequirements(device, sc->images[i], &req);
        /* Memory props: ask ICD through gipa if we have a physical device.
         * Without it, try device-local then any. */
        int mem_type = -1;
        if (sc->mem_props.memoryTypeCount)
            mem_type = find_memory_type(&sc->mem_props, req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (mem_type < 0) {
            for (uint32_t b = 0; b < 32; b++) {
                if (req.memoryTypeBits & (1u << b)) {
                    mem_type = (int)b;
                    break;
                }
            }
        }
        if (mem_type < 0) {
            destroy_swapchain_resources(sc);
            free(sc);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        const VkMemoryAllocateInfo alloc = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size,
            .memoryTypeIndex = (uint32_t)mem_type,
        };
        if (sc->AllocateMemory(device, &alloc, NULL, &sc->memories[i]) !=
                VK_SUCCESS ||
            sc->BindImageMemory(device, sc->images[i], sc->memories[i], 0) !=
                VK_SUCCESS) {
            destroy_swapchain_resources(sc);
            free(sc);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
    }

    VkDeviceSize staging_size =
        (VkDeviceSize)sc->width * (VkDeviceSize)sc->height * 4u;
    const VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = staging_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (sc->CreateBuffer(device, &buf_info, NULL, &sc->staging) != VK_SUCCESS) {
        destroy_swapchain_resources(sc);
        free(sc);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    VkMemoryRequirements breq;
    sc->GetBufferMemoryRequirements(device, sc->staging, &breq);
    int btype = -1;
    if (sc->mem_props.memoryTypeCount)
        btype = find_memory_type(&sc->mem_props, breq.memoryTypeBits,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (btype < 0) {
        for (uint32_t b = 0; b < 32; b++) {
            if (breq.memoryTypeBits & (1u << b)) {
                btype = (int)b;
                break;
            }
        }
    }
    const VkMemoryAllocateInfo balloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = breq.size,
        .memoryTypeIndex = (uint32_t)(btype < 0 ? 0 : btype),
    };
    if (sc->AllocateMemory(device, &balloc, NULL, &sc->staging_memory) !=
            VK_SUCCESS ||
        sc->BindBufferMemory(device, sc->staging, sc->staging_memory, 0) !=
            VK_SUCCESS ||
        sc->MapMemory(device, sc->staging_memory, 0, staging_size, 0,
                      &sc->staging_map) != VK_SUCCESS) {
        destroy_swapchain_resources(sc);
        free(sc);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = sc->queue_family,
    };
    if (sc->CreateCommandPool(device, &pool_info, NULL, &sc->command_pool) !=
        VK_SUCCESS) {
        destroy_swapchain_resources(sc);
        free(sc);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    const VkCommandBufferAllocateInfo cmd_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = sc->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (sc->AllocateCommandBuffers(device, &cmd_info, &sc->command_buffer) !=
        VK_SUCCESS) {
        destroy_swapchain_resources(sc);
        free(sc);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    *pSwapchain = (VkSwapchainKHR)(uintptr_t)sc;
    return VK_SUCCESS;
}

static void VKAPI_CALL iland_DestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain,
    const VkAllocationCallbacks *pAllocator)
{
    (void)device;
    (void)pAllocator;
    IlandVkWlSwapchain *sc = as_swapchain(swapchain);
    if (!sc)
        return;
    destroy_swapchain_resources(sc);
    sc->magic = 0;
    free(sc);
}

static VkResult VKAPI_CALL iland_GetSwapchainImagesKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount,
    VkImage *pSwapchainImages)
{
    (void)device;
    IlandVkWlSwapchain *sc = as_swapchain(swapchain);
    if (!sc || !pSwapchainImageCount)
        return VK_ERROR_SURFACE_LOST_KHR;
    if (!pSwapchainImages) {
        *pSwapchainImageCount = sc->image_count;
        return VK_SUCCESS;
    }
    uint32_t n = *pSwapchainImageCount < sc->image_count ? *pSwapchainImageCount
                                                         : sc->image_count;
    for (uint32_t i = 0; i < n; i++)
        pSwapchainImages[i] = sc->images[i];
    *pSwapchainImageCount = n;
    return n < sc->image_count ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult VKAPI_CALL iland_AcquireNextImageKHR(
    VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
    VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex)
{
    (void)device;
    (void)timeout;
    (void)semaphore;
    (void)fence;
    IlandVkWlSwapchain *sc = as_swapchain(swapchain);
    if (!sc || !pImageIndex)
        return VK_ERROR_SURFACE_LOST_KHR;
    *pImageIndex = sc->current_index % sc->image_count;
    sc->current_index = (*pImageIndex + 1) % sc->image_count;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL iland_QueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    if (!pPresentInfo)
        return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
        IlandVkWlSwapchain *sc = as_swapchain(pPresentInfo->pSwapchains[i]);
        if (!sc) {
            if (pPresentInfo->pResults)
                pPresentInfo->pResults[i] = VK_ERROR_SURFACE_LOST_KHR;
            continue;
        }
        uint32_t idx = pPresentInfo->pImageIndices[i];
        if (idx >= sc->image_count) {
            if (pPresentInfo->pResults)
                pPresentInfo->pResults[i] = VK_ERROR_OUT_OF_DATE_KHR;
            continue;
        }

        /* Ensure colour attachment writes are visible, then copy to staging. */
        const VkCommandBufferBeginInfo begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        sc->BeginCommandBuffer(sc->command_buffer, &begin);
        const VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sc->images[idx],
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        };
        /* Clients may leave the image in COLOR_ATTACHMENT_OPTIMAL or
         * PRESENT_SRC; accept either by using UNDEFINED→TRANSFER when unsure
         * — MoltenVK is lenient. Prefer GENERAL as oldLayout for portability. */
        VkImageMemoryBarrier b = barrier;
        b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        sc->CmdPipelineBarrier(sc->command_buffer,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                               NULL, 1, &b);
        const VkBufferImageCopy copy = {
            .bufferOffset = 0,
            .bufferRowLength = sc->width,
            .bufferImageHeight = sc->height,
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .layerCount = 1,
                },
            .imageExtent = {sc->width, sc->height, 1},
        };
        sc->CmdCopyImageToBuffer(sc->command_buffer, sc->images[idx],
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 sc->staging, 1, &copy);
        sc->EndCommandBuffer(sc->command_buffer);

        const VkSubmitInfo submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &sc->command_buffer,
            .waitSemaphoreCount = (i == 0) ? pPresentInfo->waitSemaphoreCount
                                           : 0,
            .pWaitSemaphores =
                (i == 0) ? pPresentInfo->pWaitSemaphores : NULL,
        };
        VkQueue q = queue ? queue : sc->queue;
        if (sc->QueueSubmit(q, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            sc->QueueWaitIdle(q) != VK_SUCCESS) {
            if (pPresentInfo->pResults)
                pPresentInfo->pResults[i] = VK_ERROR_DEVICE_LOST;
            continue;
        }

        if (iland_wl_swapchain_present_pixels(
                sc->swapchain, sc->staging_map, sc->width * 4u) != 0) {
            if (pPresentInfo->pResults)
                pPresentInfo->pResults[i] = VK_ERROR_SURFACE_LOST_KHR;
            continue;
        }
        if (pPresentInfo->pResults)
            pPresentInfo->pResults[i] = VK_SUCCESS;
    }
    return VK_SUCCESS;
}

/* gdpa stash so CreateSwapchainKHR can resolve ICD device entry points. */
static PFN_vkGetDeviceProcAddr g_real_gdpa;

PFN_vkGetDeviceProcAddr iland_vk_wayland_current_gdpa(void)
{
    return g_real_gdpa;
}

static PFN_vkVoidFunction VKAPI_CALL
iland_GetDeviceProcAddr(VkDevice device, const char *name);

static PFN_vkVoidFunction VKAPI_CALL
iland_GetInstanceProcAddr(VkInstance instance, const char *name)
{
    if (!name)
        return NULL;

    if (strcmp(name, "vkCreateWaylandSurfaceKHR") == 0)
        return (PFN_vkVoidFunction)iland_CreateWaylandSurfaceKHR;
    if (strcmp(name, "vkDestroySurfaceKHR") == 0)
        return (PFN_vkVoidFunction)iland_DestroySurfaceKHR;
    if (strcmp(name, "vkGetPhysicalDeviceWaylandPresentationSupportKHR") == 0)
        return (PFN_vkVoidFunction)
            iland_GetPhysicalDeviceWaylandPresentationSupportKHR;
    if (strcmp(name, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0)
        return (PFN_vkVoidFunction)iland_GetPhysicalDeviceSurfaceSupportKHR;
    if (strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0)
        return (PFN_vkVoidFunction)iland_GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (strcmp(name, "vkGetPhysicalDeviceSurfaceFormatsKHR") == 0)
        return (PFN_vkVoidFunction)iland_GetPhysicalDeviceSurfaceFormatsKHR;
    if (strcmp(name, "vkGetPhysicalDeviceSurfacePresentModesKHR") == 0)
        return (PFN_vkVoidFunction)
            iland_GetPhysicalDeviceSurfacePresentModesKHR;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)iland_GetDeviceProcAddr;
    if (strcmp(name, "vkGetInstanceProcAddr") == 0)
        return (PFN_vkVoidFunction)iland_GetInstanceProcAddr;

    if (!g_real_gipa)
        return NULL;
    return g_real_gipa(instance, name);
}

static PFN_vkVoidFunction VKAPI_CALL
iland_GetDeviceProcAddr(VkDevice device, const char *name)
{
    if (!name)
        return NULL;

    if (strcmp(name, "vkCreateSwapchainKHR") == 0)
        return (PFN_vkVoidFunction)iland_CreateSwapchainKHR;
    if (strcmp(name, "vkDestroySwapchainKHR") == 0)
        return (PFN_vkVoidFunction)iland_DestroySwapchainKHR;
    if (strcmp(name, "vkGetSwapchainImagesKHR") == 0)
        return (PFN_vkVoidFunction)iland_GetSwapchainImagesKHR;
    if (strcmp(name, "vkAcquireNextImageKHR") == 0)
        return (PFN_vkVoidFunction)iland_AcquireNextImageKHR;
    if (strcmp(name, "vkQueuePresentKHR") == 0)
        return (PFN_vkVoidFunction)iland_QueuePresentKHR;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)iland_GetDeviceProcAddr;

    if (!g_real_gdpa && g_real_gipa) {
        g_real_gdpa = (PFN_vkGetDeviceProcAddr)g_real_gipa(
            VK_NULL_HANDLE, "vkGetDeviceProcAddr");
    }
    if (g_real_gdpa)
        return g_real_gdpa(device, name);
    return NULL;
}

PFN_vkGetInstanceProcAddr iland_vk_wayland_wrap_gipa(PFN_vkGetInstanceProcAddr real)
{
    g_real_gipa = real;
    if (real) {
        g_real_gdpa =
            (PFN_vkGetDeviceProcAddr)real(VK_NULL_HANDLE, "vkGetDeviceProcAddr");
    }
    return real ? iland_GetInstanceProcAddr : NULL;
}
