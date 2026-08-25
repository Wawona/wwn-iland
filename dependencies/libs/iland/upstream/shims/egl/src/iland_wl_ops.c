/* Slot owned by libiland_userland.a. egl_wayland.c fills it from a constructor
 * when libiland_wayland_egl.a is linked. Lives in its own TU so tvOS Phase 1
 * (enableGl=false, no egl.c) still exports the pointer vkcube's winsys sets. */
#include "iland_wl_ops.h"

const IlandWlOps *iland_wl_ops = NULL;
