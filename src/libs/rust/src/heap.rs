use core::intrinsics;
use dtu;
use env;
use libc;
use util;

#[repr(C, packed)]
struct HeapArea {
    pub next: u64,    /* HEAP_USED_BITS set = used */
    pub prev: u64,
    _pad: [u8; 64 - 16],
}

extern {
    static _bss_end: u8;
    static mut heap_begin: *mut HeapArea;
    static mut heap_end: *mut HeapArea;
}

pub fn init() {
    unsafe {
        let begin = &_bss_end as *const u8;
        heap_begin = util::round_up(begin as usize, util::size_of(&*heap_begin)) as *mut HeapArea;

        let env = env::data();
        let end = util::round_up(begin as usize, dtu::PAGE_SIZE) + env.heap_size as usize;
        heap_end = (end as *mut HeapArea).offset(-1);

        let num_areas = heap_begin.offset_to(heap_end).unwrap() as i64;
        let space = num_areas * util::size_of(&*heap_begin) as i64;

        (*heap_end).next = 0;
        (*heap_end).prev = space as u64;

        (*heap_begin).next = space as u64;
        (*heap_begin).prev = 0;
    }
}

#[no_mangle]
pub unsafe extern fn __rdl_alloc(size: usize,
                                 _align: usize,
                                 _err: *mut u8) -> *mut u8 {
    libc::heap_alloc(size)
}

#[no_mangle]
pub unsafe extern fn __rdl_dealloc(ptr: *mut u8,
                                   _size: usize,
                                   _align: usize) {
    libc::heap_free(ptr);
}

#[no_mangle]
pub unsafe extern fn __rdl_realloc(ptr: *mut u8,
                                   _old_size: usize,
                                   _old_align: usize,
                                   new_size: usize,
                                   _new_align: usize,
                                   _err: *mut u8) -> *mut u8 {
    libc::heap_realloc(ptr, new_size)
}

#[no_mangle]
pub unsafe extern fn __rdl_alloc_zeroed(size: usize,
                                        _align: usize,
                                        _err: *mut u8) -> *mut u8 {
    libc::heap_calloc(size, 1)
}

#[no_mangle]
pub unsafe extern fn __rdl_oom(_err: *const u8) -> ! {
    intrinsics::abort();
}

#[no_mangle]
pub unsafe extern fn __rdl_usable_size(_layout: *const u8,
                                       _min: *mut usize,
                                       _max: *mut usize) {
    // TODO implement me
}

#[no_mangle]
pub unsafe extern fn __rdl_alloc_excess(size: usize,
                                        _align: usize,
                                        _excess: *mut usize,
                                        _err: *mut u8) -> *mut u8 {
    // TODO is that correct?
    libc::heap_alloc(size)
}

#[no_mangle]
pub unsafe extern fn __rdl_realloc_excess(ptr: *mut u8,
                                          _old_size: usize,
                                          _old_align: usize,
                                          new_size: usize,
                                          _new_align: usize,
                                          _excess: *mut usize,
                                          _err: *mut u8) -> *mut u8 {
    // TODO is that correct?
    libc::heap_realloc(ptr, new_size)
}

#[no_mangle]
pub unsafe extern fn __rdl_grow_in_place(_ptr: *mut u8,
                                         _old_size: usize,
                                         _old_align: usize,
                                         _new_size: usize,
                                         _new_align: usize) -> u8 {
    // TODO implement me
    0
}

#[no_mangle]
pub unsafe extern fn __rdl_shrink_in_place(_ptr: *mut u8,
                                           _old_size: usize,
                                           _old_align: usize,
                                           _new_size: usize,
                                           _new_align: usize) -> u8 {
    // TODO implement me
    0
}
