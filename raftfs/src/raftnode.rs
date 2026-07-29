use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use anyhow::{anyhow, Result};
use openraft::{BasicNode, Config, SnapshotPolicy};
use serde::{Deserialize, Serialize};

use crate::block::DiskStore;
use crate::fsm::Fsm;
use crate::logstore::LogStore;
use crate::net::{spawn_server, NetFactory};
use crate::raft::Raft as RaftHandle;
use crate::smstore::SmStore;

#[derive(Serialize, Deserialize, Clone)]
pub struct NodeConfig {
	pub id: u64,
	pub addr: String,
	pub data_dir: PathBuf,
	/// Initial voter set, only set on the bootstrap node(s).
	pub bootstrap: Option<BTreeMap<u64, BasicNode>>,
	pub preferred_leader: u64,
}

impl NodeConfig {
	pub fn conf_path(datadir: &Path) -> PathBuf {
		datadir.join("node.conf")
	}
	pub fn load(path: &Path) -> Result<Self> {
		let bytes = std::fs::read(path)?;
		Ok(bincode::deserialize(&bytes)?)
	}
	pub fn save(&self, path: &Path) -> Result<()> {
		std::fs::write(path, bincode::serialize(self)?)?;
		Ok(())
	}
}

/// Parse "id=addr,id=addr,..." into a member map.
pub fn parse_members(spec: &str) -> Result<BTreeMap<u64, BasicNode>> {
	let mut m = BTreeMap::new();
	for part in spec.split(',') {
		let part = part.trim();
		if part.is_empty() {
			continue;
		}
		let (id_s, addr) = part
			.split_once('=')
			.ok_or_else(|| anyhow!("bootstrap entry `{part}` must be id=addr"))?;
		let id: u64 = id_s.trim().parse()?;
		m.insert(id, BasicNode { addr: addr.trim().to_string() });
	}
	Ok(m)
}

pub async fn setup(cfg: &NodeConfig) -> Result<(RaftHandle, Arc<Mutex<Fsm>>, Arc<DiskStore>)> {
	let raft_dir = cfg.data_dir.join("raft");
	let blocks_dir = cfg.data_dir.join("blocks");
	std::fs::create_dir_all(&raft_dir)?;
	std::fs::create_dir_all(&blocks_dir)?;

	let uid = unsafe { libc::geteuid() };
	let gid = unsafe { libc::getegid() };
	let fsm = Arc::new(Mutex::new(Fsm::new(uid, gid)));
	let disk = Arc::new(DiskStore::new(&blocks_dir)?);

	let log_store = LogStore::open(&raft_dir).map_err(|e| anyhow!("open log store: {e:?}"))?;
	let sm_store = SmStore::new(fsm.clone(), &raft_dir);

	let config = Arc::new(
		Config {
			cluster_name: "raftfs".to_string(),
			heartbeat_interval: 500,
			election_timeout_min: 1500,
			election_timeout_max: 3000,
			snapshot_policy: SnapshotPolicy::Never,
			..Default::default()
		}
		.validate()
		.map_err(|e| anyhow!("config: {e}"))?,
	);

	let raft = RaftHandle::new(cfg.id, config, NetFactory, log_store, sm_store)
		.await
		.map_err(|e| anyhow!("raft new: {e:?}"))?;

	if let Some(members) = &cfg.bootstrap {
		match raft.is_initialized().await {
			Ok(false) => {
				raft.initialize(members.clone())
					.await
					.map_err(|e| anyhow!("initialize: {e:?}"))?;
			}
			Ok(true) => {}
			Err(e) => return Err(anyhow!("is_initialized: {e:?}")),
		}
	}

	spawn_server(raft.clone(), disk.clone(), cfg.addr.clone()).await?;

	Ok((raft, fsm, disk))
}

pub async fn metrics_loop(raft: RaftHandle, id: u64) -> Result<()> {
	let mut rx = raft.metrics();
	loop {
		tokio::select! {
			_ = tokio::signal::ctrl_c() => {
				eprintln!("raftfs: shutting down");
				break;
			}
			_ = tokio::time::sleep(Duration::from_secs(3)) => {
				let m = rx.borrow();
				eprintln!(
					"raftfs: id={} state={:?} term={} leader={:?} last_applied={:?} last_log={:?}",
					id, m.state, m.current_term, m.current_leader, m.last_applied, m.last_log_index
				);
			}
		}
	}
	Ok(())
}
