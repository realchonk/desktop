use std::io::Write;
use std::path::{Path, PathBuf};

use crate::model::Hash;

pub trait BlockStore: Send + Sync {
	fn put(&self, data: &[u8]) -> Hash;
	fn get(&self, hash: Hash) -> Option<Vec<u8>>;
}

pub fn hash_of(data: &[u8]) -> Hash {
	*blake3::hash(data).as_bytes()
}

const HEX: &[u8; 16] = b"0123456789abcdef";

fn hex_encode(bytes: &[u8]) -> String {
	let mut s = String::with_capacity(bytes.len() * 2);
	for &b in bytes {
		s.push(HEX[(b >> 4) as usize] as char);
		s.push(HEX[(b & 0x0f) as usize] as char);
	}
	s
}

/// Content-addressed on-disk block store: `root/<b0><b1>/<b2><b3>/<hex>`.
pub struct DiskStore {
	root: PathBuf,
}

impl DiskStore {
	pub fn new(root: &Path) -> std::io::Result<Self> {
		std::fs::create_dir_all(root)?;
		Ok(Self {
			root: root.to_path_buf(),
		})
	}

	pub fn path_for(&self, hash: Hash) -> PathBuf {
		let hex = hex_encode(&hash);
		let top = &hex[0..2];
		let mid = &hex[2..4];
		self.root.join(top).join(mid).join(hex)
	}
}

fn write_atomic(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
	if let Some(parent) = path.parent() {
		std::fs::create_dir_all(parent)?;
	}
	let tmp = path.with_extension("tmp");
	{
		let mut f = std::fs::File::create(&tmp)?;
		f.write_all(bytes)?;
		f.sync_all()?;
	}
	std::fs::rename(&tmp, path)?;
	Ok(())
}

impl BlockStore for DiskStore {
	fn put(&self, data: &[u8]) -> Hash {
		let h = hash_of(data);
		let p = self.path_for(h);
		if !p.exists() {
			let _ = write_atomic(&p, data);
		}
		h
	}

	fn get(&self, hash: Hash) -> Option<Vec<u8>> {
		std::fs::read(self.path_for(hash)).ok()
	}
}
