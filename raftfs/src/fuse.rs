use std::collections::HashMap;
use std::ffi::OsStr;
use std::future::Future;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;
use std::sync::{mpsc, Arc, Mutex};
use std::time::{Duration, SystemTime};

use fuser::{
	BsdFileFlags, Errno, FileAttr, FileHandle, FileType, Filesystem, FopenFlags, Generation, INodeNo,
	KernelConfig, LockOwner, OpenFlags, RenameFlags, ReplyAttr, ReplyCreate, ReplyData,
	ReplyDirectory, ReplyEmpty, ReplyEntry, ReplyOpen, ReplyStatfs, ReplyWrite, Request, TimeOrNow,
	WriteFlags,
};
use tokio::runtime::Handle;
use rand::RngCore;

use crate::block::{BlockStore, DiskStore};
use crate::cmd::Cmd;
use crate::fsm::Fsm;
use crate::model::{Attr, Ctx, Error, Extent, FileData, Hash, Kind, SetTime, INLINE_THRESHOLD};
use crate::net;
use crate::raft::{Raft as RaftHandle, RaftEntryData};

const TTL: Duration = Duration::from_secs(1);
const ENC_MARKER: &[u8] = b".raft-encryptdir";
const VIRT_BIT: u64 = 1u64 << 62;

fn is_virt(ino: u64) -> bool { ino & VIRT_BIT != 0 }
fn virt_ino(dir: u64) -> u64 { dir | VIRT_BIT }
fn virt_dir(ino: u64) -> u64 { ino & !VIRT_BIT }

fn trace(s: impl AsRef<str>) {
	use std::io::Write;
	use std::sync::OnceLock;
	static ON: OnceLock<bool> = OnceLock::new();
	let on = *ON.get_or_init(|| std::env::var("RAFTFS_TRACE").is_ok());
	if !on {
		return;
	}
	if let Ok(mut f) = std::fs::OpenOptions::new().append(true).create(true).open("/tmp/opencode/fuse.trace") {
		let _ = writeln!(f, "{}", s.as_ref());
		let _ = f.flush();
	}
}

pub struct Frontend {
	rt: Handle,
	raft: RaftHandle,
	fsm: Arc<Mutex<Fsm>>,
	disk: Arc<DiskStore>,
	id: crate::raft::NodeId,
	keys: Mutex<HashMap<u64, crate::crypto::DirKey>>,
}

impl Frontend {
	pub fn new(rt: Handle, raft: RaftHandle, fsm: Arc<Mutex<Fsm>>, disk: Arc<DiskStore>, id: crate::raft::NodeId) -> Self {
		Self { rt, raft, fsm, disk, id, keys: Mutex::new(HashMap::new()) }
	}

	fn ctx_from(req: &Request) -> Ctx {
		Ctx { now: SystemTime::now(), uid: req.uid(), gid: req.gid() }
	}

