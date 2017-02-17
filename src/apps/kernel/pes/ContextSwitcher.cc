/*
 * Copyright (C) 2015, René Küttner <rene.kuettner@tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of M3 (Microkernel for Minimalist Manycores).
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

#include <base/RCTMux.h>
#include <base/log/Kernel.h>
#include <base/util/Profile.h>

#include "pes/ContextSwitcher.h"
#include "pes/Timeouts.h"
#include "pes/PEManager.h"
#include "pes/VPEManager.h"
#include "pes/VPE.h"
#include "DTU.h"
#include "Platform.h"

namespace kernel {

static const char *stateNames[] = {
    "S_IDLE",
    "S_STORE_WAIT",
    "S_STORE_DONE",
    "S_SWITCH",
    "S_RESTORE_WAIT",
    "S_RESTORE_DONE",
};

/**
 * The state machine for context switching looks as follows:
 *
 *          switch & cur     +----------+
 *         /-----------------|  S_IDLE  |<--------------\
 *         |                 +----------+               |
 *         v                     |   |                  |
 * +------------------+          |   |         +-----------------+
 * |   S_STORE_WAIT   |   switch |   |         |  S_RESTORE_DONE |
 * |   ------------   |     &    |   |         |  -------------- |
 * |   e/ inject IRQ  |    !cur  |   |         |    e/ notify    |
 * +------------------+          |   | start   +-----------------+
 *         |                     |   |                  ^
 *         | signal              |   |                  | signal
 *         |                     |   |                  |
 *         v                     |   |                  |
 * +------------------+          |   |         +-----------------+
 * |   S_STORE_DONE   |          |   |         |  S_RESTORE_WAIT |
 * |   ------------   |          |   \-------->|  -------------- |
 * | e/ save DTU regs |          |             |    e/ wakeup    |
 * +------------------+          |             +-----------------+
 *         |                     v                      ^
 *         |             +------------------+           |
 *         |             |     S_SWITCH     |           |
 *         \------------>|     --------     |-----------/
 *                       | e/ sched & reset |
 *                       +------------------+
 */

size_t ContextSwitcher::_global_ready = 0;

ContextSwitcher::ContextSwitcher(peid_t pe)
    : _muxable(Platform::pe(pe).supports_multictx()), _pe(pe), _state(S_IDLE), _count(), _ready(),
      _timeout(), _wait_time(), _idle(), _cur(), _set_yield() {
    assert(pe > 0);
}

VPE* ContextSwitcher::schedule() {
    if (_ready.length() > 0) {
        VPE *vpe = _ready.remove_first();
        _global_ready--;
        assert(vpe->_flags & VPE::F_READY);
        vpe->_flags ^= VPE::F_READY;
        return vpe;
    }

    return _idle;
}

void ContextSwitcher::init() {
    assert(_idle == nullptr);

#if !defined(__host__)
    _idle = new VPE(m3::String("rctmux"), _pe, VPEManager::get().get_id(),
        VPE::F_IDLE | VPE::F_INIT, VPE::INVALID_EP, m3::KIF::INV_SEL);

    KLOG(CTXSW, "CTXSW[" << _pe << "] initialized (idle="
        << _idle->id() << ", muxable=" << _muxable << ")");
#endif

    if(_idle && _cur == nullptr)
        start_switch();
}

void ContextSwitcher::enqueue(VPE *vpe) {
    // never make IDLE VPEs ready
    if(vpe->_flags & (VPE::F_READY | VPE::F_IDLE))
        return;

    vpe->_flags |= VPE::F_READY;
    _ready.append(vpe);
    _global_ready++;
}

void ContextSwitcher::dequeue(VPE *vpe) {
    if(!(vpe->_flags & VPE::F_READY))
        return;

    vpe->_flags ^= VPE::F_READY;
    _ready.remove(vpe);
    _global_ready--;
}

void ContextSwitcher::add_vpe(VPE *vpe) {
    if(!(vpe->_flags & VPE::F_MUXABLE))
        _muxable = false;

    _count++;
}

void ContextSwitcher::remove_vpe(VPE *vpe) {
    stop_vpe(vpe);

    if(--_count == 0)
        _muxable = Platform::pe(_pe).supports_multictx();

    if(_count == 1) {
        // cancel timeout; the remaining VPE can run as long as it likes
        if(_timeout) {
            Timeouts::get().cancel(_timeout);
            _timeout = nullptr;
        }
    }
}

VPE *ContextSwitcher::steal_vpe() {
    if(can_mux() && _ready.length() > 0) {
        VPE *vpe = _ready.remove_first();
        vpe->_flags ^= VPE::F_READY;
        _global_ready--;
        return vpe;
    }

    return nullptr;
}

