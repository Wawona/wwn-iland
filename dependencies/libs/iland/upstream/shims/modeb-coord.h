/*
 * Mode B Classic TTY scanout coordination (iland + igetty + inputd).
 *
 * Exactly one DRM scanout client may own the panel at a time. iland claims
 * the lease on first open("/dev/dri/card*"). igetty yields all dumb-BO
 * pageflips while the lease is held. inputd SIGTERMs the lease holder on
 * Ctrl+Alt+Backspace restore.
 */
#ifndef WWN_MODEB_COORD_H
#define WWN_MODEB_COORD_H

#include <sys/types.h>

#define WWN_MODEB_SUPPORT_DIR "/tmp/libwayland-support"
#define WWN_MODEB_SCANOUT_PID WWN_MODEB_SUPPORT_DIR "/modeb-drm-client.pid"

#ifdef __cplusplus
extern "C" {
#endif

/* iland: first card0 open / dylib unload */
void wwn_modeb_scanout_claim(void);
void wwn_modeb_scanout_release(void);

/* Readers: igetty, inputd, helper */
int  wwn_modeb_scanout_holder_pid(void);
int  wwn_modeb_scanout_is_held(void);
int  wwn_modeb_scanout_is_held_except(pid_t except);
int  wwn_modeb_scanout_stop_holder(pid_t except);

/* igetty: typed weston/niri are children of the login shell, not shell_pid */
int  wwn_modeb_session_runs_compositor(pid_t session_leader);

#ifdef __cplusplus
}
#endif

#endif /* WWN_MODEB_COORD_H */
