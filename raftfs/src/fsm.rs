use serde::{Deserialize, Serialize};

use std::collections::BTreeMap;
use std::time::SystemTime;

use crate::cmd::Cmd;
use crate::model::{
	inode_attr, Attr, Ctx, Error, Extent, FileData, FileData::*, Inode, Ino, Kind, SetTime,
	BLOCK_SIZE,
};

pub struct Fsm {
	inodes: BTreeMap<Ino, Inode>,
	next_ino: Ino,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct FsmSnapshot {
	pub inodes: BTreeMap<Ino, Inode>,
	pub next_ino: Ino,
}

impl Fsm {
	pub fn new(uid: u32, gid: u32) -> Self {
		let t = SystemTime::now();
		let root = Inode {
			ino: 1,
			parent: 1,
			kind: Kind::Directory,
			perm: 0o755,
			uid,
			gid,
			size: 0,
			nlink: 2,
			atime: t,
			mtime: t,
			ctime: t,
			crtime: t,
			children: BTreeMap::new(),
			data: FileData::empty(),
			target: Vec::new(),
		};
		let mut inodes = BTreeMap::new();
		inodes.insert(1, root);
		Self { inodes, next_ino: 2 }
	}

	pub fn attr(&self, ino: Ino) -> Option<Attr> {
		self.inodes.get(&ino).map(inode_attr)
	}

	pub fn lookup(&self, parent: Ino, name: &[u8]) -> Option<Ino> {
		let d = self.inodes.get(&parent)?;
		if d.kind != Kind::Directory {
			return None;
		}
		d.children.get(name).copied()
	}

	pub fn read_dir(&self, ino: Ino) -> Option<(Ino, Vec<(Vec<u8>, Ino, Kind)>)> {
		let d = self.inodes.get(&ino)?;
		if d.kind != Kind::Directory {
			return None;
		}
		let parent = d.parent;
		let entries = d
			.children
			.iter()
			.map(|(n, &c)| {
				let k = self.inodes.get(&c).map(|x| x.kind).unwrap_or(Kind::Regular);
				(n.clone(), c, k)
			})
			.collect();
		Some((parent, entries))
	}

	pub fn readlink(&self, ino: Ino) -> Option<Vec<u8>> {
		let i = self.inodes.get(&ino)?;
		if i.kind != Kind::Symlink {
			return None;
		}
		Some(i.target.clone())
	}

	pub fn parent_of(&self, ino: Ino) -> Option<Ino> {
		self.inodes.get(&ino).map(|i| i.parent)
	}

	pub fn file_data(&self, ino: Ino) -> Option<(FileData, u64)> {
		let i = self.inodes.get(&ino)?;
		if i.kind != Kind::Regular {
			return None;
		}
		Some((i.data.clone(), i.size))
	}

	pub fn apply(&mut self, ctx: &Ctx, cmd: &Cmd) -> Result<(), Error> {
		match cmd {
			Cmd::MkDir { parent, name, perm } => {
				self.create_child(*parent, name, Kind::Directory, *perm, ctx)?
			}
			Cmd::Create { parent, name, perm } => {
				self.create_child(*parent, name, Kind::Regular, *perm, ctx)?
			}
			Cmd::Symlink { parent, name, target } => {
				self.create_symlink(*parent, name, target, ctx)?
			}
			Cmd::Unlink { parent, name } => self.unlink(*parent, name, ctx)?,
			Cmd::Rmdir { parent, name } => self.rmdir(*parent, name, ctx)?,
			Cmd::Rename { sp, sn, dp, dn } => self.rename(*sp, sn, *dp, dn, ctx)?,
			Cmd::Link { ino, newparent, newname } => {
				self.link(*ino, *newparent, newname, ctx)?
			}
			Cmd::SetAttr { ino, mode, uid, gid, size, atime, mtime } => self.setattr(
				*ino, *mode, *uid, *gid, *size, *atime, *mtime, ctx,
			)?,
			Cmd::WriteInline { ino, offset, data } => {
				self.write_inline(*ino, *offset, data, ctx)?
			}
			Cmd::WriteMeta { ino, extents, size } => {
				self.write_meta(*ino, extents, *size, ctx)?
			}
		}
		Ok(())
	}

	fn create_child(
		&mut self,
		parent: Ino,
		name: &[u8],
		kind: Kind,
		perm: u16,
		ctx: &Ctx,
	) -> Result<(), Error> {
		let is_dir = kind == Kind::Directory;
		{
			let p = self.inodes.get(&parent).ok_or(Error::NoEntry)?;
			if p.kind != Kind::Directory {
				return Err(Error::NotDir);
			}
			if p.children.contains_key(name) {
				return Err(Error::Exist);
			}
		}
		let ino = self.next_ino;
		self.next_ino += 1;
		let t = ctx.now;
		let inode = Inode {
			ino,
			parent,
			kind,
			perm,
			uid: ctx.uid,
			gid: ctx.gid,
			size: 0,
			nlink: if is_dir { 2 } else { 1 },
			atime: t,
			mtime: t,
			ctime: t,
			crtime: t,
			children: BTreeMap::new(),
			data: FileData::empty(),
			target: Vec::new(),
		};
		self.inodes.insert(ino, inode);
		{
			let p = self.inodes.get_mut(&parent).unwrap();
			p.children.insert(name.to_vec(), ino);
			p.mtime = t;
			p.ctime = t;
			if is_dir {
				p.nlink += 1;
			}
		}
		Ok(())
	}