	/// Spawn `fut` on the runtime, block this (FUSE) thread on a std channel until it finishes.
	/// Avoids `Handle::block_on`, which wedges when mixed with the runtime's own tasks here.
	fn spawn_recv<T: Send + 'static>(&self, fut: impl Future<Output = T> + Send + 'static) -> T {
		let (tx, rx) = mpsc::channel();
		self.rt.spawn(async move {
			let _ = tx.send(fut.await);
		});
		rx.recv().expect("raft task died")
	}

	/// Submit a command through Raft. If this node is not the leader, the
	/// command is forwarded automatically. Blocks the FUSE thread.
	fn raft_write(&self, cmd: Cmd, ctx: Ctx) -> Result<(), Error> {
		let raft = self.raft.clone();
		self.spawn_recv(async move { raft_write_inline(&raft, cmd, ctx).await })
	}

	/// Look up a name, retrying briefly (for forwarded writes that haven't
	/// replicated to the local FSM yet).
	fn wait_lookup(&self, parent: u64, name: &[u8]) -> Option<Attr> {
		for _ in 0..30 {
			let found = {
				let g = self.fsm.lock().unwrap();
				g.lookup(parent, name).and_then(|i| g.attr(i))
			};
			if found.is_some() {
				return found;
			}
			std::thread::sleep(std::time::Duration::from_millis(25));
		}
		None
	}

	// ---- per-directory encryption helpers ----

	/// Does this directory contain the encryption marker?
	fn dir_has_marker(&self, dir_ino: u64) -> bool {
		self.fsm.lock().unwrap().lookup(dir_ino, ENC_MARKER).is_some()
	}

	/// Is this directory a direct child of root (but not root itself)?
	fn is_top_level(&self, dir_ino: u64) -> bool {
		dir_ino != 1 && self.fsm.lock().unwrap().parent_of(dir_ino) == Some(1)
	}

	/// Effective encryption key for a file: walk the parent chain looking for an
	/// unlocked encrypted ancestor; fall back to the mount-wide key.
	fn effective_file_key(&self, ino: u64) -> Option<crate::crypto::DirKey> {
		let keys = self.keys.lock().unwrap();
		let g = self.fsm.lock().unwrap();
		let mut cur = ino;
		for _ in 0..64 {
			if let Some(k) = keys.get(&cur) {
				return Some(*k);
			}
			match g.parent_of(cur) {
				Some(p) if p != cur => cur = p,
				_ => break,
			}
		}
		None
	}

	/// Write a passphrase to the virtual `lock` file of a plain dir → encrypt it.
	fn do_lock(&self, dir_ino: u64, passphrase: &[u8]) -> Result<(), Error> {
		let pass = std::str::from_utf8(passphrase).map_err(|_| Error::Invalid)?;
		let pass = pass.trim();
		// Generate a random dir key and wrap it.
		let mut key = [0u8; 32];
		rand::rngs::OsRng.fill_bytes(&mut key);
		let wrapped = crate::crypto::wrap_key(&key, pass);
		let blob = bincode::serialize(&wrapped).map_err(|_| Error::Io)?;
		// Create the marker file via Raft.
		let ctx = Ctx { now: SystemTime::now(), uid: 0, gid: 0 };
		self.raft_write(Cmd::Create { parent: dir_ino, name: ENC_MARKER.to_vec(), perm: 0o600 }, ctx.clone())?;
		// Wait for the marker to replicate to the local FSM (follower may lag).
		let marker_ino = match self.wait_lookup(dir_ino, ENC_MARKER) {
			Some(attr) => attr.ino,
			None => return Err(Error::Io),
		};
		self.raft_write(Cmd::WriteInline { ino: marker_ino, offset: 0, data: blob }, ctx)?;
		// Store the key so the dir is immediately unlocked.
		self.keys.lock().unwrap().insert(dir_ino, key);
		Ok(())
	}

	/// Write a passphrase to the virtual `unlock` file of a locked dir → unlock it.
	fn do_unlock(&self, dir_ino: u64, passphrase: &[u8]) -> Result<(), Error> {
		let pass = std::str::from_utf8(passphrase).map_err(|_| Error::Invalid)?;
		let pass = pass.trim();
		let marker_ino = self.fsm.lock().unwrap().lookup(dir_ino, ENC_MARKER).ok_or(Error::NoEntry)?;
		let (data, _) = self.fsm.lock().unwrap().file_data(marker_ino).ok_or(Error::NoEntry)?;
		let blob = match &data {
			FileData::Inline(b) => b.clone(),
			_ => return Err(Error::Io),
		};
		let wrapped: crate::crypto::WrappedKey = bincode::deserialize(&blob).map_err(|_| Error::Io)?;
		let key = crate::crypto::unwrap_key(&wrapped, pass).ok_or(Error::Acces)?;
		self.keys.lock().unwrap().insert(dir_ino, key);
		Ok(())
	}
}

// ---- async helpers (take owned Arc clones so they are 'static + Send) ----

async fn peer_addrs(raft: &RaftHandle, self_id: crate::raft::NodeId) -> Vec<String> {
	let m = raft.metrics().borrow().clone();
	m.membership_config
		.membership()
		.nodes()
		.filter(|(id, _)| **id != self_id)
		.map(|(_, n)| n.addr.clone())
		.collect()
}

async fn replicate_blocks(
	raft: &RaftHandle,
	id: crate::raft::NodeId,
	blocks: &[(Hash, Vec<u8>)],
) -> Result<(), Error> {
	let peers = peer_addrs(raft, id).await;
	if peers.is_empty() {
		return Ok(());
	}
	for peer in &peers {
		for (_h, b) in blocks {
			net::block_put(peer, b.clone()).await.map_err(|_| Error::Io)?;
		}
	}
	Ok(())
}

