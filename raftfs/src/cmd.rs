use serde::{Deserialize, Serialize};

use crate::model::{Extent, Ino, SetTime};

#[derive(Clone, Debug, Serialize, Deserialize)]
pub enum Cmd {
	MkDir {
		parent: Ino,
		name: Vec<u8>,
		perm: u16,
	},
	Create {
		parent: Ino,
		name: Vec<u8>,
		perm: u16,
	},
	Symlink {
		parent: Ino,
		name: Vec<u8>,
		target: Vec<u8>,
	},
	Unlink {
		parent: Ino,
		name: Vec<u8>,
	},
	Rmdir {
		parent: Ino,
		name: Vec<u8>,
	},
	Rename {
		sp: Ino,
		sn: Vec<u8>,
		dp: Ino,
		dn: Vec<u8>,
	},
	Link {
		ino: Ino,
		newparent: Ino,
		newname: Vec<u8>,
	},
	SetAttr {
		ino: Ino,
		mode: Option<u32>,
		uid: Option<u32>,
		gid: Option<u32>,
		size: Option<u64>,
		atime: Option<SetTime>,
		mtime: Option<SetTime>,
	},
	WriteInline {
		ino: Ino,
		offset: u64,
		data: Vec<u8>,
	},
	WriteMeta {
		ino: Ino,
		extents: Vec<Extent>,
		size: u64,
	},
}

impl Cmd {
	pub fn name(&self) -> &'static str {
		match self {
			Cmd::MkDir { .. } => "MkDir",
			Cmd::Create { .. } => "Create",
			Cmd::Symlink { .. } => "Symlink",
			Cmd::Unlink { .. } => "Unlink",
			Cmd::Rmdir { .. } => "Rmdir",
			Cmd::Rename { .. } => "Rename",
			Cmd::Link { .. } => "Link",
			Cmd::SetAttr { .. } => "SetAttr",
			Cmd::WriteInline { .. } => "WriteInline",
			Cmd::WriteMeta { .. } => "WriteMeta",
		}
	}
}
