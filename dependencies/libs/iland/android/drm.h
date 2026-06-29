#ifndef DRM_H
#define DRM_H

#include <stddef.h>

int drm_send_json(const char *json);
int drm_send_json_with_surface(const char *json, unsigned int surface_port);

#endif