async fn lazy_fetch(disk: &DiskStore, raft: &RaftHandle, id: crate::raft::NodeId, hash: Hash) -> Option<Vec<u8>> {
	for addr in peer_addrs(raft, id).await {
		if let Ok(Some(b)) = net::block_get(&addr, hash).await {
			disk.put(&b);
			return Some(b);
		}
	}
	None
}

async fn materialize(
	disk: &DiskStore,
	raft: &RaftHandle,
	id: crate::raft::NodeId,
	ino: u64,
	enc: Option<crate::crypto::DirKey>,
	data: &FileData,
	size: usize,
) -> Vec<u8> {
	match data {
		FileData::Inline(b) => {
			let mut v = b.clone();
			v.resize(size, 0);
			v
		}
		FileData::Extents(map) => {
			let mut v = vec![0u8; size];
			for (off, e) in map {
				let raw = match disk.get(e.block) {
					Some(b) => b,
					None => lazy_fetch(disk, raft, id, e.block).await.unwrap_or_default(),
				};
				let bytes = match enc {
					Some(k) => {
						let fk = crate::crypto::derive_file_key(&k, ino);
						crate::crypto::decrypt_block(&raw, &fk).unwrap_or_default()
					}
					None => raw,
				};
				let start = *off as usize;
				if start < size {
					let take = bytes.len().min(size - start);
					v[start..start + take].copy_from_slice(&bytes[..take]);
				}
			}
			v
		}
	}
}

async fn do_write(
	raft: RaftHandle,
	fsm: Arc<Mutex<Fsm>>,
	disk: Arc<DiskStore>,
	id: crate::raft::NodeId,
	ino: u64,
	offset: u64,
	data: Vec<u8>,
	ctx: Ctx,
	enc: Option<crate::crypto::DirKey>,
) -> Result<u32, Error> {
	let n = data.len() as u32;
	let new_end = offset + data.len() as u64;
	let (cur_data, cur_size) = fsm.lock().unwrap().file_data(ino).ok_or(Error::NoEntry)?;
	let stays_inline =
		enc.is_none() && matches!(cur_data, FileData::Inline(_)) && new_end <= INLINE_THRESHOLD as u64;
	if stays_inline {
		let cmd = Cmd::WriteInline { ino, offset, data };
		raft_write_inline(&raft, cmd, ctx).await?;
	} else {
		let mut buf = materialize(&disk, &raft, id, ino, enc, &cur_data, cur_size as usize).await;
		let start = offset as usize;
		let end = start + data.len();
		if end > buf.len() {
			buf.resize(end, 0);
		}
		buf[start..end].copy_from_slice(&data);
		let mut new_blocks: Vec<(Hash, Vec<u8>)> = Vec::new();
		let mut extents: Vec<Extent> = Vec::new();
		let mut off = 0u64;
		for chunk in buf.chunks(INLINE_THRESHOLD) {
			let stored: Vec<u8> = match enc {
				Some(k) => crate::crypto::encrypt_block(chunk, &crate::crypto::derive_file_key(&k, ino)),
				None => chunk.to_vec(),
			};
			let h = disk.put(&stored);
			new_blocks.push((h, stored.clone()));
			extents.push(Extent { off, len: chunk.len() as u64, block: h });
			off += chunk.len() as u64;
		}
		replicate_blocks(&raft, id, &new_blocks).await?;
		let cmd = Cmd::WriteMeta { ino, extents, size: buf.len() as u64 };
		raft_write_inline(&raft, cmd, ctx).await?;
	}
	Ok(n)
}

async fn raft_write_inline(raft: &RaftHandle, cmd: Cmd, ctx: Ctx) -> Result<(), Error> {
	let m = raft.metrics().borrow().clone();
	let entry = RaftEntryData { cmd, ctx };
	if m.current_leader == Some(m.id) {
		match raft.client_write(entry).await {
			Ok(resp) => resp.data,
			Err(e) => {
				eprintln!("raftfs: client_write failed: {e:?}");
				Err(Error::Io)
			}
		}
	} else {
		let leader_id = m.current_leader.ok_or(Error::Io)?;
		let addr = m.membership_config
			.membership()
			.get_node(&leader_id)
			.map(|n| n.addr.clone())
			.ok_or(Error::Io)?;
		net::forward_cmd(&addr, entry)
			.await
			.map_err(|e| {
				eprintln!("raftfs: forward to leader failed: {e}");
				Error::Io
			})?
	}
}

