use serde::{Deserialize, Serialize};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

use openraft::error::{InstallSnapshotError, RPCError, RaftError, Unreachable};
use openraft::network::{RaftNetwork, RaftNetworkFactory, RPCOption};
use openraft::raft::{
	AppendEntriesRequest, AppendEntriesResponse, InstallSnapshotRequest, InstallSnapshotResponse,
	VoteRequest, VoteResponse,
};
use openraft::{BasicNode, ChangeMembers};
use std::collections::BTreeSet;

use crate::block::{BlockStore, DiskStore};
use crate::model::Hash;
use crate::raft::{Raft as RaftHandle, RaftEntryData, RaftResp, TypeConfig};

fn ioerr(e: impl std::error::Error + Send + Sync + 'static) -> std::io::Error {
	std::io::Error::other(e)
}

async fn write_frame<W: AsyncWriteExt + Unpin>(w: &mut W, bytes: &[u8]) -> std::io::Result<()> {
	let len = bytes.len() as u32;
	w.write_all(&len.to_be_bytes()).await?;
	w.write_all(bytes).await?;
	w.flush().await?;
	Ok(())
}

async fn read_frame<R: AsyncReadExt + Unpin>(r: &mut R) -> std::io::Result<Vec<u8>> {
	let mut lenbuf = [0u8; 4];
	r.read_exact(&mut lenbuf).await?;
	let len = u32::from_be_bytes(lenbuf) as usize;
	let mut buf = vec![0u8; len];
	r.read_exact(&mut buf).await?;
	Ok(buf)
}

#[derive(Serialize, Deserialize)]
enum Wire {
	Append(AppendEntriesRequest<TypeConfig>),
	Vote(VoteRequest<crate::raft::NodeId>),
	Snap(InstallSnapshotRequest<TypeConfig>),
	BlockGet(Hash),
	BlockPut(Vec<u8>),
	Forward(RaftEntryData),
	Join { id: crate::raft::NodeId, addr: String },
	Promote { id: crate::raft::NodeId },
	Status,
}

#[derive(Serialize, Deserialize)]
pub struct NodeInfo {
	pub id: String,
	pub addr: String,
}

#[derive(Serialize, Deserialize)]
pub struct ClusterStatus {
	pub id: String,
	pub state: String,
	pub term: u64,
	pub leader: Option<String>,
	pub last_applied_index: Option<u64>,
	pub voters: Vec<NodeInfo>,
	pub learners: Vec<NodeInfo>,
}

async fn rpc_raw(addr: &str, wire: &Wire) -> std::io::Result<Vec<u8>> {
	let mut s = TcpStream::connect(addr).await?;
	let bytes = bincode::serialize(wire).map_err(ioerr)?;
	write_frame(&mut s, &bytes).await?;
	read_frame(&mut s).await
}

pub struct NetFactory;

impl RaftNetworkFactory<TypeConfig> for NetFactory {
	type Network = NetClient;

	async fn new_client(&mut self, _target: crate::raft::NodeId, node: &BasicNode) -> NetClient {
		NetClient {
			addr: node.addr.clone(),
		}
	}
}

pub struct NetClient {
	addr: String,
}

impl NetClient {
	async fn call<R: serde::de::DeserializeOwned>(
		&self,
		wire: Wire,
	) -> Result<R, RPCError<crate::raft::NodeId, BasicNode, RaftError<crate::raft::NodeId>>> {
		let bytes = rpc_raw(&self.addr, &wire)
			.await
			.map_err(|e| RPCError::Unreachable(Unreachable::new(&e)))?;
		bincode::deserialize::<R>(&bytes)
			.map_err(|e| RPCError::Unreachable(Unreachable::new(&e)))
	}
}

impl RaftNetwork<TypeConfig> for NetClient {
	async fn append_entries(
		&mut self,
		rpc: AppendEntriesRequest<TypeConfig>,
		_opt: RPCOption,
	) -> Result<AppendEntriesResponse<crate::raft::NodeId>, RPCError<crate::raft::NodeId, BasicNode, RaftError<crate::raft::NodeId>>> {
		self.call(Wire::Append(rpc)).await
	}

