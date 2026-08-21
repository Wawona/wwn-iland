#include "drm_ipc.h"
#include "DisplaySurface.h"

#include <bootstrap.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <IOSurface/IOSurface.h>
#import <objc/message.h>
#import <objc/runtime.h>
#import <dlfcn.h>

#include <SymRez/SymRez.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface NSObject (FBPDisplay)
- (void)presentSurface:(IOSurfaceRef)surface withOptions:(NSDictionary *)options;
- (CGRect)bounds;
@end

/* ── SkyLight / CoreDisplay SPI marker ────────────────────────────────── */

__attribute__((used, section("__SLSERVER,__slserver")))
static const char _slserver_marker = 1;

/* ── Fake session stubs required by SkyLight initialisation ──────────── */

static uint8_t  fake_session_data[0x200];
static uint8_t  fake_sub_object[0x200];
static uint8_t  fake_session_ctrl[0x100];
static uint8_t  fake_cursor_ctrl[0x100];
static uint64_t fake_connections_ptr;
static uint8_t *fake_event_data;
static uint8_t *fake_event_caps;

/* ── globals ──────────────────────────────────────────────────────────── */

static mach_port_t   g_server_port  = MACH_PORT_NULL;
static id            g_display;          /* CAWindowServerDisplay */

/* The latest client surface (retained) — directly presentable */
static IOSurfaceRef  g_client_surface;
static mach_port_t   g_present_reply_port = MACH_PORT_NULL;

static pthread_mutex_t g_surface_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile bool g_running = true;
static volatile bool g_dirty = false;
static CFRunLoopRef g_main_run_loop;
static CFRunLoopSourceRef g_present_source;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = false;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

/* ── Present timer (main thread) ──────────────────────────────────────── */

static volatile uint64_t g_present_count = 0;
static volatile uint64_t g_flip_count = 0;

static void TimerCallback(CFRunLoopTimerRef timer, void *info)
{
    (void)timer; (void)info;
    @autoreleasepool {
        if (!g_display) return;
        if (!g_dirty) return;
        g_dirty = false;

        /* Snapshot client surface under lock */
        pthread_mutex_lock(&g_surface_lock);
        IOSurfaceRef client = g_client_surface;
        if (client) CFRetain(client);
        mach_port_t reply_port = g_present_reply_port;
        g_present_reply_port = MACH_PORT_NULL;
        pthread_mutex_unlock(&g_surface_lock);

        if (!client) return;

        /* Directly present the client surface — no compositing needed.
         * The surface was created via DisplaySurface_create() with the
         * same format/properties as the display pipeline. */
        [g_display presentSurface:client withOptions:@{}];
        uint64_t n = ++g_present_count;
        if (n == 1 || (n % 60) == 0) {
            fprintf(stderr,
                    "[framebufferd] presentSurface n=%llu w=%zu h=%zu "
                    "(WS still up ⇒ panel may not change; Classic needs "
                    "WindowServer unloaded)\n",
                    (unsigned long long)n,
                    (size_t)IOSurfaceGetWidth(client),
                    (size_t)IOSurfaceGetHeight(client));
        }
        CFRelease(client);
        if (reply_port != MACH_PORT_NULL) {
            mach_msg_header_t ack = {0};
            ack.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
            ack.msgh_size = sizeof(ack);
            ack.msgh_remote_port = reply_port;
            ack.msgh_id = DRM_IPC_MSG_ID + 1;
            kern_return_t kr = mach_msg(&ack, MACH_SEND_MSG, ack.msgh_size, 0,
                                        MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE,
                                        MACH_PORT_NULL);
            if (kr != KERN_SUCCESS)
                mach_port_deallocate(mach_task_self(), reply_port);
        }
    }
}

static void PresentSourcePerform(void *info)
{
    (void)info;
    TimerCallback(NULL, NULL);
}

/*
 * Wake the present run loop from the Mach server thread.  Thread-safe:
 * CFRunLoopSourceSignal + CFRunLoopWakeUp are the supported cross-thread
 * kick.  Do not call CVDisplayLink / CADisplayLink from this process.
 *
 * Those APIs are WindowServer *clients*.  They enumerate displays through
 * SLSGetActiveDisplayList / CGGetActiveDisplayList and sample VBL through
 * SLSDisplayGetCurrentVBLDelta (Mozilla 1759232: CoreVideo's IO thread
 * polls _CGSGetRealtimeDisplayDataShmemPort).  After this process has
 * become CAWindowServer, CGSRunningInServer() is true and those client
 * entry points assert.  A CFRunLoopTimer is the in-server cadence; the
 * CAWindowServerDisplay presentSurface path still waits for host vblank.
 */