	fn create_symlink(
		&mut self,
		parent: Ino,
		name: &[u8],
		target: &[u8],
		ctx: &Ctx,
	) -> Result<(), Error> {
		{
			let p = self.inodes.get(&parent).ok_or(Error::NoEntry)?;
			if p.kind != Kind::Directory {
				return Err(Error::NotDir);
			}
			if p.children.contains_key(name) {
				return Err(Error::Exist);
			}
		}
		let ino = self.next_ino;
		self.next_ino += 1;
		let t = ctx.now;
		let inode = Inode {
			ino,
			parent,
			kind: Kind::Symlink,
			perm: 0o777,
			uid: ctx.uid,
			gid: ctx.gid,
			size: target.len() as u64,
			nlink: 1,
			atime: t,
			mtime: t,
			ctime: t,
			crtime: t,
			children: BTreeMap::new(),
			data: FileData::empty(),
			target: target.to_vec(),
		};
		self.inodes.insert(ino, inode);
		{
			let p = self.inodes.get_mut(&parent).unwrap();
			p.children.insert(name.to_vec(), ino);
			p.mtime = t;
			p.ctime = t;
		}
		Ok(())
	}

	fn child_of(&self, parent: Ino, name: &[u8]) -> Result<Ino, Error> {
		let d = self.inodes.get(&parent).ok_or(Error::NoEntry)?;
		if d.kind != Kind::Directory {
			return Err(Error::NotDir);
		}
		d.children.get(name).copied().ok_or(Error::NoEntry)
	}

	fn unlink(&mut self, parent: Ino, name: &[u8], ctx: &Ctx) -> Result<(), Error> {
		let cino = self.child_of(parent, name)?;
		{
			let c = self.inodes.get(&cino).ok_or(Error::NoEntry)?;
			if c.kind == Kind::Directory {
				return Err(Error::IsDir);
			}
		}
		let drop_inode = {
			let c = self.inodes.get_mut(&cino).unwrap();
			c.nlink = c.nlink.saturating_sub(1);
			c.ctime = ctx.now;
			c.nlink == 0
		};
		{
			let p = self.inodes.get_mut(&parent).unwrap();
			p.children.remove(name);
			p.mtime = ctx.now;
			p.ctime = ctx.now;
		}
		if drop_inode {
			self.inodes.remove(&cino);
		}
		Ok(())
	}

	fn rmdir(&mut self, parent: Ino, name: &[u8], ctx: &Ctx) -> Result<(), Error> {
		let cino = self.child_of(parent, name)?;
		{
			let c = self.inodes.get(&cino).ok_or(Error::NoEntry)?;
			if c.kind != Kind::Directory {
				return Err(Error::NotDir);
			}
			if !c.children.is_empty() {
				return Err(Error::NotEmpty);
			}
		}
		{
			let p = self.inodes.get_mut(&parent).unwrap();
			p.children.remove(name);
			p.nlink = p.nlink.saturating_sub(1);
			p.mtime = ctx.now;
			p.ctime = ctx.now;
		}
		self.inodes.remove(&cino);
		Ok(())
	}

	fn rename(
		&mut self,
		sp: Ino,
		sn: &[u8],
		dp: Ino,
		dn: &[u8],
		ctx: &Ctx,
	) -> Result<(), Error> {
		let src_ino = self.child_of(sp, sn)?;
		{
			let dp_i = self.inodes.get(&dp).ok_or(Error::NoEntry)?;
			if dp_i.kind != Kind::Directory {
				return Err(Error::NotDir);
			}
		}
		if let Some(&di) = self.inodes.get(&dp).unwrap().children.get(dn) {
			if di == src_ino {
				return Ok(());
			}
			let dst_kind = self.inodes.get(&di).map(|c| c.kind);
			match dst_kind {
				Some(Kind::Directory) => {
					if self
						.inodes
						.get(&di)
						.map(|c| !c.children.is_empty())
						.unwrap_or(false)
					{
						return Err(Error::NotEmpty);
					}
					let rm = {
						let c = self.inodes.get_mut(&di).unwrap();
						c.nlink = c.nlink.saturating_sub(1);
						c.nlink == 0
					};
					if rm {
						self.inodes.remove(&di);
					}
					let p = self.inodes.get_mut(&dp).unwrap();
					p.nlink = p.nlink.saturating_sub(1);
				}
				Some(_) => {
					let rm = {
						let c = self.inodes.get_mut(&di).unwrap();
						c.nlink = c.nlink.saturating_sub(1);
						c.nlink == 0
					};
					if rm {
						self.inodes.remove(&di);
					}
				}
				None => {}
			}
		}
		{
			let d = self.inodes.get_mut(&sp).unwrap();
			d.children.remove(sn);
			d.mtime = ctx.now;
			d.ctime = ctx.now;
		}
		{
			let d = self.inodes.get_mut(&dp).unwrap();
			d.children.insert(dn.to_vec(), src_ino);
			d.mtime = ctx.now;
			d.ctime = ctx.now;
		}
		{
			let c = self.inodes.get_mut(&src_ino).unwrap();
			c.parent = dp;
			c.ctime = ctx.now;
		}
		Ok(())
	}