fn virt_fileattr(ino: u64) -> FileAttr {
	let now = SystemTime::now();
	FileAttr {
		ino: INodeNo(ino), size: 0, blocks: 0, atime: now, mtime: now, ctime: now, crtime: now,
		kind: FileType::RegularFile, perm: 0o666, nlink: 1, uid: 0, gid: 0, rdev: 0, blksize: 4096, flags: 0,
	}
}

fn to_filetype(k: Kind) -> FileType {
	match k {
		Kind::Regular => FileType::RegularFile,
		Kind::Directory => FileType::Directory,
		Kind::Symlink => FileType::Symlink,
	}
}

fn to_fileattr(a: &Attr) -> FileAttr {
	FileAttr {
		ino: INodeNo(a.ino),
		size: a.size,
		blocks: a.blocks,
		atime: a.atime,
		mtime: a.mtime,
		ctime: a.ctime,
		crtime: a.crtime,
		kind: to_filetype(a.kind),
		perm: a.perm,
		nlink: a.nlink,
		uid: a.uid,
		gid: a.gid,
		rdev: 0,
		blksize: crate::model::BLOCK_SIZE as u32,
		flags: 0,
	}
}

fn to_time(t: TimeOrNow) -> SetTime {
	match t {
		TimeOrNow::SpecificTime(s) => SetTime::Specific(s),
		TimeOrNow::Now => SetTime::Now,
	}
}

fn to_errno(e: Error) -> Errno {
	match e {
		Error::NoEntry => Errno::ENOENT,
		Error::NotDir => Errno::ENOTDIR,
		Error::IsDir => Errno::EISDIR,
		Error::Exist => Errno::EEXIST,
		Error::NotEmpty => Errno::ENOTEMPTY,
		Error::Invalid => Errno::EINVAL,
		Error::NoSupp => Errno::ENOTSUP,
		Error::Acces => Errno::EACCES,
		Error::Perm => Errno::EPERM,
		Error::Io => Errno::EIO,
		Error::Range => Errno::ERANGE,
	}
}

impl Filesystem for Frontend {
	fn init(&mut self, _req: &Request, _config: &mut KernelConfig) -> std::io::Result<()> {
		Ok(())
	}

	fn getattr(&self, _req: &Request, ino: INodeNo, _fh: Option<FileHandle>, reply: ReplyAttr) {
		if is_virt(ino.0) {
			return reply.attr(&TTL, &virt_fileattr(ino.0));
		}
		match self.fsm.lock().unwrap().attr(ino.0) {
			Some(a) => reply.attr(&TTL, &to_fileattr(&a)),
			None => reply.error(Errno::ENOENT),
		}
	}

	fn lookup(&self, _req: &Request, parent: INodeNo, name: &OsStr, reply: ReplyEntry) {
		if is_virt(parent.0) {
			return reply.error(Errno::ENOTDIR);
		}
		let nb = name.as_bytes();
		let has_marker = self.dir_has_marker(parent.0);
		let unlocked = self.keys.lock().unwrap().contains_key(&parent.0);

		if has_marker && !unlocked {
			// Locked: only "unlock" is visible.
			if nb == b"unlock" {
				return reply.entry(&TTL, &virt_fileattr(virt_ino(parent.0)), Generation(0));
			}
			return reply.error(Errno::ENOENT);
		}
		// Plain or Unlocked.
		if nb == ENC_MARKER {
			if !has_marker && self.is_top_level(parent.0) {
				return reply.entry(&TTL, &virt_fileattr(virt_ino(parent.0)), Generation(0));
			}
			return reply.error(Errno::ENOENT);
		}
		let found = {
			let g = self.fsm.lock().unwrap();
			g.lookup(parent.0, nb).and_then(|i| g.attr(i))
		};
		match found {
			Some(a) => reply.entry(&TTL, &to_fileattr(&a), Generation(0)),
			None => reply.error(Errno::ENOENT),
		}
	}