void ContextSwitcher::start_vpe(VPE *vpe) {
    if(_cur != vpe) {
        enqueue(vpe);
        start_switch();
        return;
    }

    if(_state != S_IDLE)
        return;

    m3::Profile::start(0xcccc);
    _state = S_RESTORE_WAIT;
    next_state(0);
}

void ContextSwitcher::stop_vpe(VPE *vpe) {
    dequeue(vpe);

    if(_cur == vpe && _state == S_IDLE) {
        // the VPE id is expected to be invalid in S_SWITCH
        DTU::get().unset_vpeid(_cur->desc());
        vpe->_state = VPE::SUSPENDED;
        _cur = nullptr;
        start_switch();
    }
}

bool ContextSwitcher::yield_vpe(VPE *vpe) {
    if(_cur != vpe)
        return false;
    if(_ready.length() == 0)
        return false;

    start_switch();
    return true;
}

bool ContextSwitcher::unblock_vpe(VPE *vpe, bool force) {
    enqueue(vpe);

    // if we are forced or are executing nothing useful atm, start a switch immediately
    if(force || !_cur || (_cur->_flags & VPE::F_IDLE) || _cur->is_waiting())
        return start_switch();

    if(!_timeout) {
        uint64_t now = DTU::get().get_time();
        uint64_t exectime = now - _cur->_lastsched;
        // if there is some time left in the timeslice, program a timeout
        if(exectime < VPE::TIME_SLICE) {
            auto &&callback = std::bind(&ContextSwitcher::start_switch, this, true);
            _timeout = Timeouts::get().wait_for(VPE::TIME_SLICE - exectime, m3::Util::move(callback));
        }
        // otherwise, switch now
        else
            return start_switch();
    }

    // wait for the timeout
    return false;
}

void ContextSwitcher::update_yield() {
    bool yield = _global_ready > 0;
    if(can_mux() && _cur && !(_cur->_flags & VPE::F_IDLE) && yield != _set_yield) {
        KLOG(CTXSW, "CtxSw[" << _pe << "]: VPE " << _cur->id() << " updating yield=" << yield);

        // update yield time and wake him up in case he was idling
        _set_yield = yield;
        uint64_t val = yield ? YIELD_TIME : 0;
        DTU::get().write_mem(_cur->desc(), RCTMUX_YIELD, &val, sizeof(val));
        if(yield)
            DTU::get().wakeup(_cur->desc());
    }
}

bool ContextSwitcher::start_switch(bool timedout) {
    if(!timedout && _timeout)
        Timeouts::get().cancel(_timeout);
    _timeout = nullptr;

    // if there is nobody to switch to, just ignore it
    if(_cur && _ready.length() == 0)
        return false;
    // if there is a switch running, do nothing
    if(_state != S_IDLE)
        return false;

    m3::Profile::start(0xcccc);

    // if no VPE is running, directly switch to a new VPE
    if (_cur == nullptr)
        _state = S_SWITCH;
    else
        _state = S_STORE_WAIT;

    bool finished = next_state(0);
    if(finished)
        m3::Profile::stop(0xcccc);
    return finished;
}

void ContextSwitcher::continue_switch() {
    assert(_state == S_STORE_DONE || _state == S_RESTORE_DONE);

    uint64_t flags = 0;
    DTU::get().read_swflags(_cur->desc(), &flags);

    if(~flags & m3::RCTMuxCtrl::SIGNAL) {
        assert(_wait_time > 0);
        if(_wait_time < MAX_WAIT_TIME)
            _wait_time *= 2;
        Timeouts::get().wait_for(_wait_time, std::bind(&ContextSwitcher::continue_switch, this));
    }
    else {
        if(next_state(flags))
            m3::Profile::stop(0xcccc);
    }
}