static void request_present(void)
{
    if (g_present_source && g_main_run_loop) {
        CFRunLoopSourceSignal(g_present_source);
        CFRunLoopWakeUp(g_main_run_loop);
    }
}

/* ── Mach message server thread ────────────────────────────────────────── */

static void *mach_server_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        drm_ipc_msg_t msg = {0};
        msg.header.msgh_size       = sizeof(msg);
        msg.header.msgh_local_port = g_server_port;

        kern_return_t kr = mach_msg(&msg.header,
                                     MACH_RCV_MSG,
                                     0,
                                     sizeof(msg),
                                     g_server_port,
                                     MACH_MSG_TIMEOUT_NONE,
                                     MACH_PORT_NULL);
        if (kr != KERN_SUCCESS) {
            if (g_running)
                fprintf(stderr, "[framebufferd] mach_msg recv: %s\n",
                        mach_error_string(kr));
            continue;
        }

        if (msg.header.msgh_id != DRM_IPC_MSG_ID)
            continue;

        IOSurfaceRef client_surface = NULL;
        if (msg.body.msgh_descriptor_count >= 1 &&
            msg.surface_port.type == MACH_MSG_PORT_DESCRIPTOR &&
            msg.surface_port.name != MACH_PORT_NULL) {

            client_surface = IOSurfaceLookupFromMachPort(msg.surface_port.name);
            mach_port_deallocate(mach_task_self(), msg.surface_port.name);
        }

        /* Route message by operation type */
        if (strstr(msg.json, "\"op\":\"cursor_set\"")) {
            if (client_surface) CFRelease(client_surface);
        } else if (strstr(msg.json, "\"op\":\"cursor_move\"")) {
            if (client_surface) CFRelease(client_surface);
        } else if (client_surface) {
            /* Page flip — store the client surface for presentation */
            pthread_mutex_lock(&g_surface_lock);
            if (g_client_surface) CFRelease(g_client_surface);
            if (g_present_reply_port != MACH_PORT_NULL)
                mach_port_deallocate(mach_task_self(), g_present_reply_port);
            g_client_surface = client_surface;
            g_present_reply_port = msg.header.msgh_remote_port;
            g_dirty = true;
            g_flip_count++;
            if (g_flip_count == 1 || (g_flip_count % 60) == 0) {
                fprintf(stderr, "[framebufferd] flip ipc n=%llu\n",
                        (unsigned long long)g_flip_count);
            }
            pthread_mutex_unlock(&g_surface_lock);
            request_present();
        } else {
            if (client_surface) CFRelease(client_surface);
        }
    }
    return NULL;
}

