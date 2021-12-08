/*
 * Copyright (C) 2016-2018, Nils Asmussen <nils@os.inf.tu-dresden.de>
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

#include <m3/com/GateStream.h>
#include <m3/session/Pager.h>
#include <m3/pes/VPE.h>

namespace m3 {

Pager::Pager(capsel_t sess, bool)
    : RefCounted(),
      ClientSession(sess),
      _rgate(RecvGate::create(nextlog2<64>::val, nextlog2<64>::val)),
      _own_sgate(SendGate::bind(get_sgate())),
      _child_sgate(SendGate::bind(get_sgate())),
      _child_sep(),
      _child_rep(),
      _close(true) {
}

Pager::Pager(capsel_t sess)
    : RefCounted(),
      ClientSession(sess),
      _rgate(RecvGate::bind(ObjCap::INVALID, nextlog2<64>::val, nextlog2<64>::val)),
      _own_sgate(SendGate::bind(get_sgate())),
      _child_sgate(SendGate::bind(ObjCap::INVALID)),
      _child_sep(),
      _child_rep(),
      _close(false) {
}

Pager::~Pager() {
    if(_close) {
        try {
            send_receive_vmsg(_own_sgate, CLOSE);
        }
        catch(...) {
            // ignore
        }
    }
}

capsel_t Pager::get_sgate() {
    KIF::ExchangeArgs args;
    ExchangeOStream os(args);
    os << Operation::ADD_SGATE;
    args.bytes = os.total();
    return obtain(1, &args).start();
}

void Pager::pagefault(goff_t addr, uint access) {
    GateIStream reply = send_receive_vmsg(_own_sgate, PAGEFAULT, addr, access);
    reply.pull_result();
}

void Pager::map_anon(goff_t *virt, size_t len, int prot, int flags) {
    GateIStream reply = send_receive_vmsg(_own_sgate, MAP_ANON, *virt, len, prot, flags);
    reply.pull_result();
    reply >> *virt;
}

void Pager::map_ds(goff_t *virt, size_t len, int prot, int flags, const ClientSession &sess,
                   size_t offset) {
    KIF::ExchangeArgs args;
    ExchangeOStream os(args);
    os << Operation::MAP_DS << *virt << len << prot << flags << offset;
    args.bytes = os.total();

    delegate(KIF::CapRngDesc(KIF::CapRngDesc::OBJ, sess.sel()), &args);

    ExchangeIStream is(args);
    is >> *virt;
}

void Pager::map_mem(goff_t *virt, MemGate &mem, size_t len, int prot) {
    KIF::ExchangeArgs args;
    ExchangeOStream os(args);
    os << Operation::MAP_MEM << *virt << len << prot;
    args.bytes = os.total();

    delegate(KIF::CapRngDesc(KIF::CapRngDesc::OBJ, mem.sel()), &args);

    ExchangeIStream is(args);
    is >> *virt;
}

void Pager::unmap(goff_t virt) {
    GateIStream reply = send_receive_vmsg(_own_sgate, UNMAP, virt);
    reply.pull_result();
}

Reference<Pager> Pager::create_clone() {
    KIF::CapRngDesc caps;
    {
        KIF::ExchangeArgs args;
        ExchangeOStream os(args);
        os << Operation::ADD_CHILD;
        args.bytes = os.total();
        caps = obtain(1, &args);
    }

    return Reference<Pager>(new Pager(caps.start(), true));
}

void Pager::init(VPE &vpe, epid_t eps_start) {
    // activate send and receive gate for page faults
    _child_sep.reset(vpe.epmng().acquire(eps_start + TCU::PG_SEP_OFF));
    _child_sgate.activate_on(*_child_sep);
    _child_rep.reset(vpe.epmng().acquire(eps_start + TCU::PG_REP_OFF));
    _rgate.Gate::activate_on(*_child_rep);

    // we only need to do that for clones
    if(_close) {
        KIF::ExchangeArgs args;
        ExchangeOStream os(args);
        os << Operation::INIT;
        args.bytes = os.total();
        delegate(KIF::CapRngDesc(KIF::CapRngDesc::OBJ, vpe.sel()), &args);
    }
}

void Pager::clone() {
    GateIStream reply = send_receive_vmsg(_own_sgate, CLONE);
    reply.pull_result();
}

}