	fn readdir(&self, _req: &Request, ino: INodeNo, _fh: FileHandle, offset: u64, mut reply: ReplyDirectory) {
		if is_virt(ino.0) {
			return reply.error(Errno::ENOTDIR);
		}
		let has_marker = self.dir_has_marker(ino.0);
		let unlocked = self.keys.lock().unwrap().contains_key(&ino.0);
		let locked = has_marker && !unlocked;

		let parent = self.fsm.lock().unwrap().parent_of(ino.0).unwrap_or(1);
		let mut idx: u64 = 0;
		idx += 1;
		if idx > offset && reply.add(INodeNo(ino.0), idx, FileType::Directory, ".") {
			return reply.ok();
		}
		idx += 1;
		if idx > offset && reply.add(INodeNo(parent), idx, FileType::Directory, "..") {
			return reply.ok();
		}
		if locked {
			idx += 1;
			if idx > offset {
				let _ = reply.add(INodeNo(virt_ino(ino.0)), idx, FileType::RegularFile, "unlock");
			}
			return reply.ok();
		}
		// Plain or Unlocked: list real children (minus marker) + virtual "lock" if Plain.
		let rd = self.fsm.lock().unwrap().read_dir(ino.0);
		let Some((_parent2, entries)) = rd else {
			return reply.error(Errno::ENOENT);
		};
		for (name, cino, kind) in &entries {
			if name.as_slice() == ENC_MARKER {
				continue;
			}
			idx += 1;
			if idx <= offset {
				continue;
			}
			if reply.add(INodeNo(*cino), idx, to_filetype(*kind), OsStr::from_bytes(name)) {
				break;
			}
		}
		if !has_marker && self.is_top_level(ino.0) {
			idx += 1;
			if idx > offset {
				let _ = reply.add(INodeNo(virt_ino(ino.0)), idx, FileType::RegularFile, OsStr::from_bytes(ENC_MARKER));
			}
		}
		reply.ok();
	}

	fn read(&self, _req: &Request, ino: INodeNo, _fh: FileHandle, offset: u64, size: u32, _flags: OpenFlags, _lo: Option<LockOwner>, reply: ReplyData) {
		if is_virt(ino.0) {
			let hint = b"write passphrase to lock or unlock\n";
			let start = (offset as usize).min(hint.len());
			let end = (start + size as usize).min(hint.len());
			return reply.data(&hint[start..end]);
		}
		let (data, fsize) = match self.fsm.lock().unwrap().file_data(ino.0) {
			Some(x) => x,
			None => return reply.error(Errno::ENOENT),
		};
		let id = self.id;
		let buf = match &data {
			FileData::Inline(b) => {
				let mut v = b.clone();
				v.resize(fsize as usize, 0);
				v
			}
			FileData::Extents(_) => {
				let disk = self.disk.clone();
				let raft = self.raft.clone();
				let enc = self.effective_file_key(ino.0);
				let data2 = data.clone();
				self.spawn_recv(async move { materialize(&disk, &raft, id, ino.0, enc, &data2, fsize as usize).await })
			}
		};
		let len = buf.len();
		let start = (offset as usize).min(len);
		let end = (start + size as usize).min(len);
		reply.data(&buf[start..end]);
	}