	fn link(&mut self, ino: Ino, newparent: Ino, newname: &[u8], ctx: &Ctx) -> Result<(), Error> {
		{
			let src = self.inodes.get(&ino).ok_or(Error::NoEntry)?;
			if src.kind == Kind::Directory {
				return Err(Error::Perm);
			}
		}
		{
			let p = self.inodes.get(&newparent).ok_or(Error::NoEntry)?;
			if p.kind != Kind::Directory {
				return Err(Error::NotDir);
			}
			if p.children.contains_key(newname) {
				return Err(Error::Exist);
			}
		}
		{
			let src = self.inodes.get_mut(&ino).unwrap();
			src.nlink += 1;
			src.ctime = ctx.now;
		}
		{
			let p = self.inodes.get_mut(&newparent).unwrap();
			p.children.insert(newname.to_vec(), ino);
			p.mtime = ctx.now;
			p.ctime = ctx.now;
		}
		Ok(())
	}

	#[allow(clippy::too_many_arguments)]
	fn setattr(
		&mut self,
		ino: Ino,
		mode: Option<u32>,
		uid: Option<u32>,
		gid: Option<u32>,
		size: Option<u64>,
		atime: Option<SetTime>,
		mtime: Option<SetTime>,
		ctx: &Ctx,
	) -> Result<(), Error> {
		let i = self.inodes.get_mut(&ino).ok_or(Error::NoEntry)?;
		let t = ctx.now;
		if let Some(m) = mode {
			i.perm = (m & 0o7777) as u16;
			i.ctime = t;
		}
		if let Some(u) = uid {
			i.uid = u;
			i.ctime = t;
		}
		if let Some(g) = gid {
			i.gid = g;
			i.ctime = t;
		}
		if let Some(s) = size {
			if i.kind == Kind::Regular {
				match &mut i.data {
					Inline(b) => b.resize(s as usize, 0),
					Extents(map) => map.retain(|&off, _| off < s),
				}
				i.size = s;
				i.mtime = t;
				i.ctime = t;
			} else if i.kind == Kind::Directory && s != 0 {
				return Err(Error::IsDir);
			}
		}
		if let Some(a) = atime {
			i.atime = crate::model::resolve_time(a, t);
		}
		if let Some(m) = mtime {
			i.mtime = crate::model::resolve_time(m, t);
			i.ctime = t;
		}
		Ok(())
	}

	fn write_inline(&mut self, ino: Ino, offset: u64, data: &[u8], ctx: &Ctx) -> Result<(), Error> {
		let i = self.inodes.get_mut(&ino).ok_or(Error::NoEntry)?;
		if i.kind != Kind::Regular {
			return Err(Error::IsDir);
		}
		let buf = match &mut i.data {
			Inline(b) => b,
			Extents(_) => return Err(Error::Invalid),
		};
		let start = offset as usize;
		let end = start + data.len();
		if end > buf.len() {
			buf.resize(end, 0);
		}
		buf[start..end].copy_from_slice(data);
		i.size = buf.len() as u64;
		i.mtime = ctx.now;
		i.ctime = ctx.now;
		Ok(())
	}

	fn write_meta(
		&mut self,
		ino: Ino,
		extents: &[Extent],
		size: u64,
		ctx: &Ctx,
	) -> Result<(), Error> {
		let i = self.inodes.get_mut(&ino).ok_or(Error::NoEntry)?;
		if i.kind != Kind::Regular {
			return Err(Error::IsDir);
		}
		let mut map = BTreeMap::new();
		for e in extents {
			map.insert(e.off, e.clone());
		}
		i.data = Extents(map);
		i.size = size;
		i.mtime = ctx.now;
		i.ctime = ctx.now;
		Ok(())
	}

	#[allow(dead_code)]
	pub fn block_refs(&self) -> Vec<crate::model::Hash> {
		let mut v = Vec::new();
		for i in self.inodes.values() {
			if let Extents(map) = &i.data {
				for e in map.values() {
					v.push(e.block);
				}
			}
		}
		v
	}

	#[allow(dead_code)]
	pub fn next_ino_hint(&self) -> Ino {
		self.next_ino
	}

	#[allow(dead_code)]
	pub fn block_size() -> u64 {
		BLOCK_SIZE
	}

	pub fn snapshot(&self) -> FsmSnapshot {
		FsmSnapshot {
			inodes: self.inodes.clone(),
			next_ino: self.next_ino,
		}
	}

	pub fn restore(snap: FsmSnapshot) -> Self {
		Self {
			inodes: snap.inodes,
			next_ino: snap.next_ino,
		}
	}
}
