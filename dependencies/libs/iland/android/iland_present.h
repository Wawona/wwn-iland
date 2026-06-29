#ifndef ILAND_PRESENT_H
#define ILAND_PRESENT_H

#include "iosurface_compat.h"
#include <stdint.h>

typedef void (*iland_present_callback_t)(
    uint32_t crtc_id,
    uint32_t fb_id,
    IOSurfaceRef surface,
    uint32_t flags,
    void *user);

typedef void (*iland_cursor_callback_t)(
    int32_t op,
    int32_t x,
    int32_t y,
    IOSurfaceRef surface,
    void *user);

void iland_drm_set_present_callback(iland_present_callback_t cb, void *user);
void iland_drm_set_cursor_callback(iland_cursor_callback_t cb, void *user);
int iland_drm_present_is_in_window(void);
void iland_drm_set_preferred_mode(uint32_t w, uint32_t h, uint32_t refresh);

#endif
