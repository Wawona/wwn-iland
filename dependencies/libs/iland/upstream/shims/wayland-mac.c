#include <_abort.h>
#include <_stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mach/mach.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <servers/bootstrap.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stdarg.h>
#include <poll.h>
#include <signal.h>

/* DRM ioctl dispatch — intercepts open/ioctl for /dev/dri/card* */
#include <sys/ioctl.h>
#include "drm_ioctl.h"
#include "modeb-coord.h"

/* epoll shim functions we hook into — forward-declared to avoid pulling
 * in epoll_shim_ctx.h and its system-compat dependencies */
ssize_t epoll_shim_read(int fd, void *buf, size_t nbytes);
ssize_t epoll_shim_write(int fd, void const *buf, size_t nbytes);
int     epoll_shim_close(int fd);
int     epoll_shim_poll(struct pollfd fds[], nfds_t nfds, int timeout);
int     epoll_shim_fcntl(int fd, int cmd, ...);

#define SUPPORT_DIR "/tmp/libwayland-support"

extern char **environ;

/*
 * Mode B helper ownership.
 *
 * The injected dylib starts framebufferd/inputd/caffeinate on behalf of one
 * desktop-host Weston session.  Those helpers run as root and are not children
 * of the GUI app, so simply killing Weston used to leave them alive.  Track
 * only processes this dylib spawned and terminate them when the injected
 * Weston process unloads/exits.  Do not touch helpers belonging to an already
 * registered framebufferd service (another Mode B session owns those).
 */
static pid_t g_framebufferd_pid = -1;
static pid_t g_inputd_pid       = -1;
static pid_t g_caffeinate_pid   = -1;
static bool  g_owns_helpers     = false;

static void write_helper_pid(const char *name, pid_t pid)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), SUPPORT_DIR "/%s.pid", name);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fprintf(stderr, "[wayland-mac] cannot write %s: %s\n",
                path, strerror(errno));
        return;
    }
    dprintf(fd, "%d\n", (int)pid);
    close(fd);
}

static void remove_helper_pid(const char *name)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), SUPPORT_DIR "/%s.pid", name);
    unlink(path);
}

/* ── Dobby hook function pointer definitions ──────────────────────────
 * These are referenced via `extern` by wrap.c in epoll-shim-interpose.
 * DobbyHook is forward-declared to avoid pulling in <dobby.h> here. */

int DobbyHook(void *function_address, void *replace_call, void **origin_call);

typeof(read)  *wrap_real_read;
typeof(write) *wrap_real_write;
typeof(close) *wrap_real_close;
typeof(poll)  *wrap_real_poll;
typeof(fcntl) *wrap_real_fcntl;
typeof(open)  *wrap_real_open;
typeof(ioctl) *wrap_real_ioctl;
typeof(stat)  *wrap_real_stat;
typeof(lstat) *wrap_real_lstat;
typeof(fstat) *wrap_real_fstat;
typeof(fstatat) *wrap_real_fstatat;
typeof(access) *wrap_real_access;

static ssize_t hooked_read(int fd, void *buf, size_t nbytes)
{
    return epoll_shim_read(fd, buf, nbytes);
}

static ssize_t hooked_write(int fd, void const *buf, size_t nbytes)
{
    return epoll_shim_write(fd, buf, nbytes);
}

static int hooked_close(int fd)
{
    if (fd == DRM_VIRTUAL_FD)
        return 0;
    return epoll_shim_close(fd);
}

static int hooked_poll(struct pollfd fds[], nfds_t nfds, int timeout)
{
    return epoll_shim_poll(fds, nfds, timeout);
}

static int hooked_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    void *arg = va_arg(ap, void *);
    int rv = epoll_shim_fcntl(fd, cmd, arg);
    va_end(ap);
    return rv;
}

/* ── DRM open/ioctl/stat hooks ──────────────────────────────────────── */

#define ILAND_DRM_MAJOR 226