	fn write(&self, _req: &Request, ino: INodeNo, _fh: FileHandle, offset: u64, data: &[u8], _wf: WriteFlags, _flags: OpenFlags, _lo: Option<LockOwner>, reply: ReplyWrite) {
		if is_virt(ino.0) {
			let dir = virt_dir(ino.0);
			let has_marker = self.dir_has_marker(dir);
			let unlocked = self.keys.lock().unwrap().contains_key(&dir);
			let res = if has_marker && !unlocked {
				self.do_unlock(dir, data)
			} else if !has_marker {
				self.do_lock(dir, data)
			} else {
				Err(Error::Exist)
			};
			match res {
				Ok(()) => reply.written(data.len() as u32),
				Err(e) => reply.error(to_errno(e)),
			}
			return;
		}
		let enc = self.effective_file_key(ino.0);
		let ctx = Self::ctx_from(_req);
		let raft = self.raft.clone();
		let fsm = self.fsm.clone();
		let disk = self.disk.clone();
		let id = self.id;
		let data_owned = data.to_vec();
		let res = self.spawn_recv(async move {
			do_write(raft, fsm, disk, id, ino.0, offset, data_owned, ctx, enc).await
		});
		match res {
			Ok(n) => reply.written(n),
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn create(&self, req: &Request, parent: INodeNo, name: &OsStr, mode: u32, _umask: u32, _flags: i32, reply: ReplyCreate) {
		let nb = name.as_bytes();
		let has_marker = self.dir_has_marker(parent.0);
		let unlocked = self.keys.lock().unwrap().contains_key(&parent.0);
		// Locked dir: refuse all creates except "unlock" (virtual file).
		if has_marker && !unlocked {
			if nb == b"unlock" {
				return reply.created(&TTL, &virt_fileattr(virt_ino(parent.0)), Generation(0), FileHandle(0), FopenFlags::empty());
			}
			return reply.error(Errno::EACCES);
		}
		// Plain top-level dir: ENC_MARKER is the virtual encrypt file.
		if !has_marker && self.is_top_level(parent.0) && nb == ENC_MARKER {
			return reply.created(&TTL, &virt_fileattr(virt_ino(parent.0)), Generation(0), FileHandle(0), FopenFlags::empty());
		}
		// Never allow creating the marker directly.
		if nb == ENC_MARKER {
			return reply.error(Errno::EACCES);
		}
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::Create { parent: parent.0, name: nb.to_vec(), perm: (mode & 0o7777) as u16 };
		match self.raft_write(cmd, ctx) {
			Ok(()) => match self.wait_lookup(parent.0, nb) {
				Some(a) => reply.created(&TTL, &to_fileattr(&a), Generation(0), FileHandle(0), FopenFlags::empty()),
				None => reply.error(Errno::EIO),
			}
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn mkdir(&self, req: &Request, parent: INodeNo, name: &OsStr, mode: u32, _umask: u32, reply: ReplyEntry) {
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::MkDir { parent: parent.0, name: name.as_bytes().to_vec(), perm: (mode & 0o7777) as u16 };
		match self.raft_write(cmd, ctx) {
			Ok(()) => match self.wait_lookup(parent.0, name.as_bytes()) {
				Some(a) => reply.entry(&TTL, &to_fileattr(&a), Generation(0)),
				None => reply.error(Errno::EIO),
			}
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn symlink(&self, req: &Request, parent: INodeNo, link_name: &OsStr, target: &Path, reply: ReplyEntry) {
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::Symlink { parent: parent.0, name: link_name.as_bytes().to_vec(), target: target.as_os_str().as_bytes().to_vec() };
		match self.raft_write(cmd, ctx) {
			Ok(()) => match self.wait_lookup(parent.0, link_name.as_bytes()) {
				Some(a) => reply.entry(&TTL, &to_fileattr(&a), Generation(0)),
				None => reply.error(Errno::EIO),
			}
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn readlink(&self, _req: &Request, ino: INodeNo, reply: ReplyData) {
		match self.fsm.lock().unwrap().readlink(ino.0) {
			Some(t) => reply.data(&t),
			None => reply.error(Errno::EINVAL),
		}
	}

	fn unlink(&self, req: &Request, parent: INodeNo, name: &OsStr, reply: ReplyEmpty) {
		let nb = name.as_bytes();
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::Unlink { parent: parent.0, name: nb.to_vec() };
		match self.raft_write(cmd, ctx) {
			Ok(()) => reply.ok(),
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn rmdir(&self, req: &Request, parent: INodeNo, name: &OsStr, reply: ReplyEmpty) {
		let nb = name.as_bytes();
		let target = {
			let g = self.fsm.lock().unwrap();
			g.lookup(parent.0, nb).and_then(|i| g.attr(i))
		};
		if let Some(a) = target {
			if a.kind == Kind::Directory {
				let has_marker = self.dir_has_marker(a.ino);
				let unlocked = self.keys.lock().unwrap().contains_key(&a.ino);
				if has_marker && unlocked {
					// Re-lock: drop the key from memory.
					self.keys.lock().unwrap().remove(&a.ino);
					return reply.ok();
				}
				if has_marker && !unlocked {
					// Locked: permanently delete everything inside, then the dir.
					let children: Vec<Vec<u8>> = {
						let g = self.fsm.lock().unwrap();
						g.read_dir(a.ino).map(|(_, entries)| entries.into_iter().map(|(n, _, _)| n).collect()).unwrap_or_default()
					};
					for child in &children {
						let ctx = Ctx { now: SystemTime::now(), uid: req.uid(), gid: req.gid() };
						match self.raft_write(Cmd::Unlink { parent: a.ino, name: child.clone() }, ctx) {
							Ok(()) => {}
							Err(e) => return reply.error(to_errno(e)),
						}
					}
					let ctx = Ctx { now: SystemTime::now(), uid: req.uid(), gid: req.gid() };
					match self.raft_write(Cmd::Rmdir { parent: parent.0, name: nb.to_vec() }, ctx) {
						Ok(()) => {
							self.keys.lock().unwrap().remove(&a.ino);
							return reply.ok();
						}
						Err(e) => return reply.error(to_errno(e)),
					}
				}
			}
		}
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::Rmdir { parent: parent.0, name: nb.to_vec() };
		match self.raft_write(cmd, ctx) {
			Ok(()) => reply.ok(),
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn link(&self, req: &Request, ino: INodeNo, newparent: INodeNo, newname: &OsStr, reply: ReplyEntry) {
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::Link { ino: ino.0, newparent: newparent.0, newname: newname.as_bytes().to_vec() };
		match self.raft_write(cmd, ctx) {
			Ok(()) => match self.fsm.lock().unwrap().attr(ino.0) {
				Some(a) => reply.entry(&TTL, &to_fileattr(&a), Generation(0)),
				None => reply.error(Errno::EIO),
			},
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn rename(&self, req: &Request, parent: INodeNo, name: &OsStr, newparent: INodeNo, newname: &OsStr, _flags: RenameFlags, reply: ReplyEmpty) {
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::Rename { sp: parent.0, sn: name.as_bytes().to_vec(), dp: newparent.0, dn: newname.as_bytes().to_vec() };
		match self.raft_write(cmd, ctx) {
			Ok(()) => reply.ok(),
			Err(e) => reply.error(to_errno(e)),
		}
	}

	#[allow(clippy::too_many_arguments)]
	fn setattr(&self, req: &Request, ino: INodeNo, mode: Option<u32>, uid: Option<u32>, gid: Option<u32>, size: Option<u64>, atime: Option<TimeOrNow>, mtime: Option<TimeOrNow>, _ctime: Option<SystemTime>, _fh: Option<FileHandle>, _crtime: Option<SystemTime>, _chgtime: Option<SystemTime>, _bkuptime: Option<SystemTime>, _flags: Option<BsdFileFlags>, reply: ReplyAttr) {
		if is_virt(ino.0) {
			return reply.attr(&TTL, &virt_fileattr(ino.0));
		}
		let ctx = Self::ctx_from(req);
		let cmd = Cmd::SetAttr { ino: ino.0, mode, uid, gid, size, atime: atime.map(to_time), mtime: mtime.map(to_time) };
		match self.raft_write(cmd, ctx) {
			Ok(()) => match self.fsm.lock().unwrap().attr(ino.0) {
				Some(a) => reply.attr(&TTL, &to_fileattr(&a)),
				None => reply.error(Errno::ENOENT),
			},
			Err(e) => reply.error(to_errno(e)),
		}
	}

	fn open(&self, _req: &Request, ino: INodeNo, _flags: OpenFlags, reply: ReplyOpen) {
		trace(format!("open {}", ino.0));
		reply.opened(FileHandle(0), FopenFlags::empty());
	}
	fn release(&self, _req: &Request, ino: INodeNo, _fh: FileHandle, _flags: OpenFlags, _lo: Option<LockOwner>, _flush: bool, reply: ReplyEmpty) {
		trace(format!("release {}", ino.0));
		reply.ok();
	}
	fn opendir(&self, _req: &Request, _ino: INodeNo, _flags: OpenFlags, reply: ReplyOpen) {
		reply.opened(FileHandle(0), FopenFlags::empty());
	}
	fn releasedir(&self, _req: &Request, _ino: INodeNo, _fh: FileHandle, _flags: OpenFlags, reply: ReplyEmpty) {
		reply.ok();
	}
	fn fsync(&self, _req: &Request, _ino: INodeNo, _fh: FileHandle, _datasync: bool, reply: ReplyEmpty) {
		reply.ok();
	}
	fn statfs(&self, _req: &Request, _ino: INodeNo, reply: ReplyStatfs) {
		reply.statfs(0, 0, 0, 0, 0, crate::model::BLOCK_SIZE as u32, 255, crate::model::BLOCK_SIZE as u32);
	}
}

pub fn mount(mountpoint: &Path, rt: Handle, raft: RaftHandle, fsm: Arc<Mutex<Fsm>>, disk: Arc<DiskStore>, self_id: crate::raft::NodeId) -> std::io::Result<()> {
	let fs = Frontend::new(rt, raft, fsm, disk, self_id);
	let mut config = fuser::Config::default();
	config.mount_options = vec![];
	fuser::mount(fs, mountpoint, &config)
}
