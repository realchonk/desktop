#![cfg_attr(not(feature = "fs"), allow(dead_code))]

mod block;
#[cfg(feature = "crypto")]
mod crypto;
mod cmd;
mod fsm;
mod logstore;
mod model;
mod net;
mod raft;
mod raftnode;
mod smstore;

#[cfg(feature = "fs")]
mod fuse;

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "raftfs", version, about = "FUSE + Raft distributed filesystem")]
struct Cli {
	#[command(subcommand)]
	cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
	/// Initialize a node data directory.
	Format {
		datadir: PathBuf,
		#[arg(long)]
		id: crate::raft::NodeId,
		#[arg(long)]
		addr: String,
		#[arg(long)]
		bootstrap: Option<String>,
	},
	/// Start a node (Raft + network). Optionally mount a FUSE frontend.
	Start {
		datadir: PathBuf,
		#[arg(long)]
		mount: Option<PathBuf>,
	},
	/// Add this node (already `start`ed) to a running cluster as a learner.
	Join {
		datadir: PathBuf,
		#[arg(long)]
		leader: String,
	},
	/// Promote a learner to a voting member.
	Promote {
		#[arg(long)]
		leader: String,
		#[arg(long)]
		id: crate::raft::NodeId,
	},
	/// Show cluster status (leader, voters, learners) by querying a running node.
	Status {
		datadir: PathBuf,
	},
	/// Trigger a leader election on this node (nudges it to become leader).
	Elect {
		datadir: PathBuf,
	},
}

fn main() -> ExitCode {
	let _ = tracing_subscriber::fmt()
		.with_env_filter(
			tracing_subscriber::EnvFilter::try_from_default_env()
				.unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("warn")),
		)
		.with_writer(std::io::stderr)
		.try_init();

	let cli = Cli::parse();
	match cli.cmd {
		Cmd::Format {
			datadir,
			id,
			addr,
			bootstrap,
		} => match format_cmd(&datadir, id, &addr, bootstrap.as_deref()) {
			Ok(()) => ExitCode::SUCCESS,
			Err(e) => {
				eprintln!("raftfs: format: {e}");
				ExitCode::FAILURE
			}
		},
		Cmd::Start { datadir, mount } => match start_cmd(&datadir, mount) {
			Ok(()) => ExitCode::SUCCESS,
			Err(e) => {
				eprintln!("raftfs: start: {e}");
				ExitCode::FAILURE
			}
		},
		Cmd::Join { datadir, leader } => match join_cmd(&datadir, &leader) {
			Ok(()) => ExitCode::SUCCESS,
			Err(e) => {
				eprintln!("raftfs: join: {e}");
				ExitCode::FAILURE
			}
		},
		Cmd::Promote { leader, id } => match promote_cmd(&leader, id) {
			Ok(()) => ExitCode::SUCCESS,
			Err(e) => {
				eprintln!("raftfs: promote: {e}");
				ExitCode::FAILURE
			}
		},
		Cmd::Status { datadir } => match status_cmd(&datadir) {
			Ok(()) => ExitCode::SUCCESS,
			Err(e) => {
				eprintln!("raftfs: status: {e}");
				ExitCode::FAILURE
			}
		},
		Cmd::Elect { datadir } => match elect_cmd(&datadir) {
			Ok(()) => ExitCode::SUCCESS,
			Err(e) => {
				eprintln!("raftfs: elect: {e}");
				ExitCode::FAILURE
			}
		},
	}
}

fn format_cmd(datadir: &Path, id: crate::raft::NodeId, addr: &str, bootstrap: Option<&str>) -> anyhow::Result<()> {
	std::fs::create_dir_all(datadir)?;
	std::fs::create_dir_all(datadir.join("raft"))?;
	std::fs::create_dir_all(datadir.join("blocks"))?;
	let members = match bootstrap {
		Some(s) => Some(raftnode::parse_members(s)?),
		None => None,
	};
	let preferred_leader = members
		.as_ref()
		.and_then(|m| m.keys().copied().min())
		.unwrap_or(id);
	let cfg = raftnode::NodeConfig {
		id,
		addr: addr.to_string(),
		data_dir: datadir.to_path_buf(),
		bootstrap: members,
		preferred_leader,
	};
	cfg.save(&raftnode::NodeConfig::conf_path(datadir))?;
	eprintln!("raftfs: formatted node {id} @ {addr} (dir {datadir:?})");
	Ok(())
}