static int iland_drm_minor_for_name(const char *name)
{
    long n;

    if (!name)
        return -1;
    if (strncmp(name, "card", 4) == 0) {
        n = strtol(name + 4, NULL, 10);
        return (n >= 0 && n < 256) ? (int)n : -1;
    }
    if (strncmp(name, "renderD", 7) == 0) {
        n = strtol(name + 7, NULL, 10);
        return (n >= 0 && n < 256) ? (int)n : -1;
    }
    return -1;
}

static int iland_drm_minor_from_path(const char *path)
{
    if (!path || strncmp(path, "/dev/dri/", 9) != 0)
        return -1;
    return iland_drm_minor_for_name(path + 9);
}

static void iland_fill_drm_stat(struct stat *st, int minor)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0666;
    st->st_nlink = 1;
    st->st_rdev = makedev(ILAND_DRM_MAJOR, (unsigned int)minor);
    st->st_ino = (ino_t)((unsigned)minor + 1u);
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_blksize = 4096;
}

static void iland_note_drm_client_on_card_open(void)
{
    wwn_modeb_scanout_claim();
}

static void iland_clear_drm_client_pid_if_owned(void)
{
    wwn_modeb_scanout_release();
}

static int hooked_stat(const char *path, struct stat *st)
{
    int minor = iland_drm_minor_from_path(path);

    if (minor >= 0) {
        iland_fill_drm_stat(st, minor);
        return 0;
    }
    return wrap_real_stat(path, st);
}

static int hooked_lstat(const char *path, struct stat *st)
{
    int minor = iland_drm_minor_from_path(path);

    if (minor >= 0) {
        iland_fill_drm_stat(st, minor);
        return 0;
    }
    return wrap_real_lstat(path, st);
}

static int hooked_fstat(int fd, struct stat *st)
{
    if (fd == DRM_VIRTUAL_FD) {
        iland_fill_drm_stat(st, 0);
        return 0;
    }
    return wrap_real_fstat(fd, st);
}

static int hooked_fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    int minor = iland_drm_minor_from_path(path);

    (void)flags;
    (void)dirfd;
    if (minor >= 0) {
        iland_fill_drm_stat(st, minor);
        return 0;
    }
    return wrap_real_fstatat(dirfd, path, st, flags);
}

static int hooked_access(const char *path, int mode)
{
    if (iland_drm_minor_from_path(path) >= 0)
        return 0;
    return wrap_real_access(path, mode);
}

static int hooked_open(const char *path, int flags, ...)
{
    va_list ap;
    va_start(ap, flags);
    int mode = (flags & O_CREAT) ? va_arg(ap, int) : 0;
    va_end(ap);

    /* iland userspace DRM. Never a real kernel node. card* and renderD*
     * both map to the virtual fd so niri/weston TTY backends match Linux. */
    if (path && strncmp(path, "/dev/dri/", 9) == 0) {
        if (iland_drm_minor_for_name(path + 9) >= 0 &&
            strncmp(path + 9, "card", 4) == 0)
            iland_note_drm_client_on_card_open();
        return DRM_VIRTUAL_FD;
    }
    return wrap_real_open(path, flags, mode);
}

static int hooked_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);

    if (fd == DRM_VIRTUAL_FD)
        return drm_ioctl_dispatch(request, arg);
    return wrap_real_ioctl(fd, request, arg);
}

