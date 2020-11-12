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
#include <m3/session/M3FS.h>
#include <m3/vfs/GenericFile.h>
#include <m3/vfs/VFS.h>

namespace m3 {

Reference<File> M3FS::open(const char *path, int perms) {
    KIF::ExchangeArgs args;
    ExchangeOStream os(args);
    os << perms << String(path);
    args.bytes = os.total();
    KIF::CapRngDesc crd = obtain(2, &args);
    return Reference<File>(new GenericFile(perms, crd.start()));
}

void M3FS::stat(const char *path, FileInfo &info) {
    GateIStream reply = send_receive_vmsg(_gate, STAT, path);
    reply.pull_result();
    reply >> info;
}

Errors::Code M3FS::try_stat(const char *path, FileInfo &info) noexcept {
    GateIStream reply = send_receive_vmsg(_gate, STAT, path);
    Errors::Code res;
    reply >> res;
    if(res != Errors::NONE)
        return res;
    reply >> info;
    return Errors::NONE;
}

void M3FS::mkdir(const char *path, mode_t mode) {
    GateIStream reply = send_receive_vmsg(_gate, MKDIR, path, mode);
    reply.pull_result();
}

void M3FS::rmdir(const char *path) {
    GateIStream reply = send_receive_vmsg(_gate, RMDIR, path);
    reply.pull_result();
}

void M3FS::link(const char *oldpath, const char *newpath) {
    GateIStream reply = send_receive_vmsg(_gate, LINK, oldpath, newpath);
    reply.pull_result();
}

void M3FS::unlink(const char *path) {
    GateIStream reply = send_receive_vmsg(_gate, UNLINK, path);
    reply.pull_result();
}

void M3FS::rename(const char *oldpath, const char *newpath) {
    GateIStream reply = send_receive_vmsg(_gate, RENAME, oldpath, newpath);
    reply.pull_result();
}

void M3FS::delegate(VPE &vpe) {
    vpe.delegate_obj(sel());
    // TODO what if it fails?
    obtain_for(vpe, KIF::CapRngDesc(KIF::CapRngDesc::OBJ, sel() + 1, 1));
}

void M3FS::serialize(Marshaller &m) {
    m << sel();
}

FileSystem *M3FS::unserialize(Unmarshaller &um) {
    capsel_t sel;
    um >> sel;
    return new M3FS(sel);
}

}
