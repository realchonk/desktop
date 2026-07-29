use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use openraft::storage::{RaftSnapshotBuilder, RaftStateMachine, Snapshot};
use openraft::{
	BasicNode, Entry, EntryPayload, ErrorSubject, ErrorVerb, LogId, SnapshotMeta, StorageError,
	StorageIOError, StoredMembership,
};
use serde::{Deserialize, Serialize};

use crate::fsm::{Fsm, FsmSnapshot};
use crate::raft::{RaftEntryData, RaftResp, TypeConfig};

type SResult<T> = Result<T, StorageError<u64>>;

#[derive(Default, Clone)]
struct SmMeta {
	last_applied: Option<LogId<u64>>,
	last_membership: StoredMembership<u64, BasicNode>,
}

#[derive(Serialize, Deserialize)]
struct SmSnapData {
	last_applied: Option<LogId<u64>>,
	last_membership: StoredMembership<u64, BasicNode>,
	fsm: FsmSnapshot,
}

pub struct SmStore {
	fsm: Arc<Mutex<Fsm>>,
	meta: Arc<Mutex<SmMeta>>,
	snapshot_path: PathBuf,
	current: Mutex<Option<Snapshot<TypeConfig>>>,
}

fn berr(e: bincode::Error, write: bool) -> StorageError<u64> {
	StorageError::IO {
		source: StorageIOError::new(
			ErrorSubject::Store,
			if write { ErrorVerb::Write } else { ErrorVerb::Read },
			&e,
		),
	}
}

fn ierr(e: std::io::Error, write: bool) -> StorageError<u64> {
	StorageError::IO {
		source: StorageIOError::new(
			ErrorSubject::Store,
			if write { ErrorVerb::Write } else { ErrorVerb::Read },
			&e,
		),
	}
}

fn tr(s: &str) {
	use std::io::Write;
	use std::sync::OnceLock;
	static ON: OnceLock<bool> = OnceLock::new();
	if !*ON.get_or_init(|| std::env::var("RAFTFS_TRACE").is_ok()) {
		return;
	}
	if let Ok(mut f) = std::fs::OpenOptions::new().append(true).create(true).open("/tmp/opencode/sm.trace") {
		let _ = writeln!(f, "{s}");
		let _ = f.flush();
	}
}

impl SmStore {
	pub fn new(fsm: Arc<Mutex<Fsm>>, dir: &Path) -> Self {
		let snapshot_path = dir.join("snapshot.bin");
		let mut current = Mutex::new(None);
		if let Ok(bytes) = std::fs::read(&snapshot_path) {
			if !bytes.is_empty() {
				if let Ok(data) = bincode::deserialize::<SmSnapData>(&bytes) {
					{
						let mut f = fsm.lock().unwrap();
						*f = Fsm::restore(data.fsm);
					}
					let meta = SnapshotMeta {
						last_log_id: data.last_applied,
						last_membership: data.last_membership.clone(),
						snapshot_id: snap_id(&data.last_applied, 0),
					};
					*current.lock().unwrap() = Some(Snapshot {
						meta,
						snapshot: Box::new(Cursor::new(bytes.clone())),
					});
				}
			}
		}
		Self {
			fsm,
			meta: Arc::new(Mutex::new(SmMeta::default())),
			snapshot_path,
			current,
		}
	}

	pub fn fsm_handle(&self) -> Arc<Mutex<Fsm>> {
		self.fsm.clone()
	}
}

fn snap_id(last: &Option<LogId<u64>>, idx: u64) -> String {
	match last {
		Some(l) => format!("{}-{}-{}", l.leader_id, l.index, idx),
		None => format!("--{}", idx),
	}
}

pub struct SmSnapBuilder {
	fsm: Arc<Mutex<Fsm>>,
	meta: Arc<Mutex<SmMeta>>,
}

