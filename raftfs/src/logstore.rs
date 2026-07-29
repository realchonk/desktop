use std::collections::BTreeMap;
use std::fmt::Debug;
use std::ops::RangeBounds;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use openraft::storage::{LogFlushed, LogState, RaftLogReader, RaftLogStorage};
use openraft::{Entry, ErrorSubject, ErrorVerb, LogId, OptionalSend, StorageError, StorageIOError, Vote};

use crate::raft::TypeConfig;

type SResult<T> = Result<T, StorageError<crate::raft::NodeId>>;

#[derive(Default, serde::Serialize, serde::Deserialize, Clone)]
struct Inner {
	entries: BTreeMap<u64, Entry<TypeConfig>>,
	last_purged: Option<LogId<crate::raft::NodeId>>,
	vote: Option<Vote<crate::raft::NodeId>>,
	committed: Option<LogId<crate::raft::NodeId>>,
}

#[derive(Clone)]
pub struct LogStore {
	path: PathBuf,
	inner: Arc<Mutex<Inner>>,
}

fn serr(e: impl std::error::Error + 'static, write: bool) -> StorageError<crate::raft::NodeId> {
	StorageError::IO {
		source: StorageIOError::new(
			ErrorSubject::Store,
			if write { ErrorVerb::Write } else { ErrorVerb::Read },
			&e,
		),
	}
}

fn berr(e: bincode::Error, write: bool) -> StorageError<crate::raft::NodeId> {
	StorageError::IO {
		source: StorageIOError::new(
			ErrorSubject::Store,
			if write { ErrorVerb::Write } else { ErrorVerb::Read },
			&e,
		),
	}
}

fn write_atomic_fsync(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
	let tmp = path.with_extension("tmp");
	{
		let mut f = std::fs::File::create(&tmp)?;
		use std::io::Write;
		f.write_all(bytes)?;
		f.sync_all()?;
	}
	std::fs::rename(&tmp, path)?;
	if let Some(parent) = path.parent() {
		if let Ok(d) = std::fs::File::open(parent) {
			let _ = d.sync_all();
		}
	}
	Ok(())
}

impl LogStore {
	pub fn open(dir: &Path) -> SResult<Self> {
		std::fs::create_dir_all(dir).map_err(|e| serr(e, false))?;
		let path = dir.join("log.bin");
		let inner = match std::fs::read(&path) {
			Ok(bytes) if !bytes.is_empty() => {
				bincode::deserialize::<Inner>(&bytes).map_err(|e| berr(e, false))?
			}
			_ => Inner::default(),
		};
		Ok(Self {
			path,
			inner: Arc::new(Mutex::new(inner)),
		})
	}

	fn persist(&self) -> SResult<()> {
		let bytes = {
			let g = self.inner.lock().unwrap();
			bincode::serialize(&*g).map_err(|e| berr(e, true))?
		};
		write_atomic_fsync(&self.path, &bytes).map_err(|e| serr(e, true))?;
		Ok(())
	}
}

impl RaftLogReader<TypeConfig> for LogStore {
	async fn try_get_log_entries<RB: RangeBounds<u64> + Clone + Debug + OptionalSend>(
		&mut self,
		range: RB,
	) -> SResult<Vec<Entry<TypeConfig>>> {
		let g = self.inner.lock().unwrap();
		let mut out = Vec::new();
		for (_idx, entry) in g.entries.range((range.start_bound().cloned(), range.end_bound().cloned())) {
			out.push(entry.clone());
		}
		Ok(out)
	}
}

impl RaftLogStorage<TypeConfig> for LogStore {
	type LogReader = LogStore;

	async fn get_log_state(&mut self) -> SResult<LogState<TypeConfig>> {
		let g = self.inner.lock().unwrap();
		let last_log_id = g.entries.last_key_value().map(|(_, e)| e.log_id);
		let last_purged_log_id = g.last_purged;
		let last_log_id = last_log_id.or(last_purged_log_id);
		Ok(LogState {
			last_purged_log_id,
			last_log_id,
		})
	}

	async fn get_log_reader(&mut self) -> Self::LogReader {
		self.clone()
	}

	async fn save_vote(&mut self, vote: &Vote<crate::raft::NodeId>) -> SResult<()> {
		{
			let mut g = self.inner.lock().unwrap();
			g.vote = Some(*vote);
		}
		self.persist()
	}

	async fn read_vote(&mut self) -> SResult<Option<Vote<crate::raft::NodeId>>> {
		Ok(self.inner.lock().unwrap().vote)
	}

	async fn save_committed(&mut self, committed: Option<LogId<crate::raft::NodeId>>) -> SResult<()> {
		{
			let mut g = self.inner.lock().unwrap();
			g.committed = committed;
		}
		self.persist()
	}

	async fn read_committed(&mut self) -> SResult<Option<LogId<crate::raft::NodeId>>> {
		Ok(self.inner.lock().unwrap().committed)
	}

	async fn append<I>(&mut self, entries: I, callback: LogFlushed<TypeConfig>) -> SResult<()>
	where
		I: IntoIterator<Item = Entry<TypeConfig>> + OptionalSend,
	{
		{
			let mut g = self.inner.lock().unwrap();
			for entry in entries {
				g.entries.insert(entry.log_id.index, entry);
			}
		}
		match self.persist() {
			Ok(()) => {
				callback.log_io_completed(Ok(()));
				Ok(())
			}
			Err(e) => {
				callback.log_io_completed(Err(std::io::Error::other("persist failed")));
				Err(e)
			}
		}
	}

	async fn truncate(&mut self, log_id: LogId<crate::raft::NodeId>) -> SResult<()> {
		{
			let mut g = self.inner.lock().unwrap();
			let keys: Vec<u64> = g.entries.range(log_id.index..).map(|(k, _)| *k).collect();
			for k in keys {
				g.entries.remove(&k);
			}
		}
		self.persist()
	}

	async fn purge(&mut self, log_id: LogId<crate::raft::NodeId>) -> SResult<()> {
		{
			let mut g = self.inner.lock().unwrap();
			let keys: Vec<u64> = g.entries.range(..=log_id.index).map(|(k, _)| *k).collect();
			for k in keys {
				g.entries.remove(&k);
			}
			g.last_purged = Some(log_id);
		}
		self.persist()
	}
}
