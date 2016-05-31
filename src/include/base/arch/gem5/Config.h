/*
 * Copyright (C) 2015, Nils Asmussen <nils@os.inf.tu-dresden.de>
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

#define MEMORY_CORE         8
#define KERNEL_CORE         0
#define APP_CORES           1
#define MAX_CORES           8
#define AVAIL_PES           (MAX_CORES - 1)
#define PE_MASK             0xFFFFFFFF
#define CAP_TOTAL           512
#define FS_IMG_OFFSET       0x0

#define PAGE_BITS           12
#define PAGE_SIZE           (static_cast<size_t>(1) << PAGE_BITS)
#define PAGE_MASK           (PAGE_SIZE - 1)

// leave the first 256 MiB for the filesystem and PEs
#define DRAM_OFFSET         (256 * 1024 * 1024)
#define DRAM_SIZE           (512 * 1024 * 1024 - DRAM_OFFSET)

#define INIT_HEAP_SIZE      (64 * 1024)
#define HEAP_SIZE           0x10000
#define EP_COUNT            7

#define RT_START            0x1000
#define RT_SIZE             0x2000
#define RT_END              (RT_START + RT_SIZE)

#define STACK_SIZE          0x1000
#define STACK_TOP           (RT_END + STACK_SIZE)
#define STACK_BOTTOM        RT_END

#define RECVBUF_SPACE       0xE0000000
#define RECVBUF_SIZE        (4 * PAGE_SIZE)

#define DEF_RCVBUF_ORDER    8
#define DEF_RCVBUF_SIZE     (1 << DEF_RCVBUF_ORDER)
#define DEF_RCVBUF          RECVBUF_SPACE

#define MEMCAP_END          RECVBUF_SPACE
