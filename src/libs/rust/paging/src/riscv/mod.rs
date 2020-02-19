/*
 * Copyright (C) 2018, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
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

use base::cfg;
use base::kif::PageFlags;

pub type MMUPTE = u64;

pub const PTE_BITS: usize = 3;

pub const LEVEL_CNT: usize = 3;
pub const LEVEL_BITS: usize = cfg::PAGE_BITS - PTE_BITS;
pub const LEVEL_MASK: usize = (1 << LEVEL_BITS) - 1;

pub const MODE_SV39: u64 = 8;

bitflags! {
    pub struct MMUFlags : MMUPTE {
        const V     = 0b0000_0001;          // valid
        const R     = 0b0000_0010;          // readable
        const W     = 0b0000_0100;          // writable
        const X     = 0b0000_1000;          // executable
        const U     = 0b0001_0000;          // user accessible
        const G     = 0b0010_0000;          // global
        const A     = 0b0100_0000;          // accessed
        const D     = 0b1000_0000;          // dirty

        const RW    = Self::V.bits | Self::R.bits | Self::W.bits;
        const RWX   = Self::RW.bits | Self::X.bits;

        const FLAGS = 0xFFu64;
    }
}

impl MMUFlags {
    pub fn is_leaf(&self, _level: usize) -> bool {
        (*self & (Self::R | Self::W | Self::X)) != Self::empty()
    }

    pub fn perms_missing(&self, perms: Self) -> bool {
        if !self.contains(Self::V) {
            return true;
        }
        self.is_leaf(0) && (*self & perms) != perms
    }
}

pub fn build_pte(phys: MMUPTE, perm: MMUFlags, _level: usize, _leaf: bool) -> MMUPTE {
    (phys >> 2) | (MMUFlags::V | perm).bits()
}

pub fn pte_to_phys(pte: MMUPTE) -> MMUPTE {
    (pte & !MMUFlags::FLAGS.bits()) << 2
}

pub fn needs_invalidate(_new_flags: MMUFlags, old_flags: MMUFlags) -> bool {
    // invalidate the TLB entry on every change
    old_flags.bits() != 0
}

#[no_mangle]
pub extern "C" fn to_page_flags(level: usize, pte: MMUFlags) -> PageFlags {
    let mut res = PageFlags::empty();
    if pte.contains(MMUFlags::V) {
        res |= PageFlags::R;
    }
    else {
        return res;
    }

    if pte.contains(MMUFlags::W) {
        res |= PageFlags::W;
    }
    if pte.contains(MMUFlags::X) {
        res |= PageFlags::X;
    }
    if pte.contains(MMUFlags::U) {
        res |= PageFlags::U;
    }
    if level > 0 {
        res |= PageFlags::L;
    }
    res
}

pub fn to_mmu_perms(flags: PageFlags) -> MMUFlags {
    let mut res = MMUFlags::empty();
    if flags.intersects(PageFlags::RWX) {
        res |= MMUFlags::V;
    }
    if flags.contains(PageFlags::R) {
        res |= MMUFlags::R;
    }
    if flags.contains(PageFlags::W) {
        res |= MMUFlags::W;
    }
    if flags.contains(PageFlags::X) {
        res |= MMUFlags::X;
    }
    if flags.contains(PageFlags::U) {
        res |= MMUFlags::U;
    }
    res
}

#[no_mangle]
pub extern "C" fn enable_paging() {
    // set sstatus.SUM = 1 to allow accesses to user memory (required for DTU)
    unsafe {
        asm!(
            "csrs sstatus, $0"
            : : "r"(1 << 18)
            : : "volatile"
        );
    }
}

pub fn invalidate_page(id: u64, virt: usize) {
    unsafe {
        asm!(
            "sfence.vma $0, $1"
            : : "r"(virt), "r"(id)
            : : "volatile"
        );
    }
}

pub fn invalidate_tlb() {
    unsafe { asm!("sfence.vma" : : : : "volatile"); }
}

pub fn get_root_pt() -> MMUPTE {
    let satp: u64;
    unsafe { asm!("csrr $0, satp" : "=r"(satp)); }
    (satp & 0xFFF_FFFF_FFFF) << cfg::PAGE_BITS
}

pub fn set_root_pt(id: u64, root: MMUPTE) {
    let satp: u64 = MODE_SV39 << 60 | id << 44 | (root >> cfg::PAGE_BITS);
    unsafe { asm!("csrw satp, $0" : : "r"(satp)); }
}

#[no_mangle]
pub extern "C" fn noc_to_phys(noc: u64) -> u64 {
    (noc & !0xFF00000000000000) | ((noc & 0xFF00000000000000) >> 8)
}

#[no_mangle]
pub extern "C" fn phys_to_noc(phys: u64) -> u64 {
    (phys & !0x00FF_0000_0000_0000) | ((phys & 0x00FF_0000_0000_0000) << 8)
}
