/*
 * Copyright (C) 2018 Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * Copyright (C) 2019-2021 Nils Asmussen, Barkhausen Institut
 *
 * This file is part of M3 (Microkernel-based SysteM for Heterogeneous Manycores).
 *
 * M3 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * M3 is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

//! Contains utilities

pub mod math;
pub mod parse;
pub mod random;

use core::intrinsics;
use core::slice;

use crate::libc;
use crate::mem;

/// Converts the given C string into a string slice
///
/// # Safety
///
/// This function assumes that `s` points to a permanently valid and null-terminated C string
pub unsafe fn cstr_to_str(s: *const i8) -> &'static str {
    let len = libc::strlen(s);
    let sl = slice::from_raw_parts(s, len + 1);
    &*(&sl[..sl.len() - 1] as *const [i8] as *const str)
}

/// Creates a slice of `T`s for the given address range
///
/// # Safety
///
/// This function assumes that `start` points to a permanently valid array of `size` bytes
/// containing `T`s
pub unsafe fn slice_for<T>(start: *const T, size: usize) -> &'static [T] {
    slice::from_raw_parts(start, size)
}

/// Creates a mutable slice of `T`s for the given address range
///
/// # Safety
///
/// This function assumes that `start` points to a permanently valid and writable array of `size`
/// bytes containing `T`s
pub unsafe fn slice_for_mut<T>(start: *mut T, size: usize) -> &'static mut [T] {
    slice::from_raw_parts_mut(start, size)
}

/// Creates a byte slice for the given object
pub fn object_to_bytes<T: Sized>(obj: &T) -> &[u8] {
    let p: *const T = obj;
    let p: *const u8 = p as *const u8;
    unsafe { slice::from_raw_parts(p, mem::size_of::<T>()) }
}

/// Creates a mutable byte slice for the given object
pub fn object_to_bytes_mut<T: Sized>(obj: &mut T) -> &mut [u8] {
    let p: *mut T = obj;
    let p: *mut u8 = p as *mut u8;
    unsafe { slice::from_raw_parts_mut(p, mem::size_of::<T>()) }
}

/// Wrapper for `intrinsics::unlikely`.
///
/// Tells the compiler that the given condition will likely be false.
#[inline(always)]
pub fn unlikely(cond: bool) -> bool {
    intrinsics::unlikely(cond)
}

/// Expands to the current function name.
#[macro_export]
macro_rules! function {
    () => {{
        fn f() {
        }
        fn type_name_of<T>(_: T) -> &'static str {
            unsafe { core::intrinsics::type_name::<T>() }
        }
        let name = type_name_of(f);
        &name[0..name.len() - 3]
    }};
}
