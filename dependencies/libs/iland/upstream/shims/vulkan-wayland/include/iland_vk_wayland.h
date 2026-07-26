/*
 * iland Vulkan Wayland WSI — IOSurface dmabuf present for Apple.
 *
 * MoltenVK / KosmicKrisp do not implement VK_KHR_wayland_surface. This archive
 * fills that gap: Wayland surface + swapchain entry points resolve here, while
 * everything else falls through to the selected ICD. Each present copies the
 * swapchain VkImage into an IOSurface slot and posts it with the same
 * zwp_linux_dmabuf_v1 modifier convention as the EGL winsys (#86).
 *
 * Link with libiland_wayland_egl.a (winsys) + libwayland-client. Force-load is
 * not required when the client names these symbols; the GetInstanceProcAddr
 * wrap is the usual entry.
 */
#ifndef ILAND_VK_WAYLAND_H
#define ILAND_VK_WAYLAND_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wrap an ICD's vkGetInstanceProcAddr so Wayland WSI names resolve to iland.
 * Pass the result to the client as its instance proc addr. NULL real → NULL. */
PFN_vkGetInstanceProcAddr iland_vk_wayland_wrap_gipa(PFN_vkGetInstanceProcAddr real);

/* Convenience: present tightly packed BGRA8 top-down pixels through an
 * already-created IlandWlSwapchain (for clients that skip VK_KHR_swapchain). */
struct IlandWlSwapchain;
int iland_vk_wayland_present_bgra(struct IlandWlSwapchain *sc, const void *pixels,
                                  uint32_t stride_bytes);

#ifdef __cplusplus
}
#endif

#endif /* ILAND_VK_WAYLAND_H */
