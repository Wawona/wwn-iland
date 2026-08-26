#include "modeb-coord.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#include <libproc.h>

static int g_scanout_lock_fd = -1;
static int g_scanout_owned;

static int scanout_pidfile_fd(void)
{
    char path[256];

    snprintf(path, sizeof(path), "%s", WWN_MODEB_SCANOUT_PID);
    return open(path, O_RDWR | O_CREAT, 0644);
}

static int pid_alive(pid_t pid)
{
    return pid > 1 && kill(pid, 0) == 0;
}

static void scanout_write_pid(int fd, pid_t pid)
{
    char buf[32];
    int n;

    if (ftruncate(fd, 0) != 0)
        return;
    n = snprintf(buf, sizeof(buf), "%d\n", (int)pid);
    if (n > 0)
        (void)pwrite(fd, buf, (size_t)n, 0);
}

static int scanout_read_pid(int fd, pid_t *out)
{
    char buf[32];
    ssize_t n;

    if (!out)
        return -1;
    if (lseek(fd, 0, SEEK_SET) < 0)
        return -1;
    memset(buf, 0, sizeof(buf));
    n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0)
        return -1;
    *out = (pid_t)strtol(buf, NULL, 10);
    return 0;
}

static void scanout_cleanup_stale(int fd)
{
    pid_t holder = 0;

    if (scanout_read_pid(fd, &holder) != 0 || holder <= 1)
        return;
    if (!pid_alive(holder)) {
        if (g_scanout_lock_fd == fd && g_scanout_owned)
            return;
        scanout_write_pid(fd, 0);
    }
}

static int compositor_comm(const char *name)
{
    const char *base;

    if (!name || !name[0])
        return 0;
    base = strrchr(name, '/');
    base = base ? base + 1 : name;
    return strcmp(base, "weston") == 0 || strcmp(base, "niri") == 0 ||
           strcmp(base, "kmscube") == 0 || strcmp(base, "gbm-es2-demo") == 0 ||
           strcmp(base, "gbm_es2_demo") == 0 || strcmp(base, "vkcube-kms") == 0;
}

static int proc_comm(pid_t pid, char *buf, size_t buflen)
{
    if (!buf || buflen < 2)
        return -1;
    buf[0] = '\0';
    if (proc_name(pid, buf, (uint32_t)buflen) <= 0)
        return -1;
    return 0;
}

static int session_has_compositor_recurse(pid_t root, int depth)
{
    pid_t children[128];
    int n;
    char comm[32];
    int i;

    if (depth > 8 || root <= 1)
        return 0;
    if (proc_comm(root, comm, sizeof(comm)) == 0 && compositor_comm(comm))
        return 1;
    n = proc_listchildpids(root, children, sizeof(children));
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (session_has_compositor_recurse(children[i], depth + 1))
            return 1;
    }
    return 0;
}

void wwn_modeb_scanout_claim(void)
{
    int fd;
    pid_t holder = 0;
    pid_t self = getpid();

    if (g_scanout_owned && g_scanout_lock_fd >= 0)
        return;

    fd = scanout_pidfile_fd();
    if (fd < 0)
        return;

    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return;
    }

    scanout_cleanup_stale(fd);
    if (scanout_read_pid(fd, &holder) == 0 && pid_alive(holder) && holder != self) {
        flock(fd, LOCK_UN);
        close(fd);
        return;
    }

    scanout_write_pid(fd, self);
    g_scanout_lock_fd = fd;
    g_scanout_owned = 1;
}

void wwn_modeb_scanout_release(void)
{
    pid_t holder = 0;

    if (!g_scanout_owned || g_scanout_lock_fd < 0)
        return;
    if (scanout_read_pid(g_scanout_lock_fd, &holder) == 0 && holder == getpid())
        scanout_write_pid(g_scanout_lock_fd, 0);
    flock(g_scanout_lock_fd, LOCK_UN);
    close(g_scanout_lock_fd);
    g_scanout_lock_fd = -1;
    g_scanout_owned = 0;
}

int wwn_modeb_scanout_holder_pid(void)
{
    int fd;
    pid_t holder = 0;

    fd = scanout_pidfile_fd();
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_SH | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            /* Exclusive lease held by a DRM client; do not block igetty/inputd. */
            if (scanout_read_pid(fd, &holder) == 0 && pid_alive(holder) && holder > 1) {
                close(fd);
                return (int)holder;
            }
            close(fd);
            return -1;
        }
        close(fd);
        return -1;
    }
    scanout_cleanup_stale(fd);
    if (scanout_read_pid(fd, &holder) != 0)
        holder = -1;
    else if (!pid_alive(holder))
        holder = -1;
    flock(fd, LOCK_UN);
    close(fd);
    return (int)holder;
}

int wwn_modeb_scanout_is_held(void)
{
    int holder = wwn_modeb_scanout_holder_pid();

    return holder > 1 && pid_alive((pid_t)holder);
}

int wwn_modeb_scanout_is_held_except(pid_t except)
{
    int holder = wwn_modeb_scanout_holder_pid();

    return holder > 1 && holder != (int)except && pid_alive((pid_t)holder);
}

int wwn_modeb_scanout_stop_holder(pid_t except)
{
    int holder = wwn_modeb_scanout_holder_pid();
    int st;

    if (holder <= 1 || holder == (int)except)
        return 0;
    kill((pid_t)holder, SIGTERM);
    for (int i = 0; i < 50; i++) {
        if (kill((pid_t)holder, 0) != 0)
            return 0;
        usleep(20000);
    }
    if (kill((pid_t)holder, 0) == 0)
        kill((pid_t)holder, SIGKILL);
    waitpid((pid_t)holder, &st, 0);
    return 0;
}

int wwn_modeb_session_runs_compositor(pid_t session_leader)
{
    if (session_leader <= 1 || !pid_alive(session_leader))
        return 0;
    return session_has_compositor_recurse(session_leader, 0);
}