bool get_display_resolution(uint32_t *w, uint32_t *h) {
    NSString *path = @"/Library/Preferences/com.apple.windowserver.displays.plist";
    NSDictionary *plist = [NSDictionary dictionaryWithContentsOfFile:path];
    if (!plist) {
        fprintf(stderr, "[framebufferd] failed to read %s\n", [path UTF8String]);
        return false;
    }
    NSArray *configs = [plist valueForKeyPath:@"DisplayAnyUserSets.Configs"];
    if (![configs isKindOfClass:[NSArray class]]) return false;
    for (NSDictionary *cfg in configs) {
        NSArray *dispCfg = cfg[@"DisplayConfig"];
        if (![dispCfg isKindOfClass:[NSArray class]]) continue;
        for (NSDictionary *disp in dispCfg) {
            NSDictionary *info = disp[@"CurrentInfo"];
            if (![info isKindOfClass:[NSDictionary class]]) continue;
            uint32_t dw = [info[@"Wide"] unsignedIntValue];
            uint32_t dh = [info[@"High"] unsignedIntValue];
            if (dw > 0 && dh > 0) {
                *w = dw;
                *h = dh;
                return true;
            }
        }
    }
    fprintf(stderr, "[framebufferd] no display config found in plist\n");
    return false;
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    @autoreleasepool {
        /* ── Register Mach service ───────────────────────────────────── */
        kern_return_t kr = mach_port_allocate(mach_task_self(),
                                               MACH_PORT_RIGHT_RECEIVE,
                                               &g_server_port);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "[framebufferd] mach_port_allocate: %s\n",
                    mach_error_string(kr));
            return 1;
        }

        kr = mach_port_insert_right(mach_task_self(), g_server_port,
                                     g_server_port,
                                     MACH_MSG_TYPE_MAKE_SEND);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "[framebufferd] mach_port_insert_right: %s\n",
                    mach_error_string(kr));
            return 1;
        }

        kr = bootstrap_register(bootstrap_port, DRM_IPC_SERVICE_NAME,
                                 g_server_port);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "[framebufferd] bootstrap_register %s: %s\n",
                    DRM_IPC_SERVICE_NAME, mach_error_string(kr));
            return 1;
        }

        printf("[framebufferd] listening on %s\n", DRM_IPC_SERVICE_NAME);

        /*
         * KEEP_WS probe: Aqua stays up. Claiming the panel via CoreDisplay /
         * CAWindowServer blanks the only interactive display (2026-08-20).
         * Register Mach for weston DRM IPC, but skip panel present.
         */
        if (getenv("WWN_MODEB_KEEP_WS") &&
            getenv("WWN_MODEB_KEEP_WS")[0] != '\0' &&
            strcmp(getenv("WWN_MODEB_KEEP_WS"), "0") != 0) {
            fprintf(stderr,
                    "[framebufferd] WWN_MODEB_KEEP_WS=1: Mach IPC only, "
                    "no CoreDisplay panel claim\n");
            g_main_run_loop = CFRunLoopGetCurrent();
            CFRetain(g_main_run_loop);
            pthread_t thread;
            pthread_create(&thread, NULL, mach_server_thread, NULL);
            pthread_detach(thread);
            CFRunLoopRun();
            CFRelease(g_main_run_loop);
            g_main_run_loop = NULL;
            g_running = false;
            return 0;
        }

        /* ── Set up CAWindowServer display pipeline ──────────────────── */

        /* a) Load private frameworks so SymRez can find them */
        dlopen("/System/Library/Frameworks/CoreDisplay.framework/Versions/A/CoreDisplay",
               RTLD_LAZY);
        dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight",
               RTLD_LAZY);
        dlopen("/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore",
               RTLD_LAZY);

        /* b) Resolve all symbols upfront */
