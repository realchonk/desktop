use std::io::Cursor;
use std::fmt;
use std::str::FromStr;

use openraft::{BasicNode, Entry, TokioRuntime};
use serde::{Deserialize, Serialize};

use crate::cmd::Cmd;
use crate::model::{Ctx, Error};

/// Stack-allocated, Copy string node ID (up to 31 bytes + null).
/// openraft 0.9 requires `NodeId: Copy`, so `String` cannot be used directly.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct NodeId([u8; 32]);

impl NodeId {
	pub fn new(s: &str) -> Self {
		let mut buf = [0u8; 32];
		let len = s.len().min(31);
		buf[..len].copy_from_slice(&s.as_bytes()[..len]);
		NodeId(buf)
	}
	pub fn as_str(&self) -> &str {
		let end = self.0.iter().position(|&b| b == 0).unwrap_or(32);
		std::str::from_utf8(&self.0[..end]).unwrap_or("")
	}
}

impl fmt::Display for NodeId {
	fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
		f.write_str(self.as_str())
	}
}

impl fmt::Debug for NodeId {
	fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
		write!(f, "{:?}", self.as_str())
	}
}

impl FromStr for NodeId {
	type Err = std::convert::Infallible;
	fn from_str(s: &str) -> Result<Self, Self::Err> {
		Ok(NodeId::new(s))
	}
}

impl From<&str> for NodeId {
	fn from(s: &str) -> Self {
		NodeId::new(s)
	}
}

impl Serialize for NodeId {
	fn serialize<S: serde::Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
		serializer.serialize_str(self.as_str())
	}
}

impl<'de> Deserialize<'de> for NodeId {
	fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
		let s = String::deserialize(deserializer)?;
		Ok(NodeId::new(&s))
	}
}

/// Payload of a Normal Raft log entry: the command plus the deterministic
/// caller context (timestamps / uid / gid) that drives `Fsm::apply`.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct RaftEntryData {
	pub cmd: Cmd,
	pub ctx: Ctx,
}

/// Per-entry application result returned to the writer via `client_write`.
pub type RaftResp = Result<(), Error>;

openraft::declare_raft_types!(
	/// raftfs type configuration.
	pub TypeConfig:
		D = RaftEntryData,
		R = RaftResp,
		NodeId = NodeId,
		Node = BasicNode,
		Entry = Entry<TypeConfig>,
		SnapshotData = Cursor<Vec<u8>>,
		AsyncRuntime = TokioRuntime
);

pub type Raft = openraft::Raft<TypeConfig>;
