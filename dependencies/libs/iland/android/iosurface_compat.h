#ifndef ILAND_IOSURFACE_COMPAT_H
#define ILAND_IOSURFACE_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <android/hardware_buffer.h>

/* NDK AHardwareBuffer.h omits BGRA; HAL_PIXEL_FORMAT_BGRA_8888 == 5 matches
 * Apple GBM's BGRA8888 channel order for XRGB/ARGB8888. */
#ifndef AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
#define AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM 5u
#endif

typedef struct ILandIOSurface *IOSurfaceRef;

/* `ahb_format` is an AHARDWAREBUFFER_FORMAT_* value. Rejects unknown formats
 * (no silent RGBA8 substitute). `bpe` must match the format (4 for 8888 / 2101010). */
IOSurfaceRef ILandIOSurfaceCreate(uint32_t width, uint32_t height, uint32_t bpe,
                                  uint32_t ahb_format);
/* In-process lookup by the id packed into the dmabuf modifier (bit 63 set).
 * Retains; caller must ILandIOSurfaceRelease. NULL if unknown/freed. */
IOSurfaceRef ILandIOSurfaceLookup(uint32_t id);
void ILandIOSurfaceRetain(IOSurfaceRef surf);
void ILandIOSurfaceRelease(IOSurfaceRef surf);
uint32_t ILandIOSurfaceGetID(IOSurfaceRef surf);
uint32_t ILandIOSurfaceGetWidth(IOSurfaceRef surf);
uint32_t ILandIOSurfaceGetHeight(IOSurfaceRef surf);
size_t ILandIOSurfaceGetBytesPerRow(IOSurfaceRef surf);
size_t ILandIOSurfaceGetAllocSize(IOSurfaceRef surf);
void *ILandIOSurfaceGetBaseAddress(IOSurfaceRef surf);
AHardwareBuffer *ILandIOSurfaceGetHardwareBuffer(IOSurfaceRef surf);
/* Return 0 on success so Apple-shaped `!= kIOReturnSuccess` checks compile. */
int ILandIOSurfaceLock(IOSurfaceRef surf);
int ILandIOSurfaceUnlock(IOSurfaceRef surf);

#define IOSurfaceGetID ILandIOSurfaceGetID
#define IOSurfaceGetWidth ILandIOSurfaceGetWidth
#define IOSurfaceGetHeight ILandIOSurfaceGetHeight
#define IOSurfaceGetBytesPerRow ILandIOSurfaceGetBytesPerRow
#define IOSurfaceGetAllocSize ILandIOSurfaceGetAllocSize
#define IOSurfaceGetBaseAddress ILandIOSurfaceGetBaseAddress
#define IOSurfaceLock(s, ...) ILandIOSurfaceLock(s)
#define IOSurfaceUnlock(s, ...) ILandIOSurfaceUnlock(s)
#define IOSurfaceLookup ILandIOSurfaceLookup
#define CFRetain ILandIOSurfaceRetain
#define CFRelease ILandIOSurfaceRelease
/* Apple-only IOSurface metadata; no-op on Android AHB. */
#define IOSurfaceSetValue(s, k, v) ((void)(s), (void)(k), (void)(v))
#ifndef kIOReturnSuccess
#define kIOReturnSuccess 0
#endif
#ifndef CFSTR
#define CFSTR(x) ((const void *)(x))
#endif
#ifndef kCFBooleanTrue
#define kCFBooleanTrue ((const void *)1)
#endif

#endif