#define SR_CD  "/System/Library/Frameworks/CoreDisplay.framework/Versions/A/CoreDisplay"
#define SR_SL  "/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight"
#define SR_QC  "/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore"

        symrez_t sr_cd = symrez_new(SR_CD);
        symrez_t sr_sl = symrez_new(SR_SL);
        symrez_t sr_qc = symrez_new(SR_QC);

        void  (*fn_SLSInit)(void)              = sr_resolve_symbol(sr_sl, "_SLSInitialize");
        void  (*fn_InitCD)(const void *)        = sr_resolve_symbol(sr_cd, "_InitializeCoreDisplay");
        void  (*fn_DispDrvInit)(void)           = sr_resolve_symbol(sr_cd, "_CGXDisplayDriverInitialize");
        void **p_WSCDCallbacks                  = sr_resolve_symbol(sr_sl, "_WSCDInitializeVtable.callbacks");
        void **p_sessionCtrl                    = sr_resolve_symbol(sr_sl, "___sessionControlRef");
        void **p_g_server                       = sr_resolve_symbol(sr_sl, "__ZL9_g_server");
        void  (*fn_CARenderServerRegister)(int) = sr_resolve_symbol(sr_sl, "_CARenderServerRegister");
        void **p_shared_server                  = sr_resolve_symbol(sr_qc, "__ZL14_shared_server");
        (void)fn_CARenderServerRegister;

        sr_free(sr_cd);
        sr_free(sr_sl);
        sr_free(sr_qc);

        /* c) Initialise SkyLight + CoreDisplay in order */
        fn_SLSInit();
        fn_InitCD(p_WSCDCallbacks);

        memset(fake_session_data, 0, sizeof(fake_session_data));
        memset(fake_sub_object,   0, sizeof(fake_sub_object));
        memset(fake_session_ctrl, 0, sizeof(fake_session_ctrl));
        memset(fake_cursor_ctrl,  0, sizeof(fake_cursor_ctrl));

        *(void    **)(fake_sub_object  + 232) = fake_session_data;
        *(void    **)(fake_sub_object  + 176) = &fake_connections_ptr;
        *(void    **)(fake_sub_object  + 256) = fake_cursor_ctrl;
        *(uint64_t *)(fake_cursor_ctrl + 120) = 0x10;
        *(void    **)(fake_session_ctrl + 32) = fake_sub_object;
        *p_sessionCtrl = fake_session_ctrl;

        fake_event_data = calloc(1, 0x1000);
        fake_event_caps = calloc(1, 0x100);
        *(void **)(fake_sub_object + 0xD0) = fake_event_data;
        *(void **)(fake_event_data + 0xA0) = fake_event_caps;

        /* ── Create CAWindowServer instance ─────────────────────────── */
        Class caWS = NSClassFromString(@"CAWindowServer");
        if (p_shared_server) *p_shared_server = NULL;

        id server = ((id(*)(id, SEL, id))objc_msgSend)(
            (id)caWS,
            NSSelectorFromString(@"serverWithOptions:"),
            @{@"fetchFrozenSurfaces": @YES});

        if (p_g_server) *p_g_server = (__bridge void *)server;

        void *impl = NULL;
        object_getInstanceVariable(server, "_impl", &impl);
        if (impl) {
            NSArray *displays = (__bridge NSArray *)(*(void **)impl);
            g_display = [displays firstObject];
        }
        printf("[framebufferd] CAWindowServer ready, display=%s\n",
               g_display ? "yes" : "no");
        if (!g_display) {
            fprintf(stderr,
                    "[framebufferd] FAIL: no CAWindowServerDisplay after "
                    "serverWithOptions (blank panel). Abort.\n");
            return 1;
        }

        fn_DispDrvInit();
        fprintf(stderr, "[framebufferd] CoreDisplay initialised\n");

        /* Present source + 60Hz fallback *before* the Mach thread, so a flip
         * that arrives during init can wake the run loop instead of waiting
         * for the first timer fire.  Do not use CVDisplayLink here: that is
         * a WindowServer client API and asserts once we are the server. */
        g_main_run_loop = CFRunLoopGetCurrent();
        CFRetain(g_main_run_loop);
        CFRunLoopSourceContext source_context = {0};
        source_context.perform = PresentSourcePerform;
        g_present_source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0,
                                                 &source_context);
        CFRunLoopAddSource(g_main_run_loop, g_present_source,
                           kCFRunLoopCommonModes);

        CFRunLoopTimerRef fallback_timer = CFRunLoopTimerCreate(
            kCFAllocatorDefault, CFAbsoluteTimeGetCurrent(), 1.0 / 60,
            0, 0, TimerCallback, NULL);
        CFRunLoopAddTimer(g_main_run_loop, fallback_timer,
                          kCFRunLoopCommonModes);
        fprintf(stderr,
                "[framebufferd] in-server present: Mach kick + 60Hz timer "
                "(CVDisplayLink is a client API; not used)\n");

        /* ── Start Mach server thread ───────────────────────────────── */
        pthread_t thread;
        pthread_create(&thread, NULL, mach_server_thread, NULL);
        pthread_detach(thread);

        printf("[framebufferd] direct-present mode (zero-copy, host vsync)\n");
        CFRunLoopRun();

        if (fallback_timer) CFRelease(fallback_timer);
        CFRunLoopRemoveSource(g_main_run_loop, g_present_source,
                              kCFRunLoopCommonModes);
        CFRelease(g_present_source);
        g_present_source = NULL;
        CFRelease(g_main_run_loop);
        g_main_run_loop = NULL;
        g_running = false;

        pthread_mutex_lock(&g_surface_lock);
        if (g_client_surface) { CFRelease(g_client_surface); g_client_surface = NULL; }
        if (g_present_reply_port != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), g_present_reply_port);
            g_present_reply_port = MACH_PORT_NULL;
        }
        pthread_mutex_unlock(&g_surface_lock);
    }
    return 0;
}
