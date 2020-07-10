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

#pragma once

#include <base/Common.h>
#include <base/util/Util.h>
#include <base/CPU.h>
#include <base/Env.h>
#include <base/Errors.h>
#include <assert.h>

namespace kernel {
class TCU;
class TCURegs;
class TCUState;
class ISR;
class SendQueue;
class SyscallHandler;
class VPE;
class PEMux;
class WorkLoop;
}

namespace m3 {

class TCUIf;

class TCU {
    friend class kernel::TCU;
    friend class kernel::TCURegs;
    friend class kernel::TCUState;
    friend class kernel::ISR;
    friend class kernel::SendQueue;
    friend class kernel::SyscallHandler;
    friend class kernel::VPE;
    friend class kernel::PEMux;
    friend class kernel::WorkLoop;
    friend class TCUIf;

    explicit TCU() {
    }

public:
    typedef uint64_t reg_t;

    static const uintptr_t MMIO_ADDR        = 0xF0000000;
    static const size_t MMIO_SIZE           = PAGE_SIZE * 2;
    static const uintptr_t MMIO_PRIV_ADDR   = MMIO_ADDR + MMIO_SIZE;
    static const size_t MMIO_PRIV_SIZE      = PAGE_SIZE;

    static const reg_t INVALID_EP           = 0xFFFF;
    static const reg_t NO_REPLIES           = INVALID_EP;
    static const reg_t UNLIM_CREDITS        = 0x3F;

private:
    static const size_t EXT_REGS            = 2;
    static const size_t PRIV_REGS           = 5;
    static const size_t UNPRIV_REGS         = 5;
    static const size_t EP_REGS             = 3;

    // actual max is 64k - 1; use less for better alignment
    static const size_t MAX_PKT_SIZE        = 60 * 1024;

    enum class ExtRegs {
        FEATURES            = 0,
        EXT_CMD             = 1,
    };

    enum class PrivRegs {
        CORE_REQ            = 0,
        PRIV_CMD            = 1,
        PRIV_CMD_ARG        = 2,
        CUR_VPE             = 3,
        CLEAR_IRQ           = 4,
    };

    enum class UnprivRegs {
        COMMAND             = EXT_REGS + 0,
        DATA                = EXT_REGS + 1,
        ARG1                = EXT_REGS + 2,
        CUR_TIME            = EXT_REGS + 3,
        PRINT               = EXT_REGS + 4,
    };

    enum StatusFlags : reg_t {
        PRIV                = 1 << 0,
    };

    enum class EpType {
        INVALID,
        SEND,
        RECEIVE,
        MEMORY
    };

    enum class CmdOpCode {
        IDLE                = 0,
        SEND                = 1,
        REPLY               = 2,
        READ                = 3,
        WRITE               = 4,
        FETCH_MSG           = 5,
        ACK_MSG             = 6,
        SLEEP               = 7,
    };

    enum class PrivCmdOpCode {
        IDLE                = 0,
        INV_PAGE            = 1,
        INV_TLB             = 2,
        INS_TLB             = 3,
        XCHG_VPE            = 4,
        SET_TIMER           = 5,
        ABORT_CMD           = 6,
        FLUSH_CACHE         = 7,
    };

    enum class ExtCmdOpCode {
        IDLE                = 0,
        INV_EP              = 1,
        RESET               = 2,
    };

    enum class IRQ {
        CORE_REQ            = 0,
        TIMER               = 1,
    };

public:
    enum MemFlags : reg_t {
        R                   = 1 << 0,
        W                   = 1 << 1,
    };

    struct Header {
        enum {
            FL_REPLY            = 1 << 0,
            FL_PAGEFAULT        = 1 << 1,
        };

        uint8_t flags : 2,
                replySize : 6;
        uint8_t senderPe;
        uint16_t senderEp;
        uint16_t replyEp;   // for a normal message this is the reply epId
                            // for a reply this is the enpoint that receives credits
        uint16_t length;

        uint32_t replylabel;
        uint32_t label;
    } PACKED;

    struct Message : Header {
        epid_t send_ep() const {
            return senderEp;
        }
        epid_t reply_ep() const {
            return replyEp;
        }

        unsigned char data[];
    } PACKED;

    static const epid_t KPEX_SEP            = 0;
    static const epid_t KPEX_REP            = 1;
    static const epid_t PEXUP_REP           = 2;
    static const epid_t PEXUP_RPLEP         = 3;

    static const epid_t SYSC_SEP_OFF        = 0;
    static const epid_t SYSC_REP_OFF        = 1;
    static const epid_t UPCALL_REP_OFF      = 2;
    static const epid_t UPCALL_RPLEP_OFF    = 3;
    static const epid_t DEF_REP_OFF         = 4;
    static const epid_t PG_SEP_OFF          = 5;
    static const epid_t PG_REP_OFF          = 6;

    static const epid_t FIRST_USER_EP       = 4;
    static const epid_t STD_EPS_COUNT       = 7;

    static TCU &get() {
        return inst;
    }

    bool has_missing_credits(epid_t ep) const {
        reg_t r0 = read_reg(ep, 0);
        uint16_t cur = (r0 >> 19) & 0x3F;
        uint16_t max = (r0 >> 25) & 0x3F;
        return cur < max;
    }

    bool has_credits(epid_t ep) const {
        reg_t r0 = read_reg(ep, 0);
        uint16_t cur = (r0 >> 19) & 0x3F;
        return cur > 0;
    }

    bool is_valid(epid_t ep) const {
        reg_t r0 = read_reg(ep, 0);
        return static_cast<EpType>(r0 & 0x7) != EpType::INVALID;
    }

