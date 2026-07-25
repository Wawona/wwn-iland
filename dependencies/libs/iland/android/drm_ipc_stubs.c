#include "drm.h"

int drm_send_json(const char *json)
{
    (void)json;
    return -1;
}

int drm_send_json_with_surface(const char *json, unsigned int surface_port)
{
    (void)json;
    (void)surface_port;
    return -1;
}

int drm_receive_present_ack(unsigned timeout_ms)
{
    (void)timeout_ms;
    return -1;
}
