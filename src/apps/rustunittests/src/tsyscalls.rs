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

use m3::cfg::PAGE_SIZE;
use m3::com::{MemGate, RecvGate, SendGate, EP};
use m3::errors::Code;
use m3::kif::syscalls::{ExchangeArgs, VPEOp};
use m3::kif::{CapRngDesc, CapType, Perm, FIRST_FREE_SEL, INVALID_SEL, SEL_MEM, SEL_PE, SEL_VPE};
use m3::pes::{PE, VPE};
use m3::session::M3FS;
use m3::syscalls;
use m3::test;

pub fn run(t: &mut dyn test::WvTester) {
    wv_run_test!(t, create_srv);
    wv_run_test!(t, create_sgate);
    wv_run_test!(t, create_rgate);
    wv_run_test!(t, create_sess);
    wv_run_test!(t, create_map);
    wv_run_test!(t, create_vpe);

    wv_run_test!(t, activate);
    wv_run_test!(t, derive_mem);
    wv_run_test!(t, vpe_ctrl);
    wv_run_test!(t, vpe_wait);

    wv_run_test!(t, exchange);
    wv_run_test!(t, delegate);
    wv_run_test!(t, obtain);
    wv_run_test!(t, revoke);
}

fn create_srv() {
    let sel = VPE::cur().alloc_sel();
    let mut rgate = wv_assert_ok!(RecvGate::new(10, 10));

    // invalid dest selector
    wv_assert_err!(
        syscalls::create_srv(SEL_VPE, VPE::cur().sel(), rgate.sel(), "test"),
        Code::InvArgs
    );

    // invalid rgate selector
    wv_assert_err!(
        syscalls::create_srv(sel, VPE::cur().sel(), SEL_VPE, "test"),
        Code::InvArgs
    );
    // again, with real rgate, but not activated
    wv_assert_err!(
        syscalls::create_srv(sel, VPE::cur().sel(), rgate.sel(), "test"),
        Code::InvArgs
    );
    wv_assert_ok!(rgate.activate());

    // invalid VPE selector
    wv_assert_err!(
        syscalls::create_srv(sel, SEL_MEM, rgate.sel(), "test"),
        Code::InvArgs
    );

    // invalid name
    wv_assert_err!(
        syscalls::create_srv(sel, VPE::cur().sel(), rgate.sel(), ""),
        Code::InvArgs
    );
}

fn create_sgate() {
    let sel = VPE::cur().alloc_sel();
    let rgate = wv_assert_ok!(RecvGate::new(10, 10));

    // invalid dest selector
    wv_assert_err!(
        syscalls::create_sgate(SEL_VPE, rgate.sel(), 0xDEAD_BEEF, 123),
        Code::InvArgs
    );
    // invalid rgate selector
    wv_assert_err!(
        syscalls::create_sgate(sel, SEL_VPE, 0xDEAD_BEEF, 123),
        Code::InvArgs
    );
}

fn create_rgate() {
    let sel = VPE::cur().alloc_sel();

    // invalid dest selector
    wv_assert_err!(syscalls::create_rgate(SEL_VPE, 10, 10), Code::InvArgs);
    // invalid order
    wv_assert_err!(syscalls::create_rgate(sel, 2000, 10), Code::InvArgs);
    wv_assert_err!(syscalls::create_rgate(sel, -1, 10), Code::InvArgs);
    // invalid msg order
    wv_assert_err!(syscalls::create_rgate(sel, 10, 11), Code::InvArgs);
    wv_assert_err!(syscalls::create_rgate(sel, 10, -1), Code::InvArgs);
    // invalid order and msg order
    wv_assert_err!(syscalls::create_rgate(sel, -1, -1), Code::InvArgs);
}

fn create_sess() {
    let srv = VPE::cur().alloc_sel();
    let mut rgate = wv_assert_ok!(RecvGate::new(10, 10));
    wv_assert_ok!(rgate.activate());
    wv_assert_ok!(syscalls::create_srv(
        srv,
        VPE::cur().sel(),
        rgate.sel(),
        "test"
    ));

    let sel = VPE::cur().alloc_sel();

    // invalid dest selector
    wv_assert_err!(syscalls::create_sess(SEL_VPE, srv, 0), Code::InvArgs);
    // invalid service selector
    wv_assert_err!(syscalls::create_sess(sel, SEL_VPE, 0), Code::InvArgs);

    wv_assert_ok!(syscalls::revoke(
        VPE::cur().sel(),
        CapRngDesc::new(CapType::OBJECT, srv, 1),
        true
    ));
}

