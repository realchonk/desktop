use std::io::Cursor;

use openraft::{BasicNode, Entry, TokioRuntime};
use openraft::declare_raft_types;
use serde::{Deserialize, Serialize};

use crate::cmd::Cmd;
use crate::model::{Ctx, Error};

pub type NodeId = u64;

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
