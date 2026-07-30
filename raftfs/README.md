# raftfs

FUSE-based distributed filesystem with Raft consensus and per-directory
encryption.

Every write is replicated through Raft so all nodes hold an identical, durable
copy of the filesystem tree.  File data is stored in content-addressed blocks
replicated out-of-band (not through the Raft log).  Nodes can join and leave
online.  An optional per-directory encryption layer keeps file data opaque to
storage nodes that don't hold the key.

## Build

```
mk -o obj raftfs            # optimized release
mk -o obj DEBUG=1 raftfs    # debug build (cargo dev profile, -g -O0)
sudo mk -o obj raftfs/install
```

Dependencies: Rust (cargo), `libfuse3` (development headers + `fusermount3`),
`/dev/fuse`, and `pkg-config`.

## How it works

### Architecture

Each node is a single process with three layers:

1. **FUSE frontend** (`fuse.rs`) — implements the POSIX filesystem interface.
   Translates VFS operations (create, read, write, readdir, …) into commands
   that are committed through Raft.

2. **Raft consensus** (`raft.rs`, `logstore.rs`, `smstore.rs`, `net.rs`) —
   [openraft](https://github.com/datafuselabs/openraft) 0.9 with a custom
   pure-Rust log store (no RocksDB dependency) and a TCP-based RPC layer.

3. **Block store** (`block.rs`) — content-addressed file-data storage
   (`blocks/<b0>/<b1>/<blake3-hex>`) with crash-safe writes.

### The write path

When you write to a file:

1. If the write fits within 4 KiB and the file is already inline, it goes
   directly into the Raft log as a `WriteInline` command (single round-trip).

2. Otherwise, the FUSE frontend splits the write into **block-aligned chunks**
   (4 KiB each), reads only the blocks that partially overlap the write
   (read-modify-write), stores new/changed blocks in the local `DiskStore`,
   pushes them to peer voters over a **single batched TCP connection**, then
   commits a `WriteMeta` command (block-ID list + file size) through Raft.

3. Raft replicates the `WriteMeta` to all voters.  Once committed, the leader
   applies it to the **FSM** (finite state machine) — the in-memory filesystem
   tree (inodes, directory entries, extent maps).

Key optimization: blocks are content-addressed (blake3 hash).  If a block's
content didn't change, its hash matches and it is neither re-stored nor
re-replicated.

### The read path

Reads are served from the local FSM + block store (stale reads — no leader
contact needed).  If a block is missing locally (e.g. the node was offline when
it was written), it is lazily fetched from a peer on demand.

### Persistence

- **Metadata** (inodes, directory entries, extent maps, timestamps) lives in the
  Raft log (`raft/log.bin`) and is rebuilt by replaying the log on restart.
- **File data** lives in content-addressed block files under `blocks/` and
  survives restart directly.

A full cluster restart rebuilds the FSM from the on-disk Raft log; blocks are
already on disk.

### Topology

The cluster has two kinds of members:

- **Voters** (servers): participate in Raft consensus.  A majority must be
  online to elect a leader and accept writes.  Two voters = both must be up to
  write.  Three voters = tolerates one failure.
- **Learners** (laptops, desktops): receive replicated data and can serve reads,
  but don't count toward the write quorum.  Ideal for devices that are only
  sporadically online.

### Write forwarding

If you mount a follower or learner, writes are automatically forwarded to the
leader over TCP.  The FUSE frontend waits for the forwarded entry to replicate
back to the local FSM before returning the result.

## Encryption

File data can be encrypted **per-directory**.  Encrypted data is ciphertext in
the FSM and block store — storage nodes never see plaintext.  Keys are held only
in the mounting process's memory; the Raft-Replicated marker file stores the
key wrapped by a passphrase (Argon2id + XChaCha20-Poly1305).

### Encrypt a directory

Write a passphrase to the virtual `.raft-encryptdir` file:

```
mkdir /mnt/raftfs/secret
echo mypass > /mnt/raftfs/secret/.raft-encryptdir
```

The directory is now encrypted and unlocked.  Write files normally — data is
encrypted at rest.

### Lock / unlock

After a remount (or on a node that hasn't unlocked it), the directory is
**locked** — it shows only an `unlock` file; all real contents are hidden:

```
ls /mnt/raftfs/secret          # → unlock
cat /mnt/raftfs/secret/data    # → No such file or directory (hidden)
```

Unlock by writing the passphrase:

```
echo mypass > /mnt/raftfs/secret/unlock
ls /mnt/raftfs/secret          # → data  (contents revealed)
cat /mnt/raftfs/secret/data    # → (plaintext, decrypted on the fly)
```

Re-lock an unlocked directory:

```
chmod 0 /mnt/raftfs/secret
```

Permanently delete an encrypted directory (including hidden contents):

```
rmdir /mnt/raftfs/secret
```

### Cryptographic details

| Component | Algorithm |
|---|---|
| Block AEAD | XChaCha20-Poly1305 (24-byte random nonce prepended) |
| Per-file key | HKDF-SHA256(directory key, info = inode number) |
| Directory key | 32 random bytes, wrapped by Argon2id(passphrase) → KEK → XChaCha20-Poly1305 |
| Block addressing | blake3(content) — content-addressed, immutable, dedup |
| Marker file | `.raft-encryptdir` — stores the wrapped key; hidden from listings |

Encrypted files always use the block-backed extents path (never inline), so the
FSM tracks plaintext size while blocks hold ciphertext.  A node without the key
sees only ciphertext garbage when reading block files.

## Quick start — two-node cluster

Pick two machines (the "servers").  Each needs a persistent data directory and
a reachable TCP port.

### 1. Format both nodes

On **server A** (replace `A_IP` and `B_IP` with real addresses):

```
raftfs format /data/raftfs --id serverA --addr A_IP:7000 \
    --bootstrap "serverA=A_IP:7000,serverB=B_IP:7000"
```

On **server B**:

```
raftfs format /data/raftfs --id serverB --addr B_IP:7000 \
    --bootstrap "serverA=A_IP:7000,serverB=B_IP:7000"
```

### 2. Start both nodes

On each server (run in a terminal, tmux, or as a service):

```
raftfs start /data/raftfs --mount /mnt/raftfs
```

Wait a few seconds for election.  One node becomes **leader**, the other
**follower**.  The mount is usable on both (writes are forwarded to the leader
automatically; reads work everywhere).

### 3. Verify

```
echo hello > /mnt/raftfs/test.txt   # on any mount
cat /mnt/raftfs/test.txt            # on any mount → "hello"
```

Check cluster status:

```
raftfs status /data/raftfs
```

## Adding nodes

A new node joins as a non-voting **learner** — it receives replicated data and
can serve reads, but doesn't count toward the write quorum.

On **server C**:

```
raftfs format /data/raftfs --id serverC --addr C_IP:7000
raftfs start /data/raftfs --mount /mnt/raftfs &
raftfs join /data/raftfs --leader A_IP:7000     # add as learner
```

When ready to make it a voter:

```
raftfs promote --leader A_IP:7000 --id serverC  # learner → voter
```

After promotion the cluster has 3 voters (quorum 2; tolerates 1 failure).

To change which node is leader:

```
raftfs elect /data/raftfs    # triggers a new election on this node
```

## CLI reference

| Command | Description |
|---|---|
| `format <dir> --id <name> --addr <host:port> [--bootstrap <list>]` | Initialize a node data directory |
| `start <dir> [--mount <mp>]` | Start the Raft engine + network server (+ optional FUSE mount) |
| `join <dir> --leader <addr>` | Add this node to a running cluster as a learner |
| `promote --leader <addr> --id <name>` | Promote a learner to a voting member |
| `status <dir>` | Show cluster state (leader, voters, learners) |
| `elect <dir>` | Trigger a leader election on this node |

## Operational notes

- **Quorum**: both voters must be online to write (2-of-2).  If one server is
  down, the cluster is read-only.  Adding a third voter makes it tolerate one
  failure (2-of-3).
- **Persistence**: data survives a full cluster restart — the FSM is rebuilt
  from the on-disk Raft log, and blocks are stored on disk.
- **Unmount**: `fusermount3 -u /mnt/raftfs`.
- **Debug builds**: `mk -o obj DEBUG=1 raftfs`.
- **Tracing**: set `RUST_LOG=openraft=info` for Raft diagnostics, or
  `RAFTFS_TRACE=1` for FUSE op-level tracing.

## Data directory layout

```
<data-dir>/
  node.conf            node configuration (bincode)
  raft/
    log.bin            Raft log (entries + vote + committed)
    snapshot.bin       FSM snapshot (when snapshotting is enabled)
  blocks/              content-addressed file data (blake3)
    ab/cd/<hex>        block files (ciphertext if encrypted)
```

## Limitations

- Filenames are not encrypted (file *data* is).
- Snapshot policy is `Never` (the Raft log grows; compaction is planned).
- POSIX permissions are not enforced.
- Server-only build (no FUSE): `mk -o obj raftfs/clean && mk -o obj CARGO_BUILD_FLAGS='--no-default-features --features block-quorum' raftfs`.