    uint64_t nanotime() const {
        return read_reg(UnprivRegs::CUR_TIME);
    }

    void print(const char *str, size_t len);

private:
    Errors::Code send(epid_t ep, const void *msg, size_t size, label_t replylbl, epid_t reply_ep);
    Errors::Code reply(epid_t ep, const void *reply, size_t size, size_t msg_off);
    Errors::Code read(epid_t ep, void *msg, size_t size, goff_t off);
    Errors::Code write(epid_t ep, const void *msg, size_t size, goff_t off);

    size_t fetch_msg(epid_t ep) const {
        write_reg(UnprivRegs::COMMAND, build_command(ep, CmdOpCode::FETCH_MSG));
        get_error();
        return read_reg(UnprivRegs::ARG1);
    }

    Errors::Code ack_msg(epid_t ep, size_t msg_off) {
        // ensure that we are really done with the message before acking it
        CPU::memory_barrier();
        write_reg(UnprivRegs::COMMAND, build_command(ep, CmdOpCode::ACK_MSG, msg_off));
        return get_error();
    }

    void sleep() {
        wait_for_msg(INVALID_EP);
    }
    void wait_for_msg(epid_t ep) {
        write_reg(UnprivRegs::COMMAND, build_command(0, CmdOpCode::SLEEP, ep));
        get_error();
    }

    void drop_msgs(size_t buf_addr, epid_t ep, label_t label) {
        // we assume that the one that used the label can no longer send messages. thus, if there
        // are no messages yet, we are done.
        word_t unread = read_reg(ep, 2) >> 32;
        if(unread == 0)
            return;

        reg_t r0 = read_reg(ep, 0);
        size_t bufsize = static_cast<size_t>(1) << ((r0 >> 35) & 0x3F);
        size_t msgsize = (r0 >> 41) & 0x3F;
        for(size_t i = 0; i < bufsize; ++i) {
            if(unread & (static_cast<size_t>(1) << i)) {
                const m3::TCU::Message *msg = offset_to_msg(buf_addr, i << msgsize);
                if(msg->label == label)
                    ack_msg(ep, i << msgsize);
            }
        }
    }

    static size_t msg_to_offset(size_t base, const Message *msg) {
        return reinterpret_cast<uintptr_t>(msg) - base;
    }
    static const Message *offset_to_msg(size_t base, size_t msg_off) {
        return reinterpret_cast<const Message*>(base + msg_off);
    }

    reg_t get_core_req() const {
        return read_reg(PrivRegs::CORE_REQ);
    }
    void set_core_req(reg_t val) {
        write_reg(PrivRegs::CORE_REQ, val);
    }

    void clear_irq(IRQ irq) {
        write_reg(PrivRegs::CLEAR_IRQ, static_cast<reg_t>(irq));
    }

    static Errors::Code get_error() {
        while(true) {
            reg_t cmd = read_reg(UnprivRegs::COMMAND);
            if(static_cast<CmdOpCode>(cmd & 0xF) == CmdOpCode::IDLE)
                return static_cast<Errors::Code>((cmd >> 20) & 0x1F);
        }
        UNREACHED;
    }

    static reg_t read_reg(ExtRegs reg) {
        return read_reg(static_cast<size_t>(reg));
    }
    static reg_t read_reg(PrivRegs reg) {
        return read_reg(((PAGE_SIZE * 2) / sizeof(reg_t)) + static_cast<size_t>(reg));
    }
    static reg_t read_reg(UnprivRegs reg) {
        return read_reg(static_cast<size_t>(reg));
    }
    static reg_t read_reg(epid_t ep, size_t idx) {
        return read_reg(EXT_REGS + UNPRIV_REGS + EP_REGS * ep + idx);
    }
    static reg_t read_reg(size_t idx) {
        return CPU::read8b(MMIO_ADDR + idx * sizeof(reg_t));
    }

    static void write_reg(ExtRegs reg, reg_t value) {
        write_reg(static_cast<size_t>(reg), value);
    }
    static void write_reg(PrivRegs reg, reg_t value) {
        write_reg(((PAGE_SIZE * 2) / sizeof(reg_t)) + static_cast<size_t>(reg), value);
    }
    static void write_reg(UnprivRegs reg, reg_t value) {
        write_reg(static_cast<size_t>(reg), value);
    }
    static void write_reg(size_t idx, reg_t value) {
        CPU::write8b(MMIO_ADDR + idx * sizeof(reg_t), value);
    }

    static uintptr_t ext_reg_addr(ExtRegs reg) {
        return MMIO_ADDR + static_cast<size_t>(reg) * sizeof(reg_t);
    }
    static uintptr_t priv_reg_addr(PrivRegs reg) {
        return MMIO_ADDR + (PAGE_SIZE * 2) + static_cast<size_t>(reg) * sizeof(reg_t);
    }
    static uintptr_t unpriv_reg_addr(UnprivRegs reg) {
        return MMIO_ADDR + static_cast<size_t>(reg) * sizeof(reg_t);
    }
    static uintptr_t ep_regs_addr(epid_t ep) {
        return MMIO_ADDR + (EXT_REGS + UNPRIV_REGS + ep * EP_REGS) * sizeof(reg_t);
    }
    static uintptr_t buffer_addr() {
        size_t regCount = EXT_REGS + UNPRIV_REGS + EP_COUNT * EP_REGS;
        return MMIO_ADDR + regCount * sizeof(reg_t);
    }

    static reg_t build_command(epid_t ep, CmdOpCode c, reg_t arg = 0) {
        return static_cast<reg_t>(c) | (static_cast<reg_t>(ep) << 4) | (arg << 25);
    }

    static TCU inst;
};

}