static void install_epoll_hooks(void)
{
#define HOOK(fun)                                                         \
    do {                                                                  \
        int ret = DobbyHook((void *)fun, (void *)hooked_##fun,            \
                            (void **)&wrap_real_##fun);                   \
        if (ret != 0) {                                                   \
            fprintf(stderr,                                               \
                    "epoll-shim: error hooking \"" #fun "\" with DobbyHook!\n"); \
            abort();                                                      \
        }                                                                 \
    } while (0)

    HOOK(read);
    HOOK(write);
    HOOK(close);
    HOOK(poll);
    HOOK(fcntl);

#undef HOOK
}

static int extract_section(const char *segname, const char *sectname,
                            const char *destpath) {
    unsigned long size = 0;
    const uint8_t *data = getsectiondata(&_mh_dylib_header, segname, sectname,
                                         &size);
    if (!data || size == 0) {
        fprintf(stderr, "[wayland-mac] section %s,%s not found\n",
                segname, sectname);
        return -1;
    }

    int fd = open(destpath, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        fprintf(stderr, "[wayland-mac] open %s: %s\n", destpath,
                strerror(errno));
        return -1;
    }

    ssize_t written = write(fd, data, size);
    close(fd);

    if (written != (ssize_t)size) {
        fprintf(stderr, "[wayland-mac] write %s: short write\n", destpath);
        return -1;
    }

    return 0;
}

/* Build environment without DYLD_INSERT_LIBRARIES so spawned children
 * don't recursively load our dylib. */
static char **clean_environ(void) {
    static char **clean = NULL;
    if (clean) return clean;

    int count = 0;
    while (environ[count]) count++;

    /* Each entry survives unless it starts with DYLD_INSERT_LIBRARIES=.
     * Worst case: all survive. */
    clean = calloc(count + 1, sizeof(char *));
    if (!clean) return environ;

    int j = 0;
    for (int i = 0; i < count; i++) {
        if (strncmp(environ[i], "DYLD_INSERT_LIBRARIES=", 22) == 0)
            continue;
        clean[j++] = environ[i];
    }
    clean[j] = NULL;

    unsetenv("DYLD_INSERT_LIBRARIES");

    return clean;
}

#define POSIX_SPAWN_PROC_TYPE_DAEMON_INTERACTIVE    0x00000400
#define CS_LAUNCH_TYPE_SYSTEM_SERVICE 1
int posix_spawnattr_setprocesstype_np(posix_spawnattr_t *, const int);
int posix_spawnattr_set_launch_type_np(posix_spawnattr_t *attr, int launch_type);
int posix_spawnattr_set_darwin_role_np(const posix_spawnattr_t * __restrict, uint64_t);

static void wmac_log(const char *fmt, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if (n > (int)sizeof(buf) - 2) {
        n = (int)sizeof(buf) - 2;
    }
    buf[n++] = '\n';
    (void)write(STDERR_FILENO, buf, (size_t)n);
    int fd = open("/tmp/wawona-modeb.log", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd >= 0) {
        (void)write(fd, buf, (size_t)n);
        close(fd);
    }
}

static int spawn_one(const char *path, char *const argv[], pid_t *pid_out,
                     int as_system_service) {
    pid_t pid;
    posix_spawnattr_t spattr;
    posix_spawnattr_init(&spattr);
    posix_spawnattr_setprocesstype_np(&spattr,
                                      POSIX_SPAWN_PROC_TYPE_DAEMON_INTERACTIVE);
    if (as_system_service) {
        posix_spawnattr_set_launch_type_np(&spattr, CS_LAUNCH_TYPE_SYSTEM_SERVICE);
        if (strstr(path, "framebufferd") != NULL) {
            posix_spawnattr_set_darwin_role_np(&spattr, 0x4);
        }
    }

    int ret = posix_spawn(&pid, path, NULL, &spattr, argv, clean_environ());
    posix_spawnattr_destroy(&spattr);
    if (ret != 0) {
        wmac_log("[wayland-mac] posix_spawn %s (system=%d): %s", path,
                 as_system_service, strerror(ret));
        return -1;
    }
    usleep(80000);
    if (kill(pid, 0) != 0 && errno == ESRCH) {
        wmac_log("[wayland-mac] %s pid %d died immediately (system=%d)", path,
                 (int)pid, as_system_service);
        return -1;
    }
    if (pid_out)
        *pid_out = pid;
    wmac_log("[wayland-mac] spawned %s pid=%d system=%d", path, (int)pid,
             as_system_service);
    return 0;
}

static int spawn_and_wait(const char *path, char *const argv[]) {
    pid_t pid;
    posix_spawnattr_t spattr;
    posix_spawnattr_init(&spattr);
    posix_spawnattr_setprocesstype_np(&spattr, POSIX_SPAWN_PROC_TYPE_DAEMON_INTERACTIVE);
    posix_spawnattr_set_launch_type_np(&spattr, CS_LAUNCH_TYPE_SYSTEM_SERVICE);

    int ret = posix_spawn(&pid, path, NULL, &spattr, argv, clean_environ());
    posix_spawnattr_destroy(&spattr);
    if (ret != 0) {
        wmac_log("[wayland-mac] posix_spawn %s: %s", path, strerror(ret));
        return -1;
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        wmac_log("[wayland-mac] waitpid %s: %s", path, strerror(errno));
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int wait_mach_service(const char *name, int tries) {
    mach_port_t port = MACH_PORT_NULL;
    int i;
    for (i = 0; i < tries; i++) {
        kern_return_t kr = bootstrap_look_up(bootstrap_port, (char *)name, &port);
        if (kr == KERN_SUCCESS) {
            mach_port_deallocate(mach_task_self(), port);
            wmac_log("[wayland-mac] %s registered", name);
            return 0;
        }
        usleep(50000);
    }
    wmac_log("[wayland-mac] timeout waiting for %s", name);
    return -1;
}

/*
 * Spawn background helpers. Prefer CS_LAUNCH_TYPE_SYSTEM_SERVICE (CoreBedtime),
 * but on 25F80 that domain can accept the process while bootstrap_register
 * fails ("unknown error code"). Verify the Mach name when required; on miss,
 * kill and retry without SYSTEM_SERVICE.
 */
static int spawn_background(const char *path, char *const argv[], pid_t *pid_out,
                            const char *mach_name) {
    int attempt;
    for (attempt = 0; attempt < 2; attempt++) {
        int as_sys = (attempt == 0) ? 1 : 0;
        pid_t pid = -1;
        if (spawn_one(path, argv, &pid, as_sys) != 0) {
            if (as_sys) {
                wmac_log("[wayland-mac] retry %s without CS_LAUNCH_TYPE_SYSTEM_SERVICE",
                         path);
                continue;
            }
            return -1;
        }
        if (mach_name != NULL) {
            if (wait_mach_service(mach_name, 40) != 0) {
                wmac_log("[wayland-mac] %s pid %d up but %s not registered "
                         "(system=%d); killing and retrying",
                         path, (int)pid, mach_name, as_sys);
                (void)kill(pid, SIGTERM);
                usleep(100000);
                (void)kill(pid, SIGKILL);
                if (!as_sys)
                    return -1;
                wmac_log("[wayland-mac] retry %s without CS_LAUNCH_TYPE_SYSTEM_SERVICE",
                         path);
                continue;
            }
        }
        if (pid_out)
            *pid_out = pid;
        return 0;
    }
    return -1;
}

static void stop_owned_helper(pid_t *pid, const char *name)
{
    if (!pid || *pid <= 0)
        return;

    if (kill(*pid, SIGTERM) != 0 && errno != ESRCH)
        fprintf(stderr, "[wayland-mac] failed to stop %s pid %d: %s\n",
                name, (int)*pid, strerror(errno));
    *pid = -1;
    remove_helper_pid(name);
}

__attribute__((destructor))
static void wayland_mac_unload(void)
{
    iland_clear_drm_client_pid_if_owned();

    if (!g_owns_helpers)
        return;

    /*
     * Reverse startup order.  framebufferd releases the retained IOSurface
     * after its run loop exits; inputd/caffeinate must not outlive the desktop
     * session.  These are the PIDs spawned by this dylib only.
     */
    stop_owned_helper(&g_caffeinate_pid, "caffeinate");
    stop_owned_helper(&g_inputd_pid, "inputd");
    stop_owned_helper(&g_framebufferd_pid, "framebufferd");
    g_owns_helpers = false;
}

static void install_drm_hooks(void)
{
    int ret;

    ret = DobbyHook((void *)open,      (void *)hooked_open,
                    (void **)&wrap_real_open);
    if (ret != 0) {
        fprintf(stderr, "wayland-mac: error hooking \"open\" with DobbyHook!\n");
        abort();
    }

    ret = DobbyHook((void *)ioctl,     (void *)hooked_ioctl,
                    (void **)&wrap_real_ioctl);
    if (ret != 0) {
        fprintf(stderr, "wayland-mac: error hooking \"ioctl\" with DobbyHook!\n");
        abort();
    }

    ret = DobbyHook((void *)stat,      (void *)hooked_stat,
                    (void **)&wrap_real_stat);
    if (ret != 0) {
        fprintf(stderr, "wayland-mac: error hooking \"stat\" with DobbyHook!\n");
        abort();
    }

    ret = DobbyHook((void *)lstat,     (void *)hooked_lstat,
                    (void **)&wrap_real_lstat);
    if (ret != 0) {
        fprintf(stderr, "wayland-mac: error hooking \"lstat\" with DobbyHook!\n");
        abort();
    }

    ret = DobbyHook((void *)fstat,     (void *)hooked_fstat,
                    (void **)&wrap_real_fstat);
    if (ret != 0) {
        fprintf(stderr, "wayland-mac: error hooking \"fstat\" with DobbyHook!\n");
        abort();
    }

    ret = DobbyHook((void *)fstatat,   (void *)hooked_fstatat,
                    (void **)&wrap_real_fstatat);
    if (ret != 0) {
        fprintf(stderr, "wayland-mac: error hooking \"fstatat\" with DobbyHook!\n");
        abort();
    }

    ret = DobbyHook((void *)access,    (void *)hooked_access,
                    (void **)&wrap_real_access);
    if (ret != 0) {
        fprintf(stderr, "wayland-mac: error hooking \"access\" with DobbyHook!\n");
        abort();
    }
}

__attribute__((constructor))
static void wayland_mac_load(void) {
    /*
     * Classic engage (igettyd / weston / niri as root) may spawn
     * framebufferd and inputd. A Doorman login user typing `niri` is a
     * DRM/KMS client of those helpers. Insert still installs open/ioctl
     * hooks so libc open("/dev/dri/...") is iland, never a real node.
     * Never abort() for euid != 0. Never sudo the compositor.
     */
    wmac_log("[wayland-mac] constructor begin uid=%d", (int)geteuid());

    /* Create a real pipe dup'd to DRM_VIRTUAL_FD so select/poll work on
     * our virtual DRM fd.  The read end becomes fd 42; the write end is
     * stored for drmModePageFlip to signal page-flip completion events. */
    {
        int p[2];
        if (pipe(p) == 0) {
            dup2(p[0], DRM_VIRTUAL_FD);
            close(p[0]);
            /* Declared in drm_linux.h, defined in drm_linux.c */
            extern int g_drm_event_pipe_write;
            g_drm_event_pipe_write = p[1];
        }
    }

    /* Install hooks before anything else — these intercept libc calls
     * (read, write, poll, close, fcntl) and route them through the
     * epoll shim.  Future DRM hooks go here too. */
    install_epoll_hooks();
    install_drm_hooks();

    if (geteuid() != 0) {
        wmac_log("[wayland-mac] client-only insert uid=%d (DRM/KMS hooks; "
                 "helpers stay with Classic)", (int)geteuid());
        return;
    }

    /* Classic helper publishes Mach via launchd MachServices. After WS
     * unload, bootstrap_look_up of a legacy register name fails
     * ("exception protected"), so never spawn a second framebufferd when
     * the helper already armed modeb-mach.ready / owns the pidfile. */
    int framebufferd_already = 0;
    if (access("/tmp/libwayland-support/modeb-mach.ready", F_OK) == 0) {
        framebufferd_already = 1;
        wmac_log("[wayland-mac] modeb-mach.ready present (helper-owned); "
                 "skipping framebufferd spawn");
    } else {
        mach_port_t port = MACH_PORT_NULL;
        kern_return_t kr = bootstrap_look_up(bootstrap_port,
                                            "com.wayland-mac.framebufferd",
                                            &port);
        if (kr == KERN_SUCCESS) {
            mach_port_deallocate(mach_task_self(), port);
            framebufferd_already = 1;
            wmac_log("[wayland-mac] com.wayland-mac.framebufferd already "
                     "registered; skipping framebufferd spawn");
        }
    }

    /* Create support directory */
    if (mkdir(SUPPORT_DIR, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "[wayland-mac] mkdir %s: %s\n", SUPPORT_DIR,
                strerror(errno));
        return;
    }

    const char *amfiexceptiond_path = SUPPORT_DIR "/amfiexceptiond";
    const char *framebufferd_path    = SUPPORT_DIR "/framebufferd";

    if (!framebufferd_already) {
        /* Extract and launch amfiexceptiond — wait for it to finish patching AMFI */
        if (extract_section("__DATA_OBJ", "amfiexceptiond", amfiexceptiond_path) == 0) {
            char *const argv[] = {
                (char *)amfiexceptiond_path,
                NULL
            };
            spawn_and_wait(amfiexceptiond_path, argv);
        }

        /* Extract and launch framebufferd, then wait for its Mach service */
        if (extract_section("__DATA_OBJ", "framebufferd", framebufferd_path) == 0) {
            char *const argv[] = {
                (char *)framebufferd_path,
                NULL
            };
            if (spawn_background(framebufferd_path, argv, &g_framebufferd_pid,
                                 "com.wayland-mac.framebufferd") != 0)
                return;
            write_helper_pid("framebufferd", g_framebufferd_pid);
            g_owns_helpers = true;
        } else {
            wmac_log("[wayland-mac] could not extract framebufferd");
            return;
        }
    }


    /*
     * Same as framebufferd: after WS unload, bootstrap_look_up of the
     * helper's launchd MachServices name fails. Spawning a second inputd
     * then times out and kill -KILL that child. Classic 2026-08-22: that
     * left igettyd unable to subscribe (invalid destination port) and
     * blank fail-closed. When the helper armed modeb-mach.ready, do not
     * look_up or spawn inputd.
     */
    if (framebufferd_already) {
        wmac_log("[wayland-mac] helper-owned Mach; skipping inputd spawn");
    } else {
        mach_port_t iport = MACH_PORT_NULL;
        kern_return_t ikr = bootstrap_look_up(bootstrap_port,
                                              "com.wayland-mac.inputd",
                                              &iport);
        if (ikr == KERN_SUCCESS) {
            mach_port_deallocate(mach_task_self(), iport);
            wmac_log("[wayland-mac] com.wayland-mac.inputd already registered");
        } else {
            const char *inputd_path = SUPPORT_DIR "/inputd";
            if (extract_section("__DATA_OBJ", "inputd", inputd_path) == 0) {
                char *const argv[] = {
                    (char *)inputd_path,
                    NULL
                };
                if (spawn_background(inputd_path, argv, &g_inputd_pid,
                                     "com.wayland-mac.inputd") != 0)
                    return;
                write_helper_pid("inputd", g_inputd_pid);
                g_owns_helpers = true;
            } else {
                wmac_log("[wayland-mac] could not extract inputd");
                return;
            }
        }
    }

    /* Prevent display sleep while weston is running */
    {
        char *const argv[] = {
            (char *)"/usr/bin/caffeinate",
            (char *)"-d",
            NULL
        };
        if (spawn_background("/usr/bin/caffeinate", argv, &g_caffeinate_pid,
                             NULL) != 0)
            return;
        write_helper_pid("caffeinate", g_caffeinate_pid);
        g_owns_helpers = true;
    }
}

__attribute__((visibility("default")))
void wayland_mac_init(void) {
    (void)0;
}