#[allow(clippy::cognitive_complexity)]
fn create_map() {
    if !VPE::cur().pe_desc().has_virtmem() {
        return;
    }

    let meminv = wv_assert_ok!(MemGate::new(64, Perm::RW)); // not page-granular
    let mem = wv_assert_ok!(MemGate::new(PAGE_SIZE * 4, Perm::RW));

    // invalid VPE selector
    wv_assert_err!(
        syscalls::create_map(0, SEL_MEM, mem.sel(), 0, 4, Perm::RW),
        Code::InvArgs
    );
    // invalid memgate selector
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), SEL_VPE, 0, 4, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), meminv.sel(), 0, 4, Perm::RW),
        Code::InvArgs
    );
    // invalid first page
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), 4, 4, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), !0, 4, Perm::RW),
        Code::InvArgs
    );
    // invalid page count
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), 0, 5, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), 3, 2, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), 4, 0, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), !0, !0, Perm::RW),
        Code::InvArgs
    );
    // invalid permissions
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), 0, 4, Perm::X),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_map(0, VPE::cur().sel(), mem.sel(), 0, 4, Perm::RWX),
        Code::InvArgs
    );
}

#[allow(clippy::cognitive_complexity)]
fn create_vpe() {
    let cap_count = FIRST_FREE_SEL;
    let sels = VPE::cur().alloc_sels(cap_count);
    let crd = CapRngDesc::new(CapType::OBJECT, sels, cap_count);
    let rgate = wv_assert_ok!(RecvGate::new(10, 10));
    let sgate = wv_assert_ok!(SendGate::new(&rgate));
    let kmem = VPE::cur().kmem().sel();

    let pe = wv_assert_ok!(PE::new(&VPE::cur().pe_desc()));

    // invalid dest caps
    wv_assert_err!(
        syscalls::create_vpe(
            CapRngDesc::new(CapType::OBJECT, SEL_VPE, cap_count),
            INVALID_SEL,
            INVALID_SEL,
            "test",
            pe.sel(),
            kmem
        ),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_vpe(
            CapRngDesc::new(CapType::OBJECT, sels, 0),
            INVALID_SEL,
            INVALID_SEL,
            "test",
            pe.sel(),
            kmem
        ),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_vpe(
            CapRngDesc::new(CapType::OBJECT, sels, cap_count - 1),
            INVALID_SEL,
            INVALID_SEL,
            "test",
            pe.sel(),
            kmem
        ),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_vpe(
            CapRngDesc::new(CapType::OBJECT, sels, !0),
            INVALID_SEL,
            INVALID_SEL,
            "test",
            pe.sel(),
            kmem
        ),
        Code::InvArgs
    );

    // invalid sgate
    wv_assert_err!(
        syscalls::create_vpe(crd, SEL_VPE, INVALID_SEL, "test", pe.sel(), kmem),
        Code::InvArgs
    );

    // invalid name
    wv_assert_err!(
        syscalls::create_vpe(crd, sgate.sel(), INVALID_SEL, "", pe.sel(), kmem),
        Code::InvArgs
    );

    // invalid kmem
    wv_assert_err!(
        syscalls::create_vpe(crd, sgate.sel(), INVALID_SEL, "test", pe.sel(), INVALID_SEL),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::create_vpe(crd, sgate.sel(), INVALID_SEL, "test", pe.sel(), 1),
        Code::InvArgs
    );
}

fn activate() {
    let ep1 = wv_assert_ok!(EP::new());
    let ep2 = wv_assert_ok!(EP::new());
    let sel = VPE::cur().alloc_sel();
    let mgate = wv_assert_ok!(MemGate::new(0x1000, Perm::RW));

    // invalid EP sel
    wv_assert_err!(syscalls::activate(SEL_VPE, mgate.sel(), 0), Code::InvArgs);
    wv_assert_err!(syscalls::activate(sel, mgate.sel(), 0), Code::InvArgs);
    // invalid mgate sel
    wv_assert_err!(syscalls::activate(ep1.sel(), SEL_VPE, 0), Code::InvArgs);
    // invalid address
    wv_assert_err!(
        syscalls::activate(ep1.sel(), mgate.sel(), 0x1000),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::activate(ep1.sel(), mgate.sel(), !0),
        Code::InvArgs
    );
    // already activated
    wv_assert_ok!(syscalls::activate(ep1.sel(), mgate.sel(), 0));
    wv_assert_err!(syscalls::activate(ep2.sel(), mgate.sel(), 0), Code::Exists);
}

fn derive_mem() {
    let vpe = VPE::cur().sel();
    let sel = VPE::cur().alloc_sel();
    let mem = wv_assert_ok!(MemGate::new(0x4000, Perm::RW));

    // invalid dest selector
    wv_assert_err!(
        syscalls::derive_mem(vpe, SEL_VPE, mem.sel(), 0, 0x1000, Perm::RW),
        Code::InvArgs
    );
    // invalid mem
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, SEL_VPE, 0, 0x1000, Perm::RW),
        Code::InvArgs
    );
    // invalid offset
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, mem.sel(), 0x4000, 0x1000, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, mem.sel(), !0, 0x1000, Perm::RW),
        Code::InvArgs
    );
    // invalid size
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, mem.sel(), 0, 0x4001, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, mem.sel(), 0x2000, 0x2001, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, mem.sel(), 0x2000, 0, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, mem.sel(), 0x4000, 0, Perm::RW),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::derive_mem(vpe, sel, mem.sel(), !0, !0, Perm::RW),
        Code::InvArgs
    );
    // perms are arbitrary; will be ANDed
}

