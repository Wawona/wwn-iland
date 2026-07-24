#ifndef ILAND_IOSURFACE_COMPAT_H
#define ILAND_IOSURFACE_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <android/hardware_buffer.h>

typedef struct ILandIOSurface *IOSurfaceRef;

IOSurfaceRef ILandIOSurfaceCreate(uint32_t width, uint32_t height, uint32_t bpe);
void ILandIOSurfaceRetain(IOSurfaceRef surf);
void ILandIOSurfaceRelease(IOSurfaceRef surf);
uint32_t ILandIOSurfaceGetID(IOSurfaceRef surf);
uint32_t ILandIOSurfaceGetWidth(IOSurfaceRef surf);
uint32_t ILandIOSurfaceGetHeight(IOSurfaceRef surf);
size_t ILandIOSurfaceGetBytesPerRow(IOSurfaceRef surf);
size_t ILandIOSurfaceGetAllocSize(IOSurfaceRef surf);
void *ILandIOSurfaceGetBaseAddress(IOSurfaceRef surf);
AHardwareBuffer *ILandIOSurfaceGetHardwareBuffer(IOSurfaceRef surf);
void ILandIOSurfaceLock(IOSurfaceRef surf);
void ILandIOSurfaceUnlock(IOSurfaceRef surf);

#define IOSurfaceGetID ILandIOSurfaceGetID
#define IOSurfaceGetWidth ILandIOSurfaceGetWidth
#define IOSurfaceGetHeight ILandIOSurfaceGetHeight
#define IOSurfaceGetBytesPerRow ILandIOSurfaceGetBytesPerRow
#define IOSurfaceGetAllocSize ILandIOSurfaceGetAllocSize
#define IOSurfaceGetBaseAddress ILandIOSurfaceGetBaseAddress
#define IOSurfaceLock(s, ...) ILandIOSurfaceLock(s)
#define IOSurfaceUnlock(s, ...) ILandIOSurfaceUnlock(s)
#define CFRetain ILandIOSurfaceRetain
#define CFRelease ILandIOSurfaceRelease

#endif
