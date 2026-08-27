#ifndef ILAND_WATCH_IOSURFACE_STUB_H
#define ILAND_WATCH_IOSURFACE_STUB_H

#include <stddef.h>
#include <stdint.h>
#include <mach/mach.h>

/* Watch SDK has no IOSurface.framework (CGBase may forward-declare
 * IOSurfaceRef). Malloc-backed stub for GBM/DRM bookkeeping; present path is
 * wl_shm (ILAND_WATCH_SHM_WINSYS). Do not include CoreFoundation in the same
 * TU as these CFRetain/CFRelease remaps. */

typedef struct ILandWatchSurface *IOSurfaceRef;

IOSurfaceRef ILandWatchSurfaceCreate(uint32_t width, uint32_t height, uint32_t bpe);
IOSurfaceRef ILandWatchSurfaceLookup(uint32_t id);
void ILandWatchSurfaceRetain(IOSurfaceRef surf);
void ILandWatchSurfaceRelease(IOSurfaceRef surf);
uint32_t ILandWatchSurfaceGetID(IOSurfaceRef surf);
uint32_t ILandWatchSurfaceGetWidth(IOSurfaceRef surf);
uint32_t ILandWatchSurfaceGetHeight(IOSurfaceRef surf);
size_t ILandWatchSurfaceGetBytesPerRow(IOSurfaceRef surf);
size_t ILandWatchSurfaceGetAllocSize(IOSurfaceRef surf);
void *ILandWatchSurfaceGetBaseAddress(IOSurfaceRef surf);
int ILandWatchSurfaceLock(IOSurfaceRef surf);
int ILandWatchSurfaceUnlock(IOSurfaceRef surf);

#define IOSurfaceGetID ILandWatchSurfaceGetID
#define IOSurfaceGetWidth ILandWatchSurfaceGetWidth
#define IOSurfaceGetHeight ILandWatchSurfaceGetHeight
#define IOSurfaceGetBytesPerRow ILandWatchSurfaceGetBytesPerRow
#define IOSurfaceGetAllocSize ILandWatchSurfaceGetAllocSize
#define IOSurfaceGetBaseAddress ILandWatchSurfaceGetBaseAddress
#define IOSurfaceLock(s, ...) ILandWatchSurfaceLock(s)
#define IOSurfaceUnlock(s, ...) ILandWatchSurfaceUnlock(s)
#define IOSurfaceLookup ILandWatchSurfaceLookup
#define CFRetain ILandWatchSurfaceRetain
#define CFRelease ILandWatchSurfaceRelease
#define IOSurfaceSetValue(s, k, v) ((void)(s), (void)(k), (void)(v))
#ifndef IOSurfaceCreateMachPort
#define IOSurfaceCreateMachPort(s) ((mach_port_t)0)
#endif

#ifndef kIOReturnSuccess
#define kIOReturnSuccess 0
#endif
#ifndef kIOSurfaceLockReadOnly
#define kIOSurfaceLockReadOnly 1
#endif
#ifndef CFSTR
#define CFSTR(x) ((const void *)(x))
#endif
#ifndef kCFBooleanTrue
#define kCFBooleanTrue ((const void *)1)
#endif

/* Android gbm.c names (same malloc surface ABI). */
#define ILandIOSurfaceRelease ILandWatchSurfaceRelease
#define ILandIOSurfaceLookup ILandWatchSurfaceLookup
#define ILandIOSurfaceRetain ILandWatchSurfaceRetain

#endif
