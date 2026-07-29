# raftfs

FUSE-based distributed filesystem with Raft consensus.

Writes go through Raft so every node has an identical, durable copy of the
filesystem.  Nodes can join and leave online.  An optional encryption layer
keeps file data opaque to storage nodes.

## Build

```
mk raftfs            # optimized release
mk DEBUG=1 raftfs    # debug build (-g -O0, cargo dev profile)
sudo mk raftfs/install
```

Dependencies: Rust (cargo), `libfuse3` (development headers + `fusermount3`),
`/dev/fuse` available, and `pkg-config`.

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
**follower**.  The mount is usable on both (writes succeed on the leader;
reads work everywhere).

### 3. Verify

```
echo hello > /mnt/raftfs/test.txt   # on the leader's mount
cat /mnt/raftfs/test.txt            # on either mount → "hello"
```

## Adding a third node (learner → voter)

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

After promotion the cluster has 3 voters (quorum 2; tolerates 1 down).

## Encryption

File data can be encrypted per-directory.  Encrypted data is ciphertext in the
FSM and block store — storage nodes never see plaintext.

Encrypt a directory by writing a passphrase to its virtual `lock` file:

```
mkdir /mnt/raftfs/secret
echo mypass > /mnt/raftfs/secret/lock
```

The directory is now encrypted and unlocked.  Write files normally — data is
encrypted at rest.

After a remount (or on a node that hasn't unlocked it), the directory is
**locked** and shows only one file:

```
ls /mnt/raftfs/secret          # → unlock
cat /mnt/raftfs/secret/data    # → No such file or directory (hidden)
```

Unlock by writing the passphrase to `unlock`:

```
echo mypass > /mnt/raftfs/secret/unlock
ls /mnt/raftfs/secret          # → data  (contents revealed)
cat /mnt/raftfs/secret/data    # → (plaintext, decrypted on the fly)
```

## Operational notes

- **Quorum**: both voters must be online to write (2-of-2).  If one server is
  down, the cluster is read-only.  Adding a third voter makes it tolerate one
  failure (2-of-3).
- **Persistence**: data survives a full cluster restart — the FSM is rebuilt
  from the on-disk Raft log, and blocks are stored on disk.
- **Leadership**: writes must reach the leader.  If you mount a follower,
  reads work but writes return an error.  To find the leader, check stderr
  (the metrics line prints `state=Leader` / `state=Follower`).
- **Unmount**: `fusermount3 -u /mnt/raftfs`.

## Data directory layout

```
<data-dir>/
  node.conf          node configuration (bincode)
  raft/
    log.bin          Raft log (entries + vote + committed)
    snapshot.bin     FSM snapshot (when snapshotting is enabled)
  blocks/            content-addressed file data (blake3)
    ab/cd/<hex>      block files (ciphertext if encrypted)
```

## Limitations

- Writes must target the leader node (mount the leader to write; reads work
  anywhere).
- Filenames are not encrypted (file *data* is).
- Snapshot policy is `Never` (the log grows; compaction is planned).
- POSIX permissions are not enforced.
- The write path is O(filesize) per write.
- Server-only build (no FUSE): `mk raftfs/clean && mk CARGO_BUILD_FLAGS='--no-default-features --features block-quorum' raftfs`.