	async fn vote(
		&mut self,
		rpc: VoteRequest<crate::raft::NodeId>,
		_opt: RPCOption,
	) -> Result<VoteResponse<crate::raft::NodeId>, RPCError<crate::raft::NodeId, BasicNode, RaftError<crate::raft::NodeId>>> {
		self.call(Wire::Vote(rpc)).await
	}

	async fn install_snapshot(
		&mut self,
		rpc: InstallSnapshotRequest<TypeConfig>,
		_opt: RPCOption,
	) -> Result<
		InstallSnapshotResponse<crate::raft::NodeId>,
		RPCError<crate::raft::NodeId, BasicNode, RaftError<crate::raft::NodeId, InstallSnapshotError>>,
	> {
		let bytes = rpc_raw(&self.addr, &Wire::Snap(rpc))
			.await
			.map_err(|e| RPCError::Unreachable(Unreachable::new(&e)))?;
		bincode::deserialize::<InstallSnapshotResponse<crate::raft::NodeId>>(&bytes)
			.map_err(|e| RPCError::Unreachable(Unreachable::new(&e)))
	}
}

pub async fn spawn_server(raft: RaftHandle, disk: std::sync::Arc<DiskStore>, addr: String) -> std::io::Result<()> {
	let listener = TcpListener::bind(&addr).await?;
	tokio::spawn(async move {
		loop {
			let (mut stream, _peer) = match listener.accept().await {
				Ok(x) => x,
				Err(_) => continue,
			};
			let raft = raft.clone();
			let disk = disk.clone();
			tokio::spawn(async move {
				let _ = handle_conn(&mut stream, &raft, &disk).await;
			});
		}
	});
	Ok(())
}

async fn handle_conn(stream: &mut TcpStream, raft: &RaftHandle, disk: &DiskStore) -> std::io::Result<()> {
	let req = read_frame(stream).await?;
	let wire: Wire = bincode::deserialize(&req).map_err(ioerr)?;
	let resp_bytes: Option<Vec<u8>> = match wire {
		Wire::Append(r) => raft.append_entries(r).await.ok().map(|x| bincode::serialize(&x).unwrap()),
		Wire::Vote(r) => raft.vote(r).await.ok().map(|x| bincode::serialize(&x).unwrap()),
		Wire::Snap(r) => raft.install_snapshot(r).await.ok().map(|x| bincode::serialize(&x).unwrap()),
		Wire::BlockGet(h) => Some(bincode::serialize(&disk.get(h)).unwrap()),
		Wire::BlockPut(b) => {
			disk.put(&b);
			Some(bincode::serialize(&()).unwrap())
		}
		Wire::Forward(entry) => raft
			.client_write(entry)
			.await
			.ok()
			.map(|resp| bincode::serialize(&resp.data).unwrap()),
		Wire::Join { id, addr } => {
			let r = raft
				.add_learner(id, BasicNode { addr }, false)
				.await
				.map(|_| ())
				.map_err(|e| format!("{e:?}"));
			Some(bincode::serialize(&r).unwrap())
		}
		Wire::Promote { id } => {
			let r = raft
				.change_membership(ChangeMembers::AddVoterIds(BTreeSet::from([id])), true)
				.await
				.map(|_| ())
				.map_err(|e| format!("{e:?}"));
			Some(bincode::serialize(&r).unwrap())
		}
		Wire::Status => {
			let m = raft.metrics().borrow().clone();
			let status = build_status(&m);
			Some(bincode::serialize(&status).unwrap())
		}
	};
	if let Some(b) = resp_bytes {
		write_frame(stream, &b).await?;
	}
	Ok(())
}

