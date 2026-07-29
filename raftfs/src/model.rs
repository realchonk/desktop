use serde::{Deserialize, Serialize};

use std::collections::BTreeMap;
use std::time::SystemTime;

pub type Ino = u64;
pub type Hash = [u8; 32];

pub const BLOCK_SIZE: u64 = 4096;
pub const INLINE_THRESHOLD: usize = 4096;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Kind {
	Regular,
	Directory,
	Symlink,
}

#[allow(dead_code)]
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Extent {
	pub off: u64,
	pub len: u64,
	pub block: Hash,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum FileData {
	Inline(Vec<u8>),
	Extents(BTreeMap<u64, Extent>),
}

impl FileData {
	pub fn empty() -> Self {
		FileData::Inline(Vec::new())
	}
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Inode {
	pub ino: Ino,
	pub parent: Ino,
	pub kind: Kind,
	pub perm: u16,
	pub uid: u32,
	pub gid: u32,
	pub size: u64,
	pub nlink: u32,
	pub atime: SystemTime,
	pub mtime: SystemTime,
	pub ctime: SystemTime,
	pub crtime: SystemTime,
	pub children: BTreeMap<Vec<u8>, Ino>,
	pub data: FileData,
	pub target: Vec<u8>,
}

#[derive(Clone, Debug)]
pub struct Attr {
	pub ino: Ino,
	pub kind: Kind,
	pub perm: u16,
	pub nlink: u32,
	pub uid: u32,
	pub gid: u32,
	pub size: u64,
	pub blocks: u64,
	pub atime: SystemTime,
	pub mtime: SystemTime,
	pub ctime: SystemTime,
	pub crtime: SystemTime,
}

pub fn inode_attr(i: &Inode) -> Attr {
	let blocks = (i.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	Attr {
		ino: i.ino,
		kind: i.kind,
		perm: i.perm,
		nlink: i.nlink,
		uid: i.uid,
		gid: i.gid,
		size: i.size,
		blocks,
		atime: i.atime,
		mtime: i.mtime,
		ctime: i.ctime,
		crtime: i.crtime,
	}
}

#[allow(dead_code)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Error {
	NoEntry,
	NotDir,
	IsDir,
	Exist,
	NotEmpty,
	Invalid,
	NoSupp,
	Acces,
	Perm,
	Io,
	Range,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Ctx {
	pub now: SystemTime,
	pub uid: u32,
	pub gid: u32,
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
pub enum SetTime {
	Now,
	Specific(SystemTime),
}

pub fn resolve_time(t: SetTime, now: SystemTime) -> SystemTime {
	match t {
		SetTime::Now => now,
		SetTime::Specific(s) => s,
	}
}