fn start_cmd(datadir: &Path, mount: Option<PathBuf>) -> anyhow::Result<()> {
	let rt = tokio::runtime::Builder::new_multi_thread()
		.enable_all()
		.build()?;
	let handle = rt.handle().clone();
	let cfg = {
		let cfg = raftnode::NodeConfig::load(&raftnode::NodeConfig::conf_path(datadir))?;
		cfg
	};
	let (raft, fsm, disk) = rt.block_on(raftnode::setup(&cfg))?;
	eprintln!("raftfs: node {} started on {}", cfg.id, cfg.addr);

	#[cfg(feature = "fs")]
	{
		if let Some(mp) = mount {
			let raft2 = raft.clone();
			let id = cfg.id;
			rt.spawn(async move {
				let _ = raftnode::metrics_loop(raft2, id).await;
			});
			eprintln!("raftfs: mounting FUSE at {} (main thread)", mp.display());
			fuse::mount(&mp, handle, raft, fsm, disk, cfg.id)?;
			return Ok(());
		}
	}
	#[cfg(not(feature = "fs"))]
	let _ = mount;

	// No FUSE mount: just run the node until Ctrl-C.
	rt.block_on(raftnode::metrics_loop(raft, cfg.id))?;
	Ok(())
}

fn join_cmd(datadir: &Path, leader: &str) -> anyhow::Result<()> {
	let rt = tokio::runtime::Builder::new_multi_thread()
		.enable_all()
		.build()?;
	rt.block_on(async move {
		let cfg = raftnode::NodeConfig::load(&raftnode::NodeConfig::conf_path(datadir))?;
		crate::net::mgmt_join(leader, cfg.id, cfg.addr.clone())
			.await
			.map_err(|e| anyhow::anyhow!("join RPC: {e}"))?;
		eprintln!("raftfs: node {} joined cluster via leader {}", cfg.id, leader);
		Ok::<(), anyhow::Error>(())
	})?;
	Ok(())
}

fn promote_cmd(leader: &str, id: crate::raft::NodeId) -> anyhow::Result<()> {
	let rt = tokio::runtime::Builder::new_multi_thread()
		.enable_all()
		.build()?;
	rt.block_on(async move {
		crate::net::mgmt_promote(leader, id)
			.await
			.map_err(|e| anyhow::anyhow!("promote RPC: {e}"))?;
		eprintln!("raftfs: promoted node {id} to voter");
		Ok::<(), anyhow::Error>(())
	})?;
	Ok(())
}

fn status_cmd(datadir: &Path) -> anyhow::Result<()> {
	let cfg = raftnode::NodeConfig::load(&raftnode::NodeConfig::conf_path(datadir))?;
	let addr = cfg.addr.clone();
	let rt = tokio::runtime::Builder::new_multi_thread()
		.enable_all()
		.build()?;
	let status = rt.block_on(async move {
		crate::net::status_query(&addr)
			.await
			.map_err(|e| anyhow::anyhow!("query {addr}: {e}"))
	})?;
	println!("node:    {} ({})", status.id, cfg.addr);
	println!("state:   {}", status.state);
	println!("term:    {}", status.term);
	match &status.leader {
		Some(l) => println!("leader:  {l}"),
		None => println!("leader:  (none)"),
	}
	match status.last_applied_index {
		Some(i) => println!("applied: log index {i}"),
		None => println!("applied: (none)"),
	}
	if !status.voters.is_empty() {
		println!("voters:");
		for v in &status.voters {
			println!("  {} ({})", v.id, v.addr);
		}
	}
	if !status.learners.is_empty() {
		println!("learners:");
		for l in &status.learners {
			println!("  {} ({})", l.id, l.addr);
		}
	}
	Ok(())
}

fn elect_cmd(datadir: &Path) -> anyhow::Result<()> {
	let cfg = raftnode::NodeConfig::load(&raftnode::NodeConfig::conf_path(datadir))?;
	let addr = cfg.addr.clone();
	let rt = tokio::runtime::Builder::new_multi_thread()
		.enable_all()
		.build()?;
	rt.block_on(async move {
		crate::net::elect_node(&addr)
			.await
			.map_err(|e| anyhow::anyhow!("elect RPC to {addr}: {e}"))?;
		eprintln!("raftfs: election triggered on node {} ({})", cfg.id, cfg.addr);
		// Give the election a moment to settle, then report.
		tokio::time::sleep(std::time::Duration::from_secs(3)).await;
		match crate::net::status_query(&addr).await {
			Ok(st) => eprintln!(
				"raftfs: node {} state={} leader={}",
				st.id, st.state,
				st.leader.as_deref().unwrap_or("(none)")
			),
			Err(e) => eprintln!("raftfs: could not query status after elect: {e}"),
		}
		Ok::<(), anyhow::Error>(())
	})?;
	Ok(())
}
