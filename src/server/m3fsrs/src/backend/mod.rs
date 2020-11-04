use crate::internal::*;
use crate::meta_buffer::MetaBufferHead;
use crate::sess::request::Request;

use m3::cap::Selector;
use m3::cell::RefCell;
use m3::com::MemGate;
use m3::com::Perm;
use m3::errors::Error;
use m3::rc::Rc;
use thread::Event;

///In-Memory backend implementation for the file system
pub mod mem_backend;

///On-Disk backend implementation for the file system
pub mod disk_backend;

pub trait Backend {
    ///Needed for the hotfix. Might be removed.
    fn in_memory(&self) -> bool;
    fn load_meta(
        &self,
        dst: Rc<RefCell<MetaBufferHead>>,
        dst_off: usize,
        bno: BlockNo,
        unlock: Event,
    ) -> Result<(), Error>;
    fn load_data(
        &self,
        mem: &MemGate,
        bno: BlockNo,
        blocks: usize,
        init: bool,
        unlock: Event,
    ) -> Result<(), Error>;

    fn store_meta(
        &self,
        src: Rc<RefCell<MetaBufferHead>>,
        src_off: usize,
        bno: BlockNo,
        unlock: Event,
    ) -> Result<(), Error>;
    fn store_data(&self, bno: BlockNo, blocks: usize, unlock: Event) -> Result<(), Error>;

    fn sync_meta(&self, request: &mut Request, bno: &BlockNo) -> Result<(), Error>;

    fn get_filedata(
        &self,
        req: &Request,
        ext: &mut LoadedExtent,
        extoff: usize,
        perms: Perm,
        sel: Selector,
        dirty: bool,
        load: bool,
        accessed: usize,
    ) -> Result<usize, Error>;

    fn clear_extent(
        &self,
        request: &mut Request,
        extent: &LoadedExtent,
        accessed: usize,
    ) -> Result<(), Error>;

    ///Loads a new superblock
    fn load_sb(&mut self) -> Result<SuperBlock, Error>;

    fn store_sb(&self, super_block: &SuperBlock) -> Result<(), Error>;
}
