//! Optional symmetric encryption layer (frontend-only).
//!
//! Encrypted file data and filenames live as ciphertext in the FSM/block store,
//! so storage/replication nodes never see plaintext. Keys are held only by the
//! mounting client (derived from a passphrase); the wrapped master key is stored
//! in the cluster as an opaque blob, useless without the passphrase.
//!
//! Currently wires **file-data** encryption (per-block AEAD + inline). Filename
//! encryption (deterministic AEAD for lookups) is the next addition.

use argon2::{Algorithm, Argon2, Params, Version};
use chacha20poly1305::{
	aead::{Aead, KeyInit, OsRng},
	XChaCha20Poly1305, XNonce,
};
use hkdf::Hkdf;
use rand::RngCore;
use serde::{Deserialize, Serialize};
use sha2::Sha256;

/// 32-byte master key for one encrypted directory.
pub type DirKey = [u8; 32];
/// Per-file key, derived from the dir master key + the inode number.
pub type FileKey = [u8; 32];

pub const NONCE_LEN: usize = 24;
pub const TAG_LEN: usize = 16;

/// A dir master key wrapped by Argon2id(passphrase). Stored as the `.raftfs.enc`
/// marker inside the encrypted directory.
#[derive(Serialize, Deserialize, Clone)]
pub struct WrappedKey {
	pub salt: [u8; 16],
	pub nonce: [u8; NONCE_LEN],
	pub ct: Vec<u8>, // encrypted DirKey (32) + tag (16) = 48 bytes
}

/// Derive a per-file key from the dir master key and the (stable) inode number.
pub fn derive_file_key(dir: &DirKey, ino: u64) -> FileKey {
	let hk = Hkdf::<Sha256>::new(Some(&dir[..16]), dir);
	let mut okm = [0u8; 32];
	// expand never fails for a 32-byte output into a 32-byte key.
	let _ = hk.expand(&ino.to_be_bytes(), &mut okm);
	okm
}

/// Encrypt a plaintext block: output = `nonce(24) || ciphertext || tag(16)`.
pub fn encrypt_block(plain: &[u8], fk: &FileKey) -> Vec<u8> {
	let cipher = XChaCha20Poly1305::new(fk.into());
	let mut nonce = [0u8; NONCE_LEN];
	OsRng.fill_bytes(&mut nonce);
	let ct = cipher
		.encrypt(XNonce::from_slice(&nonce), plain)
		.expect("encrypt");
	let mut out = Vec::with_capacity(NONCE_LEN + ct.len());
	out.extend_from_slice(&nonce);
	out.extend_from_slice(&ct);
	out
}

/// Decrypt a block produced by [`encrypt_block`]; `None` on failure (wrong key /
/// corrupted).
pub fn decrypt_block(blob: &[u8], fk: &FileKey) -> Option<Vec<u8>> {
	if blob.len() < NONCE_LEN + TAG_LEN {
		return None;
	}
	let (nonce, ct) = blob.split_at(NONCE_LEN);
	let cipher = XChaCha20Poly1305::new(fk.into());
	cipher
		.decrypt(XNonce::from_slice(nonce), ct)
		.ok()
}

/// Wrap a dir master key with a passphrase (Argon2id → KEK → XChaCha20-Poly1305).
pub fn wrap_key(master: &DirKey, passphrase: &str) -> WrappedKey {
	let mut salt = [0u8; 16];
	OsRng.fill_bytes(&mut salt);
	let kek = derive_kek(passphrase, &salt);
	let cipher = XChaCha20Poly1305::new(kek.as_ref().into());
	let mut nonce = [0u8; NONCE_LEN];
	OsRng.fill_bytes(&mut nonce);
	let ct = cipher
		.encrypt(XNonce::from_slice(&nonce), master.as_slice())
		.expect("encrypt");
	WrappedKey { salt, nonce, ct }
}

/// Unwrap a dir master key with a passphrase; `None` if the passphrase is wrong.
pub fn unwrap_key(wrapped: &WrappedKey, passphrase: &str) -> Option<DirKey> {
	let kek = derive_kek(passphrase, &wrapped.salt);
	let cipher = XChaCha20Poly1305::new(kek.as_ref().into());
	cipher
		.decrypt(XNonce::from_slice(&wrapped.nonce), wrapped.ct.as_slice())
		.ok()
		.and_then(|pt| {
			if pt.len() == 32 {
				let mut k = [0u8; 32];
				k.copy_from_slice(&pt);
				Some(k)
			} else {
				None
			}
		})
}

fn derive_kek(passphrase: &str, salt: &[u8; 16]) -> FileKey {
	let params = Params::new(64 * 1024, 3, 4, Some(32)).expect("argon2 params");
	let argon2 = Argon2::new(Algorithm::Argon2id, Version::V0x13, params);
	let mut out = [0u8; 32];
	let _ = argon2.hash_password_into(passphrase.as_bytes(), salt, &mut out);
	out
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn roundtrip_block() {
		let fk = derive_file_key(&[7u8; 32], 42);
		let blob = encrypt_block(b"hello encryption", &fk);
		assert_eq!(decrypt_block(&blob, &fk).unwrap(), b"hello encryption");
	}

	#[test]
	fn wrong_key_fails() {
		let fk = derive_file_key(&[7u8; 32], 42);
		let other = derive_file_key(&[8u8; 32], 42);
		let blob = encrypt_block(b"secret", &fk);
		assert!(decrypt_block(&blob, &other).is_none());
	}

	#[test]
	fn wrap_roundtrip() {
		let master = [9u8; 32];
		let w = wrap_key(&master, "correct horse battery staple");
		assert_eq!(unwrap_key(&w, "correct horse battery staple").unwrap(), master);
		assert!(unwrap_key(&w, "wrong").is_none());
	}
}
