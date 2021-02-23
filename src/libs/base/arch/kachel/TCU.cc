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

#include <base/util/Math.h>
#include <base/CPU.h>
#include <base/TCU.h>
#include <base/Init.h>
#include <base/KIF.h>

namespace m3 {

INIT_PRIO_TCU TCU TCU::inst;

void TCU::print(const char *str, size_t len) {
    uintptr_t buffer = buffer_addr();
    const reg_t *rstr = reinterpret_cast<const reg_t*>(str);
    const reg_t *end = reinterpret_cast<const reg_t*>(str + len);
    while(rstr < end) {
        CPU::write8b(buffer, *rstr);
        buffer += sizeof(reg_t);
        rstr++;
    }

    write_reg(UnprivRegs::PRINT, len);
}

Errors::Code TCU::send(epid_t ep, const void *msg, size_t size, label_t replylbl, epid_t reply_ep) {
    uintptr_t msg_addr = reinterpret_cast<uintptr_t>(msg);
    write_reg(UnprivRegs::DATA, static_cast<reg_t>(msg_addr) | (static_cast<reg_t>(size) << 32));
    if(replylbl)
        write_reg(UnprivRegs::ARG1, replylbl);
    CPU::compiler_barrier();
    return perform_send_reply(build_command(ep, CmdOpCode::SEND, reply_ep));
}

Errors::Code TCU::reply(epid_t ep, const void *reply, size_t size, size_t msg_off) {
    assert(size <= 0xFFFFFFFF);
    uintptr_t reply_addr = reinterpret_cast<uintptr_t>(reply);
    write_reg(UnprivRegs::DATA, static_cast<reg_t>(reply_addr) | (static_cast<reg_t>(size) << 32));
    CPU::compiler_barrier();
    return perform_send_reply(build_command(ep, CmdOpCode::REPLY, msg_off));
}

Errors::Code TCU::perform_send_reply(reg_t cmd) {
    while(true) {
        write_reg(UnprivRegs::COMMAND, cmd);

        auto res = get_error();
        if (res != Errors::RECV_BUSY)
            return res;
    }
}

Errors::Code TCU::read(epid_t ep, void *data, size_t size, goff_t off) {
    assert(size <= 0xFFFFFFFF);
    uintptr_t data_addr = reinterpret_cast<uintptr_t>(data);
    write_reg(UnprivRegs::DATA, static_cast<reg_t>(data_addr) | (static_cast<reg_t>(size) << 32));
    write_reg(UnprivRegs::ARG1, off);
    CPU::compiler_barrier();
    write_reg(UnprivRegs::COMMAND, build_command(ep, CmdOpCode::READ));
    Errors::Code res = get_error();
    CPU::memory_barrier();
    return res;
}

Errors::Code TCU::write(epid_t ep, const void *data, size_t size, goff_t off) {
    uintptr_t data_addr = reinterpret_cast<uintptr_t>(data);
    write_reg(UnprivRegs::DATA, static_cast<reg_t>(data_addr) | (static_cast<reg_t>(size) << 32));
    write_reg(UnprivRegs::ARG1, off);
    CPU::compiler_barrier();
    write_reg(UnprivRegs::COMMAND, build_command(ep, CmdOpCode::WRITE));
    return get_error();
}

}