fn vpe_ctrl() {
    wv_assert_err!(syscalls::vpe_ctrl(SEL_MEM, VPEOp::START, 0), Code::InvArgs);
    wv_assert_err!(
        syscalls::vpe_ctrl(INVALID_SEL, VPEOp::START, 0),
        Code::InvArgs
    );
    // can't start ourself
    wv_assert_err!(
        syscalls::vpe_ctrl(VPE::cur().sel(), VPEOp::START, 0),
        Code::InvArgs
    );
}

fn vpe_wait() {
    wv_assert_err!(syscalls::vpe_wait(&[], 0), Code::InvArgs);
}

fn exchange() {
    let pe = wv_assert_ok!(PE::new(&VPE::cur().pe_desc()));
    let mut child = wv_assert_ok!(VPE::new(&pe, "test"));
    let csel = child.alloc_sel();

    let sel = VPE::cur().alloc_sel();
    let unused = CapRngDesc::new(CapType::OBJECT, sel, 1);
    let used = CapRngDesc::new(CapType::OBJECT, 0, 1);

    // invalid VPE sel
    wv_assert_err!(
        syscalls::exchange(SEL_MEM, used, csel, false),
        Code::InvArgs
    );
    // invalid own caps (source caps can be invalid)
    wv_assert_err!(
        syscalls::exchange(VPE::cur().sel(), used, unused.start(), true),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::exchange(child.sel(), used, 0, true),
        Code::InvArgs
    );
    // invalid other caps
    wv_assert_err!(
        syscalls::exchange(VPE::cur().sel(), used, 0, false),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::exchange(child.sel(), used, 0, false),
        Code::InvArgs
    );
}

fn delegate() {
    let m3fs = wv_assert_ok!(M3FS::new("m3fs-clone"));
    let m3fs = m3fs.borrow();
    let sess = m3fs.as_any().downcast_ref::<M3FS>().unwrap().sess();
    let crd = CapRngDesc::new(CapType::OBJECT, SEL_VPE, 1);
    let mut args = ExchangeArgs::default();

    // invalid VPE selector
    wv_assert_err!(
        syscalls::delegate(SEL_MEM, sess.sel(), crd, &mut args),
        Code::InvArgs
    );
    // invalid sess selector
    wv_assert_err!(
        syscalls::delegate(VPE::cur().sel(), SEL_VPE, crd, &mut args),
        Code::InvArgs
    );
    // CRD can be anything (depends on server)
}

fn obtain() {
    let m3fs = wv_assert_ok!(M3FS::new("m3fs-clone"));
    let m3fs = m3fs.borrow();
    let sess = m3fs.as_any().downcast_ref::<M3FS>().unwrap().sess();
    let sel = VPE::cur().alloc_sel();
    let crd = CapRngDesc::new(CapType::OBJECT, sel, 1);
    let inval = CapRngDesc::new(CapType::OBJECT, SEL_VPE, 1);
    let mut args = ExchangeArgs::default();

    // invalid VPE selector
    wv_assert_err!(
        syscalls::obtain(SEL_MEM, sess.sel(), crd, &mut args),
        Code::InvArgs
    );
    // invalid sess selector
    wv_assert_err!(
        syscalls::obtain(VPE::cur().sel(), SEL_VPE, crd, &mut args),
        Code::InvArgs
    );
    // invalid CRD
    wv_assert_err!(
        syscalls::obtain(VPE::cur().sel(), sess.sel(), inval, &mut args),
        Code::InvArgs
    );
}

fn revoke() {
    let crd_pe = CapRngDesc::new(CapType::OBJECT, SEL_PE, 1);
    let crd_vpe = CapRngDesc::new(CapType::OBJECT, SEL_VPE, 1);
    let crd_mem = CapRngDesc::new(CapType::OBJECT, SEL_MEM, 1);

    // invalid VPE selector
    wv_assert_err!(syscalls::revoke(SEL_MEM, crd_vpe, true), Code::InvArgs);
    // can't revoke PE, VPE, or mem cap
    wv_assert_err!(
        syscalls::revoke(VPE::cur().sel(), crd_pe, true),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::revoke(VPE::cur().sel(), crd_vpe, true),
        Code::InvArgs
    );
    wv_assert_err!(
        syscalls::revoke(VPE::cur().sel(), crd_mem, true),
        Code::InvArgs
    );
}
