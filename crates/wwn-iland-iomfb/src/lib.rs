//! Rust-owned policy and lifecycle for the iOS Mode B IOMFB display sink.
//!
//! Private Apple SPI and Objective-C Metal objects stay in the platform
//! trampoline. This crate owns state transitions, buffer selection, damage,
//! frame accounting, validation, and the stable C ABI consumed by Wawona.

use std::ffi::{c_char, c_void};
use std::ptr;

const WWN_IOMFB_OK: i32 = 0;
const WWN_IOMFB_INVALID: i32 = -1;
const WWN_IOMFB_PLATFORM: i32 = -2;
const WWN_IOMFB_STATE: i32 = -3;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct WwnIomfbDamage {
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct WwnIomfbSurface {
    pub iosurface: *mut c_void,
    pub id: u32,
    pub width: u32,
    pub height: u32,
    pub bytes_per_row: u32,
}

impl Default for WwnIomfbSurface {
    fn default() -> Self {
        Self {
            iosurface: ptr::null_mut(),
            id: 0,
            width: 0,
            height: 0,
            bytes_per_row: 0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum SinkState {
    Open,
    Restored,
}

struct Session {
    platform: *mut c_void,
    width: u32,
    height: u32,
    front: u32,
    frame: u64,
    state: SinkState,
    last_error: [u8; 192],
}

impl Session {
    fn set_error(&mut self, message: &str) {
        self.last_error.fill(0);
        let bytes = message.as_bytes();
        let count = bytes.len().min(self.last_error.len() - 1);
        self.last_error[..count].copy_from_slice(&bytes[..count]);
    }

    fn validate_damage(&self, damage: WwnIomfbDamage) -> bool {
        if damage.width == 0 || damage.height == 0 {
            return true;
        }
        damage.x <= self.width
            && damage.y <= self.height
            && damage.width <= self.width.saturating_sub(damage.x)
            && damage.height <= self.height.saturating_sub(damage.y)
    }

    fn active(&mut self) -> Result<(), i32> {
        if self.state != SinkState::Open || self.platform.is_null() {
            self.set_error("IOMFB session is not open");
            Err(WWN_IOMFB_STATE)
        } else {
            Ok(())
        }
    }
}

extern "C" {
    fn wwn_iomfb_platform_open(
        out_width: *mut u32,
        out_height: *mut u32,
        error: *mut c_char,
        error_capacity: usize,
    ) -> *mut c_void;
    fn wwn_iomfb_platform_acquire(
        platform: *mut c_void,
        index: u32,
        out_surface: *mut WwnIomfbSurface,
    ) -> i32;
    fn wwn_iomfb_platform_present_surface(
        platform: *mut c_void,
        iosurface: *mut c_void,
        damage: WwnIomfbDamage,
        frame: u64,
    ) -> i32;
    fn wwn_iomfb_platform_present_texture(
        platform: *mut c_void,
        texture: *mut c_void,
        damage: WwnIomfbDamage,
        frame: u64,
        destination_index: u32,
    ) -> i32;
    fn wwn_iomfb_platform_restore(platform: *mut c_void) -> i32;
    fn wwn_iomfb_platform_destroy(platform: *mut c_void);
}

/// Opens the own-display sink. Available only in the separately linked Mode B product.
#[no_mangle]
pub unsafe extern "C" fn wwn_iomfb_open(out_session: *mut *mut c_void) -> i32 {
    if out_session.is_null() {
        return WWN_IOMFB_INVALID;
    }
    *out_session = ptr::null_mut();
    let mut width = 0;
    let mut height = 0;
    let mut error = [0_i8; 192];
    let platform =
        wwn_iomfb_platform_open(&mut width, &mut height, error.as_mut_ptr(), error.len());
    if platform.is_null() || width == 0 || height == 0 {
        return WWN_IOMFB_PLATFORM;
    }
    let session = Box::new(Session {
        platform,
        width,
        height,
        front: 0,
        frame: 0,
        state: SinkState::Open,
        last_error: [0; 192],
    });
    eprintln!(
        "wwn.iomfb op=open result=ok width={} height={}",
        width, height
    );
    *out_session = Box::into_raw(session).cast();
    WWN_IOMFB_OK
}

#[no_mangle]
pub unsafe extern "C" fn wwn_iomfb_acquire(
    opaque: *mut c_void,
    out_surface: *mut WwnIomfbSurface,
) -> i32 {
    let Some(session) = opaque.cast::<Session>().as_mut() else {
        return WWN_IOMFB_INVALID;
    };
    if out_surface.is_null() {
        session.set_error("acquire output is null");
        return WWN_IOMFB_INVALID;
    }
    if let Err(code) = session.active() {
        return code;
    }
    let back = 1 - session.front;
    let result = wwn_iomfb_platform_acquire(session.platform, back, out_surface);
    if result != 0 {
        session.set_error("platform failed to acquire IOMFB back buffer");
        return WWN_IOMFB_PLATFORM;
    }
    WWN_IOMFB_OK
}

#[no_mangle]
pub unsafe extern "C" fn wwn_iomfb_present_iosurface(
    opaque: *mut c_void,
    iosurface: *mut c_void,
    damage: WwnIomfbDamage,
) -> i32 {
    let Some(session) = opaque.cast::<Session>().as_mut() else {
        return WWN_IOMFB_INVALID;
    };
    if let Err(code) = session.active() {
        return code;
    }
    if iosurface.is_null() || !session.validate_damage(damage) {
        session.set_error("invalid IOSurface or damage");
        return WWN_IOMFB_INVALID;
    }
    let result =
        wwn_iomfb_platform_present_surface(session.platform, iosurface, damage, session.frame);
    if result != 0 {
        session.set_error("direct IOSurface presentation failed");
        return WWN_IOMFB_PLATFORM;
    }
    session.front = 1 - session.front;
    session.frame = session.frame.wrapping_add(1);
    WWN_IOMFB_OK
}

#[no_mangle]
pub unsafe extern "C" fn wwn_iomfb_present_metal_texture(
    opaque: *mut c_void,
    texture: *mut c_void,
    damage: WwnIomfbDamage,
) -> i32 {
    let Some(session) = opaque.cast::<Session>().as_mut() else {
        return WWN_IOMFB_INVALID;
    };
    if let Err(code) = session.active() {
        return code;
    }
    if texture.is_null() || !session.validate_damage(damage) {
        session.set_error("invalid Metal texture or damage");
        return WWN_IOMFB_INVALID;
    }
    let back = 1 - session.front;
    let result =
        wwn_iomfb_platform_present_texture(session.platform, texture, damage, session.frame, back);
    if result != 0 {
        session.set_error("Metal GPU blit fallback failed");
        return WWN_IOMFB_PLATFORM;
    }
    session.front = back;
    session.frame = session.frame.wrapping_add(1);
    WWN_IOMFB_OK
}

#[no_mangle]
pub unsafe extern "C" fn wwn_iomfb_restore(opaque: *mut c_void) -> i32 {
    let Some(session) = opaque.cast::<Session>().as_mut() else {
        return WWN_IOMFB_INVALID;
    };
    if session.state == SinkState::Restored {
        return WWN_IOMFB_OK;
    }
    let result = wwn_iomfb_platform_restore(session.platform);
    if result != 0 {
        session.set_error("IOMFB display restore failed");
        return WWN_IOMFB_PLATFORM;
    }
    session.state = SinkState::Restored;
    eprintln!("wwn.iomfb op=restore result=ok frames={}", session.frame);
    WWN_IOMFB_OK
}

#[no_mangle]
pub unsafe extern "C" fn wwn_iomfb_last_error(opaque: *mut c_void) -> *const c_char {
    let Some(session) = opaque.cast::<Session>().as_ref() else {
        return ptr::null();
    };
    session.last_error.as_ptr().cast()
}

#[no_mangle]
pub unsafe extern "C" fn wwn_iomfb_destroy(opaque: *mut c_void) {
    if opaque.is_null() {
        return;
    }
    let mut session = Box::from_raw(opaque.cast::<Session>());
    if session.state == SinkState::Open {
        let _ = wwn_iomfb_platform_restore(session.platform);
        session.state = SinkState::Restored;
    }
    wwn_iomfb_platform_destroy(session.platform);
    session.platform = ptr::null_mut();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn damage_is_bounded() {
        let session = Session {
            platform: ptr::dangling_mut::<c_void>(),
            width: 1920,
            height: 1080,
            front: 0,
            frame: 0,
            state: SinkState::Open,
            last_error: [0; 192],
        };
        assert!(session.validate_damage(WwnIomfbDamage::default()));
        assert!(session.validate_damage(WwnIomfbDamage {
            x: 10,
            y: 20,
            width: 100,
            height: 200,
        }));
        assert!(!session.validate_damage(WwnIomfbDamage {
            x: 1900,
            y: 0,
            width: 100,
            height: 10,
        }));
    }
}
