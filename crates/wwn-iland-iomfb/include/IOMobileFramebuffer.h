/* FROZEN. Authority moved to github.com/Wawona/wwn-iomfb-rs.
 * Do not add ABI guesses here. Wawona #169 Desktop IOMFB waits until
 * that crate is the present path.
 *
 * Wawona reconstruction of the IOMobileFramebuffer userspace ABI.
 *
 * Source: public reverse engineering (Apple Wiki Dev:IOMobileFramebuffer,
 * Zodttd/RecordMyScreen, anthonya1999 gist, screendump 6-arg SwapSetLayer,
 * aiaf _kern_* IOConnect map). Not an Apple header. We dlopen/dlsym; this
 * file is the contract Wawona Desktop Replacement on TrollStore implements.
 *
 * Kernel userclient (stable selectors):
 *   3 getDefaultSurface
 *   4 swapBegin          -> swap token
 *   5 swapEnd            -> SwapArg (IOSurface IDs per layer)
 *   6 swapWait           -> token + wait options
 *   8 getDisplaySize
 *   9 setVSyncNotifications
 *  12 requestPowerChange
 *  52 swapCancel         -> one token (not "cancel everyone")
 *
 * Userspace wrappers we resolve:
 *   IOMobileFramebufferGetMainDisplay
 *   IOMobileFramebufferGetDisplaySize
 *   IOMobileFramebufferSwapBegin
 *   IOMobileFramebufferSwapSetLayer   (modern 6-arg: src, dst, flags)
 *   IOMobileFramebufferSwapEnd
 *   IOMobileFramebufferSwapWait       (token, options)
 *   IOMobileFramebufferGetLayerDefaultSurface  (SpringBoard CA surface)
 *   IOMobileFramebufferEnableDisableVideoPowerSavings
 *
 * Desktop Replacement rules:
 *   Present only Wawona IOSurfaces. Never paint GetLayerDefaultSurface.
 *   Wait every swap. Restore by putting the default surface back.
 */
#ifndef WWN_IOMOBILEFRAMEBUFFER_RE_H
#define WWN_IOMOBILEFRAMEBUFFER_RE_H

#include <CoreGraphics/CoreGraphics.h>
#include <IOSurface/IOSurfaceRef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *IOMobileFramebufferRef;
typedef int32_t IOMobileFramebufferReturn;
typedef CGSize IOMobileFramebufferDisplaySize;

#define WWN_IOMFB_LAYER_COUNT 3
#define WWN_IOMFB_WAIT_UNTIL_DISPLAYED 0

#ifdef __cplusplus
}
#endif

#endif
