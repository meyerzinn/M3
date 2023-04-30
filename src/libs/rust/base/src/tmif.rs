/*
 * Copyright (C) 2021-2022 Nils Asmussen, Barkhausen Institut
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

//! Contains the interface between applications and TileMux

use num_enum::{IntoPrimitive, TryFromPrimitive};

use crate::arch::{TMABIOps, TMABI};
use crate::errors::{Code, Error};
use crate::goff;
use crate::kif;
use crate::tcu::{EpId, INVALID_EP};
use crate::time::TimeDuration;

pub type IRQId = u32;

pub const INVALID_IRQ: IRQId = !0;

/// The operations TileMux supports
#[derive(Copy, Clone, Debug, Eq, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(usize)]
pub enum Operation {
    /// Wait for an event, optionally with timeout
    Wait,
    /// Exit the application
    Exit,
    /// Switch to the next ready activity
    Yield,
    /// Map local physical memory (IO memory)
    Map,
    /// Register for a given interrupt
    RegIRQ,
    /// For TCU TLB misses
    TranslFault,
    /// Flush and invalidate cache
    FlushInv,
    /// Initializes thread-local storage (x86 only)
    InitTLS,
    /// Noop operation for testing purposes
    Noop,
}

pub(crate) fn get_result(res: usize) -> Result<(), Error> {
    Result::from(Code::from(res as u32))
}

#[inline(always)]
pub fn wait(
    ep: Option<EpId>,
    irq: Option<IRQId>,
    duration: Option<TimeDuration>,
) -> Result<(), Error> {
    TMABI::call3(
        Operation::Wait,
        ep.unwrap_or(INVALID_EP) as usize,
        irq.unwrap_or(INVALID_IRQ) as usize,
        match duration {
            Some(d) => d.as_nanos() as usize,
            None => usize::MAX,
        },
    )
    .map(|_| ())
}

pub fn exit(code: Code) -> ! {
    TMABI::call1(Operation::Exit, code as usize).ok();
    unreachable!();
}

pub fn map(virt: usize, phys: goff, pages: usize, access: kif::Perm) -> Result<(), Error> {
    TMABI::call4(
        Operation::Map,
        virt,
        phys as usize,
        pages,
        access.bits() as usize,
    )
}

pub fn reg_irq(irq: IRQId) -> Result<(), Error> {
    TMABI::call1(Operation::RegIRQ, irq as usize)
}

pub fn flush_invalidate() -> Result<(), Error> {
    TMABI::call1(Operation::FlushInv, 0)
}

#[inline(always)]
pub fn switch_activity() -> Result<(), Error> {
    TMABI::call1(Operation::Yield, 0)
}

#[inline(always)]
pub fn noop() -> Result<(), Error> {
    TMABI::call1(Operation::Noop, 0)
}