impl RaftSnapshotBuilder<TypeConfig> for SmSnapBuilder {
	async fn build_snapshot(&mut self) -> SResult<Snapshot<TypeConfig>> {
		let (fsm_snap, last_applied, last_membership) = {
			let f = self.fsm.lock().unwrap();
			let m = self.meta.lock().unwrap();
			(f.snapshot(), m.last_applied, m.last_membership.clone())
		};
		let data = SmSnapData {
			last_applied,
			last_membership: last_membership.clone(),
			fsm: fsm_snap,
		};
		let bytes = bincode::serialize(&data).map_err(|e| berr(e, true))?;
		let meta = SnapshotMeta {
			last_log_id: last_applied,
			last_membership,
			snapshot_id: snap_id(&last_applied, 1),
		};
		Ok(Snapshot {
			meta,
			snapshot: Box::new(Cursor::new(bytes)),
		})
	}
}

impl RaftStateMachine<TypeConfig> for SmStore {
	type SnapshotBuilder = SmSnapBuilder;

	async fn applied_state(
		&mut self,
	) -> SResult<(Option<LogId<u64>>, StoredMembership<u64, BasicNode>)> {
		let m = self.meta.lock().unwrap();
		Ok((m.last_applied, m.last_membership.clone()))
	}

	async fn apply<I>(&mut self, entries: I) -> SResult<Vec<RaftResp>>
	where
		I: IntoIterator<Item = Entry<TypeConfig>>,
	{
		let mut out = Vec::new();
		for entry in entries {
			let log_id = entry.log_id;
			tr(&format!("apply: entry {:?} payload", log_id));
			let r: RaftResp = match entry.payload {
				EntryPayload::Blank => Ok(()),
				EntryPayload::Membership(ref m) => {
					let mut g = self.meta.lock().unwrap();
					g.last_membership = StoredMembership::new(Some(log_id), m.clone());
					Ok(())
				}
				EntryPayload::Normal(RaftEntryData { cmd, ctx }) => {
					tr(&format!("apply: Normal cmd={} locking fsm", cmd.name()));
					let res = {
						let mut f = self.fsm.lock().unwrap();
						tr("apply: fsm locked, calling f.apply");
						let r = f.apply(&ctx, &cmd);
						tr("apply: f.apply done, releasing fsm");
						r
					};
					tr("apply: fsm released");
					let mut g = self.meta.lock().unwrap();
					g.last_applied = Some(log_id);
					res
				}
			};
			{
				let mut g = self.meta.lock().unwrap();
				g.last_applied = Some(log_id);
			}
			out.push(r);
		}
		tr("apply: returning");
		Ok(out)
	}

	async fn get_snapshot_builder(&mut self) -> Self::SnapshotBuilder {
		SmSnapBuilder {
			fsm: self.fsm.clone(),
			meta: self.meta.clone(),
		}
	}

	async fn begin_receiving_snapshot(&mut self) -> SResult<Box<Cursor<Vec<u8>>>> {
		Ok(Box::new(Cursor::new(Vec::new())))
	}

	async fn install_snapshot(
		&mut self,
		meta: &openraft::SnapshotMeta<u64, BasicNode>,
		snapshot: Box<Cursor<Vec<u8>>>,
	) -> SResult<()> {
		let bytes = snapshot.into_inner();
		let data: SmSnapData = bincode::deserialize(&bytes).map_err(|e| berr(e, false))?;
		{
			let mut f = self.fsm.lock().unwrap();
			*f = Fsm::restore(data.fsm);
		}
		{
			let mut g = self.meta.lock().unwrap();
			g.last_applied = data.last_applied;
			g.last_membership = data.last_membership;
		}
		std::fs::write(&self.snapshot_path, &bytes).map_err(|e| ierr(e, true))?;
		*self.current.lock().unwrap() = Some(Snapshot {
			meta: meta.clone(),
			snapshot: Box::new(Cursor::new(bytes)),
		});
		Ok(())
	}

	async fn get_current_snapshot(&mut self) -> SResult<Option<Snapshot<TypeConfig>>> {
		Ok(self.current.lock().unwrap().clone())
	}
}