bool ContextSwitcher::next_state(uint64_t flags) {
retry:
    KLOG(CTXSW_STATES, "CtxSw[" << _pe << "]: next; state="
        << stateNames[static_cast<size_t>(_state)]
        << " (current=" << (_cur ? _cur->id() : 0) << ":"
                        << (_cur ? _cur->name().c_str() : "-") << ")");

    _wait_time = 0;

    VPE *migvpe = nullptr;
    switch(_state) {
        case S_IDLE:
            assert(false);
            break;

        case S_STORE_WAIT: {
            DTU::get().write_swflags(_cur->desc(), m3::RCTMuxCtrl::STORE | m3::RCTMuxCtrl::WAITING);
            DTU::get().injectIRQ(_cur->desc());

            _state = S_STORE_DONE;

            _wait_time = INIT_WAIT_TIME;
            break;
        }

        case S_STORE_DONE: {
            _cur->_dtustate.save(_cur->desc());

            uint64_t now = DTU::get().get_time();
            uint64_t cycles = _cur->_dtustate.get_idle_time();
            uint64_t total = now - _cur->_lastsched;
            bool blocked = (_cur->_flags & VPE::F_HASAPP) && _cur->_dtustate.was_idling();

            KLOG(CTXSW, "CtxSw[" << _pe << "]: saved VPE " << _cur->id() << " (idled "
                << cycles << " of " << total << " cycles)");
            KLOG(CTXSW, "CtxSw[" << _pe << "]: VPE " << _cur->id() << " is set to "
                << (blocked ? "blocked" : "ready"));

            _cur->_state = VPE::SUSPENDED;
            if(!blocked) {
                // the VPE is ready, so try to migrate it somewhere else to continue immediately
                if(!_cur->migrate())
                    enqueue(_cur);
                // if that worked, remember to switch to it. don't do that now, because we have to
                // do the reset first to ensure that the cache is flushed in case of non coherent
                // caches.
                else
                    migvpe = _cur;
            }

            // fall through
        }

        case S_SWITCH: {
            vpeid_t old = _cur ? _cur->id() : VPE::INVALID_ID;
            _cur = schedule();
            if(_cur == nullptr) {
                _state = S_IDLE;
                return true;
            }

            // make it running here, so that the PTEs are sent to the PE, if F_INIT is set
            _cur->_state = VPE::RUNNING;
            _cur->_lastsched = DTU::get().get_time();

            _cur->_dtustate.reset(RCTMUX_ENTRY);

            _cur->_dtustate.restore(VPEDesc(_pe, old), _cur->id());

            if(_cur->_flags & VPE::F_INIT)
                _cur->init_memory();

            // fall through
        }

        case S_RESTORE_WAIT: {
            // let the VPE report idle times if there are other VPEs
            uint64_t report = (can_mux() && (_set_yield = migvpe || _global_ready > 0)) ? YIELD_TIME : 0;
            uint64_t flags = m3::RCTMuxCtrl::WAITING;

            // tell rctmux whether there is an application and the PE id
            if(_cur->_flags & VPE::F_HASAPP)
                flags |= m3::RCTMuxCtrl::RESTORE | (static_cast<uint64_t>(_pe) << 32);

            KLOG(CTXSW, "CtxSw[" << _pe << "]: restoring VPE " << _cur->id() << " with report="
                << report << " flags=" << m3::fmt(flags, "#x"));

            DTU::get().write_swstate(_cur->desc(), flags, report);
            DTU::get().wakeup(_cur->desc());
            _state = S_RESTORE_DONE;

            _wait_time = INIT_WAIT_TIME;
            break;
        }

        case S_RESTORE_DONE: {
            // we have finished the init phase (if it was set)
#if !defined(__host__)
            _cur->_flags &= ~static_cast<uint>(VPE::F_INIT);
#endif
            _cur->notify_resume();

            // TODO actually, we have to disable the communication between restoring the EPs and
            // this point, i.e., the ack of rctmux. because if rctmux needs to restore something
            // before being able to continue, communication should still be blocked. we have that
            // case with the hash accelerator, that needs to restore the buffer and clients should
            // not be able to overwrite it until that is finished

            DTU::get().write_swflags(_cur->desc(), 0);
            _state = S_IDLE;

            // if we are starting a VPE, we might already have a timeout for it
            if(_ready.length() > 0 && !_timeout) {
                auto &&callback = std::bind(&ContextSwitcher::start_switch, this, true);
                _timeout = Timeouts::get().wait_for(VPE::TIME_SLICE, m3::Util::move(callback));
            }
            break;
        }
    }

    KLOG(CTXSW_STATES, "CtxSw[" << _pe << "]: done; state="
        << stateNames[static_cast<size_t>(_state)]
        << " (current=" << (_cur ? _cur->id() : 0) << ":"
                        << (_cur ? _cur->name().c_str() : "-") << ")");

    if(migvpe)
        PEManager::get().unblock_vpe(migvpe, false);

    if(_wait_time) {
        for(int i = 0; i < SIGNAL_WAIT_COUNT; ++i) {
            DTU::get().read_swflags(_cur->desc(), &flags);
            if(flags & m3::RCTMuxCtrl::SIGNAL)
                goto retry;
        }

        Timeouts::get().wait_for(_wait_time, std::bind(&ContextSwitcher::continue_switch, this));
    }

    return _state == S_IDLE;
}

} /* namespace m3 */
