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

#include <base/Common.h>
#include <base/col/SList.h>
#include <base/util/Profile.h>
#include <base/KIF.h>
#include <base/Panic.h>

#include <m3/stream/Standard.h>
#include <m3/Syscalls.h>

#include "../cppbenchs.h"

using namespace m3;

alignas(64) static char buf[8192];
static capsel_t selector = ObjCap::INVALID;

NOINLINE static void noop() {
    Profile pr;
    cout << pr.run_with_id([] {
        Syscalls::noop();
    }, 0x50) << "\n";
}

NOINLINE static void activate() {
    MemGate mgate = MemGate::create_global(0x1000, MemGate::RW);
    mgate.read(buf, 8, 0);

    Profile pr;
    cout << pr.run_with_id([&mgate] {
        Syscalls::activate(VPE::self().ep_to_sel(mgate.ep()), mgate.sel(), 0);
    }, 0x51) << "\n";
}

NOINLINE static void create_rgate() {
    struct SyscallRGateRunner : public Runner {
        void run() override {
            Syscalls::create_rgate(selector, 10, 10);
        }
        void post() override {
            Syscalls::revoke(0, KIF::CapRngDesc(KIF::CapRngDesc::OBJ, selector, 1), true);
        }
    };

    Profile pr;
    SyscallRGateRunner runner;
    cout << pr.runner_with_id(runner, 0x52) << "\n";
}

NOINLINE static void create_sgate() {
    struct SyscallSGateRunner : public Runner {
        explicit SyscallSGateRunner() : rgate(RecvGate::create(10, 10)) {
        }
        void run() override {
            Syscalls::create_sgate(selector, rgate.sel(), 0x1234, 1024);
        }
        void post() override {
            Syscalls::revoke(0, KIF::CapRngDesc(KIF::CapRngDesc::OBJ, selector, 1), true);
        }

        RecvGate rgate;
    };

    Profile pr;
    SyscallSGateRunner runner;
    cout << pr.runner_with_id(runner, 0x53) << "\n";
}

NOINLINE static void create_map() {
    if(!VPE::self().pe().has_virtmem()) {
        cout << "PE has no virtual memory support; skipping\n";
        return;
    }

    constexpr capsel_t DEST = 0x30000000 >> PAGE_BITS;

    struct SyscallMapRunner : public Runner {
        explicit SyscallMapRunner() : mgate(MemGate::create_global(0x1000, MemGate::RW)) {
        }

        void run() override {
            Syscalls::create_map(DEST, 0, mgate.sel(), 0, 1, MemGate::RW);
        }
        void post() override {
            Syscalls::revoke(0, KIF::CapRngDesc(KIF::CapRngDesc::MAP, DEST, 1), true);
        }

        MemGate mgate;
    };

    Profile pr;
    SyscallMapRunner runner;
    cout << pr.runner_with_id(runner, 0x55) << "\n";
}

NOINLINE static void create_srv() {
    struct SyscallSrvRunner : public Runner {
        explicit SyscallSrvRunner() : rgate(RecvGate::create(10, 10)) {
            rgate.activate();
        }

        void run() override {
            Syscalls::create_srv(selector, VPE::self().sel(), rgate.sel(), "test");
        }
        void post() override {
            Syscalls::revoke(0, KIF::CapRngDesc(KIF::CapRngDesc::OBJ, selector, 1), true);
        }

        RecvGate rgate;
    };

    Profile pr;
    SyscallSrvRunner runner;
    cout << pr.runner_with_id(runner, 0x56) << "\n";
}

NOINLINE static void derive_mem() {
    struct SyscallDeriveRunner : public Runner {
        explicit SyscallDeriveRunner() : mgate(MemGate::create_global(0x1000, MemGate::RW)) {
        }

        void run() override {
            Syscalls::derive_mem(VPE::self().sel(), selector, mgate.sel(), 0, 0x1000, MemGate::RW);
        }
        void post() override {
            Syscalls::revoke(0, KIF::CapRngDesc(KIF::CapRngDesc::OBJ, selector, 1), true);
        }

        MemGate mgate;
    };

    Profile pr;
    SyscallDeriveRunner runner;
    cout << pr.runner_with_id(runner, 0x58) << "\n";
}

NOINLINE static void exchange() {
    struct SyscallExchangeRunner : public Runner {
        explicit SyscallExchangeRunner() : vpe("test") {
        }

        void run() override {
            Syscalls::exchange(vpe.sel(),
                KIF::CapRngDesc(KIF::CapRngDesc::OBJ, 1, 1), selector, false);
        }
        void post() override {
            Syscalls::revoke(vpe.sel(), KIF::CapRngDesc(KIF::CapRngDesc::OBJ, selector, 1), true);
        }

        VPE vpe;
    };

    Profile pr;
    SyscallExchangeRunner runner;
    cout << pr.runner_with_id(runner, 0x59) << "\n";
}

NOINLINE static void revoke() {
    struct SyscallRevokeRunner : public Runner {
        void pre() override {
            mgate = new MemGate(MemGate::create_global(0x1000, MemGate::RW));
        }
        void run() override {
            delete mgate;
            mgate = nullptr;
        }

        MemGate *mgate;
    };

    Profile pr;
    SyscallRevokeRunner runner;
    cout << pr.runner_with_id(runner, 0x5A) << "\n";
}

void bsyscall() {
    selector = VPE::self().alloc_sel();

    RUN_BENCH(noop);
    RUN_BENCH(activate);
    RUN_BENCH(create_rgate);
    RUN_BENCH(create_sgate);
    RUN_BENCH(create_map);
    RUN_BENCH(create_srv);
    RUN_BENCH(derive_mem);
    RUN_BENCH(exchange);
    RUN_BENCH(revoke);
}
