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

#include <base/log/Services.h>
#include <base/stream/IStringStream.h>
#include <base/CmdArgs.h>

#include <m3/com/MemGate.h>
#include <m3/server/Server.h>
#include <m3/server/RequestHandler.h>
#include <m3/session/Pipes.h>

#include "Session.h"

using namespace m3;

class PipeServiceHandler;
using base_class = RequestHandler<
    PipeServiceHandler, GenericFile::Operation, GenericFile::Operation::COUNT, PipeSession
>;

static Server<PipeServiceHandler> *srv;

class PipeServiceHandler : public base_class {
public:
    static constexpr size_t MSG_SIZE = 64;

    explicit PipeServiceHandler()
        : base_class(),
          _meta_sessions(),
          _rgate(RecvGate::create(nextlog2<32 * MSG_SIZE>::val, nextlog2<MSG_SIZE>::val)) {
        add_operation(GenericFile::SEEK, &PipeServiceHandler::invalid_op);
        add_operation(GenericFile::STAT, &PipeServiceHandler::invalid_op);
        add_operation(GenericFile::NEXT_IN, &PipeServiceHandler::next_in);
        add_operation(GenericFile::NEXT_OUT, &PipeServiceHandler::next_out);
        add_operation(GenericFile::COMMIT, &PipeServiceHandler::commit);
        add_operation(GenericFile::CLOSE, &PipeServiceHandler::close_chan);

        using std::placeholders::_1;
        _rgate.start(std::bind(&PipeServiceHandler::handle_message, this, _1));
    }

    virtual Errors::Code open(PipeSession **sess, capsel_t srv_sel, word_t) override {
        auto meta = new PipeMeta(srv_sel);
        _meta_sessions.append(meta);
        *sess = meta;
        return Errors::NONE;
    }

    virtual Errors::Code obtain(PipeSession *sess, KIF::Service::ExchangeData &data) override {
        if(data.caps != 2)
            return Errors::INV_ARGS;

        if(sess->type() == PipeSession::META) {
            if(data.args.count != 1)
                return Errors::INV_ARGS;
            auto npipe = static_cast<PipeMeta*>(sess)->create(srv->sel(), _rgate, data.args.vals[0]);
            data.caps = KIF::CapRngDesc(KIF::CapRngDesc::OBJ, npipe->sel(), 1).value();
        }
        else if(sess->type() == PipeSession::DATA) {
            if(data.args.count != 1)
                return Errors::INV_ARGS;
            auto nchan = static_cast<PipeData*>(sess)->attach(srv->sel(), data.args.vals[0]);
            data.caps = nchan->crd().value();
        }
        else {
            auto nchan = static_cast<PipeChannel*>(sess)->clone(srv->sel());
            data.caps = nchan->crd().value();
        }
        return Errors::NONE;
    }

    virtual Errors::Code delegate(PipeSession *sess, KIF::Service::ExchangeData &data) override {
        if(sess->type() == PipeSession::DATA) {
            if(data.caps != 1 || data.args.count != 0 || static_cast<PipeData*>(sess)->memory)
                return Errors::INV_ARGS;

            capsel_t sel = VPE::self().alloc_sel();
            static_cast<PipeData*>(sess)->memory = new MemGate(MemGate::bind(sel));
            data.caps = KIF::CapRngDesc(KIF::CapRngDesc::OBJ, sel, data.caps).value();
        }
        else if(sess->type() != PipeSession::META) {
            if(data.caps != 1 || data.args.count != 0)
                return Errors::INV_ARGS;

            capsel_t sel = VPE::self().alloc_sel();
            static_cast<PipeChannel*>(sess)->set_ep(sel);
            data.caps = KIF::CapRngDesc(KIF::CapRngDesc::OBJ, sel, data.caps).value();
        }
        else
            return Errors::INV_ARGS;
        return Errors::NONE;
    }

    virtual Errors::Code close(PipeSession *sess) override {
        if(sess->type() == PipeSession::META)
            _meta_sessions.remove(static_cast<PipeMeta*>(sess));
        sess->close();
        delete sess;
        _rgate.drop_msgs_with(reinterpret_cast<label_t>(sess));
        return Errors::NONE;
    }

    virtual void shutdown() override {
        // delete meta sessions, which will delete the child sessions as well
        for(auto it = _meta_sessions.begin(); it != _meta_sessions.end(); ) {
            auto old = it++;
            delete &*old;
        }

        _rgate.stop();
    }

    void invalid_op(GateIStream &is) {
        reply_vmsg(is, m3::Errors::NOT_SUP);
    }

    void next_in(m3::GateIStream &is) {
        PipeSession *sess = is.label<PipeSession*>();
        sess->read(is, 0);
    }

    void next_out(m3::GateIStream &is) {
        PipeSession *sess = is.label<PipeSession*>();
        sess->write(is, 0);
    }

    void commit(m3::GateIStream &is) {
        PipeSession *sess = is.label<PipeSession*>();
        sess->commit(is);
    }

    void close_chan(m3::GateIStream &is) {
        PipeSession *sess = is.label<PipeSession*>();
        // reply first to prevent that we drop this message
        reply_error(is, Errors::NONE);
        close(sess);
    }

private:
    SList<PipeMeta> _meta_sessions;
    RecvGate _rgate;
};

static void usage(const char *name) {
    Serial::get() << "Usage: " << name << " [-s <sel>]\n";
    Serial::get() << "  -s: don't create service, use selectors <sel>..<sel+1>\n";
    exit(1);
}

int main(int argc, char **argv) {
    capsel_t sels = ObjCap::INVALID;
    epid_t ep = EP_COUNT;

    int opt;
    while((opt = CmdArgs::get(argc, argv, "s:")) != -1) {
        switch(opt) {
            case 's': {
                String input(CmdArgs::arg);
                IStringStream is(input);
                is >> sels >> ep;
                break;
            }
            default:
                usage(argv[0]);
        }
    }

    if(sels != ObjCap::INVALID)
        srv = new Server<PipeServiceHandler>(sels, ep, new PipeServiceHandler());
    else
        srv = new Server<PipeServiceHandler>("pipes", new PipeServiceHandler());

    env()->workloop()->multithreaded(16);
    env()->workloop()->run();
    delete srv;
    return 0;
}