/// Push a block to a peer; returns Ok once the peer ACKed it stored.
pub async fn block_put(addr: &str, bytes: Vec<u8>) -> std::io::Result<()> {
	let mut s = TcpStream::connect(addr).await?;
	let req = bincode::serialize(&Wire::BlockPut(bytes)).map_err(ioerr)?;
	write_frame(&mut s, &req).await?;
	let resp = read_frame(&mut s).await?;
	bincode::deserialize::<()>(&resp).map_err(ioerr)?;
	Ok(())
}

/// Fetch a block from a peer by hash; Ok(None) if the peer doesn't have it.
pub async fn block_get(addr: &str, hash: Hash) -> std::io::Result<Option<Vec<u8>>> {
	let mut s = TcpStream::connect(addr).await?;
	let req = bincode::serialize(&Wire::BlockGet(hash)).map_err(ioerr)?;
	write_frame(&mut s, &req).await?;
	let resp = read_frame(&mut s).await?;
	Ok(bincode::deserialize::<Option<Vec<u8>>>(&resp).map_err(ioerr)?)
}

/// Forward a command to the leader (used by a non-leader FUSE mount).
#[allow(dead_code)]
pub async fn forward_cmd(addr: &str, entry: RaftEntryData) -> std::io::Result<RaftResp> {
	let mut s = TcpStream::connect(addr).await?;
	let req = bincode::serialize(&Wire::Forward(entry)).map_err(ioerr)?;
	write_frame(&mut s, &req).await?;
	let resp = read_frame(&mut s).await?;
	Ok(bincode::deserialize::<RaftResp>(&resp).map_err(ioerr)?)
}

/// Ask the leader at `addr` to add (id, addr) as a learner.
pub async fn mgmt_join(addr: &str, id: crate::raft::NodeId, node_addr: String) -> std::io::Result<()> {
	let mut s = TcpStream::connect(addr).await?;
	let req = bincode::serialize(&Wire::Join { id, addr: node_addr }).map_err(ioerr)?;
	write_frame(&mut s, &req).await?;
	let resp = read_frame(&mut s).await?;
	bincode::deserialize::<Result<(), String>>(&resp)
		.map_err(ioerr)?
		.map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))
}

/// Ask the leader at `addr` to promote node `id` from learner to voter.
pub async fn mgmt_promote(addr: &str, id: crate::raft::NodeId) -> std::io::Result<()> {
	let mut s = TcpStream::connect(addr).await?;
	let req = bincode::serialize(&Wire::Promote { id }).map_err(ioerr)?;
	write_frame(&mut s, &req).await?;
	let resp = read_frame(&mut s).await?;
	bincode::deserialize::<Result<(), String>>(&resp)
		.map_err(ioerr)?
		.map_err(|e| std::io::Error::new(std::io::ErrorKind::Other, e))
}

/// Query a node for its cluster status.
pub async fn status_query(addr: &str) -> std::io::Result<ClusterStatus> {
	let mut s = TcpStream::connect(addr).await?;
	let req = bincode::serialize(&Wire::Status).map_err(ioerr)?;
	write_frame(&mut s, &req).await?;
	let resp = read_frame(&mut s).await?;
	bincode::deserialize::<ClusterStatus>(&resp).map_err(ioerr)
}

fn build_status(m: &openraft::RaftMetrics<crate::raft::NodeId, BasicNode>) -> ClusterStatus {
	let membership = m.membership_config.membership();
	let voter_ids: std::collections::BTreeSet<crate::raft::NodeId> =
		membership.get_joint_config().iter().flatten().copied().collect();
	let mut voters = Vec::new();
	let mut learners = Vec::new();
	for (id, node) in membership.nodes() {
		let info = NodeInfo { id: id.to_string(), addr: node.addr.clone() };
		if voter_ids.contains(id) {
			voters.push(info);
		} else {
			learners.push(info);
		}
	}
	ClusterStatus {
		id: m.id.to_string(),
		state: format!("{:?}", m.state),
		term: m.current_term,
		leader: m.current_leader.map(|l| l.to_string()),
		last_applied_index: m.last_applied.map(|l| l.index),
		voters,
		learners,
	}
}
