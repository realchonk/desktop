# AGENTS.md

Personal suckless-based desktop environment. C + shell, targeted at OpenBSD / FreeBSD / Linux.

## Version control: `got`, not git

This repo uses [Game of Trees (`got`)](https://gameoftrees.org/). The `.got/` dir is the worktree metadata; the bare repo lives at `~/src/repos/desktop.git`. Use `got log`, `got diff`, `got status`, `got commit`, `got revert`, etc. Git commands will not work.

## Build system: `mk`, not make

Build with **`mk`** (BSD/Plan9 make, `/usr/sbin/mk`). Source files are `Mkfile` (capital M), never `Makefile`. GNU `make` is deprecated and will misparse the templates.

- `mk` — build all subdirs listed in `.SUBDIRS` of the root `Mkfile`
- `mk install` — install binaries (root)
- `mk <dir>` — build one component, e.g. `mk dwm`, `mk bedstatus` (mk recurses into subdirs natively; no `-C` needed). Subdir targets work too: `mk dwm/install`, `mk dwm/clean`.
- `mk clean` — remove build artifacts

When you build, also install: run `sudo mk install` after a successful `mk` (or `sudo mk <dir>/install` for a single component). The user expects installed binaries to track the source tree after each change.

By default `mk` builds **optimized**: C with `-O2`, Rust with `cargo build --release` (the `--release` is appended to `CARGO_BUILD_FLAGS` in `config.mk`). For test/debuggable builds, pass **`mk DEBUG=1 ...`** — C gets `-g -O0` (debug symbols, no optimization) and Rust builds in cargo's dev profile (no `--release`); the rust template forwards `${CARGO_BUILD_FLAGS}` either way. A command-line `CARGO_BUILD_FLAGS=...` override still keeps the `--release` (mk's `+=` appends to it), so feature-selection builds stay optimized unless you also pass `DEBUG=1`.

`mk` drives three reusable templates from `templates.mk`, expanded via `.expand`:
- `.template prog` — a single C binary. The component `Mkfile` sets `NAME` (and optionally `MAN`) then does `.expand prog`.
- `.template dir` — subdirectory recursion; the root `Mkfile` ends with `.expand dir`.
- `.template rust` — a Rust binary built with `cargo build ${CARGO_BUILD_FLAGS}` (which `config.mk` defaults to `--release`; `mk DEBUG=1` drops `--release` for a dev build). Sources follow cargo's normal layout: `.rs` files under `src/` (cargo finds `src/main.rs` by default; no `[[bin]] path` needed). The template declares `.SUBDIRS: src` so mk knows about the source dir, and `SRC != find src -name '*.rs' -type f -printf '%p '` collects **space-separated full paths** (`src/main.rs src/memfs.rs`) as prerequisites — the `-printf '%p '` matters: default `find` output is newline-separated, which splits the dependency line across lines and breaks `mk`. `${CARGO_BUILD_FLAGS}` is forwarded to cargo, so feature selection works. Pass it as a **mk command-line variable** (not env — config.mk's `?=` shadows the environment), and `clean` first since mk keys rebuilds on source mtimes, not flag changes: `mk raftfs/clean && mk CARGO_BUILD_FLAGS='--no-default-features --features block-leader-local' raftfs` (the `--release` is appended automatically; add `DEBUG=1` for an unoptimized feature build). Same `NAME`/`MAN`/`install-extra` hooks as `prog`. Used by `raftfs/`.

## Config / overrides

`config.mk` is the committed default and now centralizes every install-layout and compiler variable (previously these were scattered across the root `Mkfile`, `templates.mk`, and per-component `Mkfile`s). `config.mk.local` is **gitignored** and `-include`d at the *end* of `config.mk` — put machine-local overrides here. Because all knobs are declared with `?=`/`+=`, both overrides (`FONT_SIZE_TERM = 10`) and appends (`CFLAGS += -g`) in `config.mk.local` work correctly. Don't edit `config.mk` for personal settings.

Knobs: layout — `PREFIX` (default `/usr/local`), `BINPREFIX`, `MANPREFIX`, `SCRIPTSDIR` (renamed from `SCRIPTDIR` during the rework — update stale references), `GAMESDIR`, `DATADIR`, `CONFDIR`, `USERCONFDIR`; compiler — `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`; Rust — `CARGO` (default `cargo`), `RUSTFLAGS`, `CARGO_BUILD_FLAGS` (flags for `cargo build`; defaults to `--release`, dropped under `DEBUG=1`); UI — `TERM` (default `st`), `FONT`, `FONT_SIZE_TOPBAR`, `FONT_SIZE_TERM`. `DESTDIR` is honored at install time.

## Verification

There is **no test suite**. A change is verified by `mk` building cleanly across the components you touched. There is no lint/typecheck step either; treat compiler warnings (`-Wall -Wextra -pedantic`) as the gate.

Two drift checks are available against an already-installed system:
- `mk check` — diffs installed `/etc` files vs `etc/<OS>/` + `etc/common/`
- `mk check-user` — diffs installed dotfiles in `$HOME` vs `dotfiles/`

## Layout

Suckless forks (each has its own `config.h` for customization, built against `../master.h`): `dwm/`, `dmenu/`, `st/`, `slock/`.

Original software: `bedstatus/` (status bar), `xbgcd/` (X background setter), `netris/` (tetris, needs ncurses + `-lbsd` on Linux), `misc/` (small utilities: `bidle`, `flash`, `slowcat`, `gurkx`, `xxed`, `pinentry-dmenu2`, `wt`; see `misc/AGENTS.md`), `raftfs/` (FUSE + Raft distributed filesystem; see the "raftfs" section below), `scripts/` (`dmenu_*`, `launch-*`, etc.).

Deployed config: `dotfiles/` (copied to `$HOME` via `mk install-user`) and `etc/` (split into `common/`, `OpenBSD/`, `FreeBSD/`; installed to `/etc` via `mk install-etc`). There is no Linux-specific `etc/` dir.

## Platform conditionals

`OS` is assigned `OS != uname` in `config.mk` and re-exported via `.EXPORTS: OS` in the root `Mkfile`. Component Mkfiles branch on it:
- `bedstatus/` compiles `openbsd.c` / `freebsd.c` / `linux.c` / `unsupported.c`.
- On Linux, `slock` links `-lcrypt` and `netris` links `-lbsd`. Linux users need `libbsd-dev` (or equivalent) and `libcrypt-dev`.
- `raftfs/` is Linux-only (depends on `fuser`/libfuse3). Its `Mkfile` takes the `.if "${OS}" == "Linux"` branch and expands the rust template; on OpenBSD/FreeBSD it falls through to a stub that no-ops `all`/`clean`/`install`.
- Don't assume a Linux-only toolchain; keep OpenBSD/FreeBSD paths working.

## Install-time side effects

Some `install-extra` targets set file modes you should not break:
- `slock` is installed `u+s` (setuid root).
- `netris` is installed `g+s`, creates `/var/games/netris.scores` owned by group `games`.

## C conventions

- Standard is **c2x** (`-std=c2x -pedantic`), with `-Wall -Wextra -Wno-sign-compare`.
- `master.h` at repo root defines `BUGREPORT` and the `UNUSED` macro; suckless components depend on `../master.h`.
- pkg-config pulls X libs (`x11`, `xft`, `xinerama`, `xext`, `xrandr`, `fontconfig`, `freetype2`, `xscrnsaver`, `xrender`). Don't hardcode include paths.

## Scripts

`scripts/Mkfile` auto-discovers any executable file in `scripts/` and at install time runs `sed` over `@PREFIX@`, `@SCRIPTS@`, `@TERM@`, `@DATADIR@`. To add a script, make it executable (`chmod +x`) and use those tokens instead of hardcoding paths. The root `Mkfile`'s `.sh` rule similarly substitutes `@VERSION@`/`@PREFIX@`/`@TERM@` for `.sh.in`-style sources.

## Cross-component: `wt` ↔ `bedstatus`

`misc/wt` (work-time stopwatch) and `bedstatus` are coupled by an undocumented runtime contract — preserve it when touching either:

- `$XDG_RUNTIME_DIR/wt.active` (or `/tmp/wt.active` if `XDG_RUNTIME_DIR` is unset) is the lock file `wt` writes when a session is running. Format: `<PID> <stem>\n` where `<stem>` is the basename of the selected CSV with `.csv` stripped (e.g. `/home/u/Documents/wt/work.csv` → `work`).
- `wt` refuses to start a new session while the file exists (no stale-lock recovery — manual `rm` is needed after a `kill -9`).
- `bedstatus`'s `format_wt` reads the same file every refresh and renders `[<stopwatch-icon> stem]` between the BAT and TMR sections when present.
- Right after writing the lock file, `wt` runs `pgrep -x bedstatus` and sends `SIGUSR1` to each PID for an immediate refresh. bedstatus already registers `sig_reset` for `SIGUSR1`, which interrupts its `sleep(1)` loop.
- The desktop `lock` script (`scripts/lock`) sends `SIGUSR1` to any running `wt` via `pkill -USR1 -x wt` right before invoking `slock`, so an active work session is recorded up to the lock moment. `wt`'s `stop_session` handler ends the session and returns to the session list; with no session active the signal is a no-op.

## raftfs

FUSE-based distributed filesystem with Raft consensus (`raftfs/`, Rust). Linux-only; see the platform-conditional note above. Build: `mk raftfs`, install: `sudo mk raftfs/install`. CLI: `format` / `start [--mount <mp>]` / `join --leader <addr>` / `promote --leader <addr> --id <N>`.

**Current state (P2–P4 done):** a working replicated, durable, multi-node filesystem. Verified: a 2-voter cluster elects a leader, FUSE writes go through Raft, small files replicate via the Raft log and large files via out-of-band quorum block push, reads work on every node (stale), data survives a full cluster restart (FSM rebuilt from the on-disk Raft log + blocks on disk), and new nodes join as learners then promote to voters online.

Topology (per the design): servers = voters (quorum-2 → both servers must be up to write; read-only otherwise), edge devices = learners. No disconnected writes — a device writes only when the server quorum is reachable.

Run a cluster:
```
raftfs format /data/n1 --id 1 --addr H:P --bootstrap "1=H1:P1,2=H2:P2"
raftfs format /data/n2 --id 2 --addr H:P --bootstrap "1=H1:P1,2=H2:P2"
raftfs start /data/n1 --mount /mnt/a &  raftfs start /data/n2 --mount /mnt/b &
# add a node later:
raftfs format /data/n3 --id 3 --addr H3:P3 && raftfs start /data/n3 &
raftfs join   /data/n3 --leader <leader-addr>   # learner, catches up + can read
raftfs promote --leader <leader-addr> --id 3     # learner -> voter
```

Layout: `model.rs` / `cmd.rs` / `fsm.rs` / `block.rs` / `logstore.rs` / `smstore.rs` / `net.rs` / `raftnode.rs` are core (always compiled — the server node shares them; only `fuser`/`libc` are `fs`-gated via `src/fuse.rs`).

Files:
- `src/main.rs` — clap CLI: `format`/`start [--mount]`/`join`/`promote`. Installs a `tracing-subscriber` (RUST_LOG); `start` builds a multi-thread tokio runtime, sets the node up, and either runs FUSE on the main thread (with a background metrics task) or blocks on a metrics loop.
- `src/raft.rs` — openraft `TypeConfig` (`declare_raft_types!`): `D = RaftEntryData { cmd, ctx }`, `R = Result<(), Error>`, `NodeId = u64`, `Node = BasicNode`, `SnapshotData = Cursor<Vec<u8>>`, `AsyncRuntime = TokioRuntime`. Needs the `storage-v2` and `serde` features of openraft 0.9.
- `src/logstore.rs` — **pure-Rust** `RaftLogStorage` (v2) + `RaftLogReader`: entries + vote/committed/last_purged in an `Arc<Mutex<Inner>>`, persisted by atomic rewrite+fsync of `raft/log.bin` on each mutation (RocksDB's C++ wouldn't build on this toolchain and would hurt portability). `append` calls the `LogFlushed` callback after fsync.
- `src/smstore.rs` — `RaftStateMachine` (v2) + `RaftSnapshotBuilder` over the `Fsm` (shared via `Arc<Mutex<Fsm>>` with the FUSE frontend). `apply` deserializes `RaftEntryData` and calls `fsm.apply`; snapshots are bincode of `{last_applied, last_membership, fsm}` to `raft/snapshot.bin`. Snapshot policy is currently `Never` (the full log is always replayed on restart); real compaction is a later step.
- `src/net.rs` — `RaftNetwork`/`RaftNetworkFactory` (outbound) + a tagged-frame TCP server (inbound) over length-prefixed bincode on one port per node. Tags: `Append`/`Vote`/`Snap` (Raft RPCs → `raft.append_entries`/`vote`/`install_snapshot`), `BlockGet`/`BlockPut` (out-of-band block service → local `DiskStore`), `Forward` (non-leader→leader write redirect), `Join`/`Promote` (membership mgmt → `add_learner`/`change_membership`).
- `src/raftnode.rs` — `NodeConfig` (id/addr/data_dir/bootstrap/preferred_leader, bincode `node.conf`), `setup()` (opens log+sm stores, `Raft::new`, `initialize` if a bootstrap member, spawns the TCP server), `metrics_loop`. Every bootstrap member `initialize`s with the same voter set.
- `src/block.rs` — `BlockStore` trait (`put`/`get`/`has`), `MemStore`, and `DiskStore` (content-addressed `blocks/<b0>/<b1>/<hex>`, `blake3`, crash-safe tmp→fsync→rename).
- `src/fsm.rs` — deterministic `Fsm::apply(&Ctx, &Cmd)` + read helpers + `snapshot`/`restore` (serde).
- `src/cmd.rs` — `Cmd` enum + `Cmd::name()`.
- `src/model.rs` — `Inode`/`Kind`/`Extent`/`FileData` (Inline ≤4 KiB | Extents)/`Attr`/`Ctx`/`Error`/`SetTime` (all serde).
- `src/fuse.rs` (`fs`-gated) — `Frontend { rt, raft, fsm, disk, enc }` implementing `fuser::Filesystem`. **Sync↔async bridge:** mutating ops `spawn_recv` the async work on the runtime and block the FUSE thread on a std mpsc (NOT `Handle::block_on`, which wedged here). Metadata ops issue `client_write(Cmd)`; data writes chunk into content-addressed blocks, push them to peer voters (quorum), then `client_write(WriteMeta)`. Reads are synchronous for inline files; extent reads fetch blocks from the local `DiskStore` with lazy peer fetch on miss. If `--enc` is set, data writes encrypt each block (`crypto::encrypt_block`) and reads decrypt (`crypto::materialize`), so the FSM/store hold only ciphertext. **Beware:** never re-lock the shared `fsm` mutex while already holding it (e.g. `lock().lookup().and_then(|i| lock().attr(i))` self-deadlocks) — use one guard. Optional op tracing via `RAFTFS_TRACE=1` (writes `/tmp/opencode/fuse.trace` + `sm.trace`).
- `src/crypto.rs` (`crypto`-gated) — the optional encryption layer: XChaCha20-Poly1305 block AEAD, HKDF per-file keys, Argon2id passphrase→key. See the "Encryption" paragraph below.

Cargo features: `fs` (default) gates the FUSE frontend (`fuser`/`libc` optional, `src/fuse.rs` + `--mount` cfg-gated); `crypto` (default, implies `fs`) enables the optional encryption layer (`chacha20poly1305`/`argon2`/`hkdf`/`sha2`/`rand` optional deps, `src/crypto.rs` + `--enc`); `block-quorum` (default) / `block-leader-local` select the block-replication strategy (quorum push is implemented; leader-local is a stub). Server-only build: `mk raftfs/clean && mk CARGO_BUILD_FLAGS='--no-default-features --features block-quorum' raftfs`.

**Encryption (optional, `crypto` feature):** `src/crypto.rs` is a frontend-only layer — encrypted file data is ciphertext in the FSM/block store, so storage/replication nodes never see plaintext. Per-directory encryption uses a FUSE-native lock/unlock UX: every plain directory presents a virtual `lock` file; writing a passphrase to it generates a random dir key, wraps it (Argon2id → stored as a hidden `.raftfs.enc` marker in the cluster), and unlocks the directory. A locked directory (e.g. after remount, before unlocking) presents **only** a virtual `unlock` file — all real contents are hidden. Writing the passphrase to `unlock` reads the marker, unwraps the key, and reveals the (decrypted) contents. Per-file keys are `HKDF-SHA256(dir_key, info=ino)`; each block is AEAD'd with XChaCha20-Poly1305 (random 24-byte nonce prepended). Encrypted files always use the extents path (never inline). Verified end-to-end: locked dir shows only `unlock`; hidden file.txt is invisible; unlock reveals contents; data decrypts correctly; blocks contain no plaintext; a node without the key sees garbage. **Not yet done:** filename encryption (names are still plaintext) — additive on the same `crypto.rs` primitives.

**Known limitations / not yet done:** write forwarding to the leader is stubbed (mount the leader node to write; reads work anywhere); no master-leader pinning (openraft 0.9 has no `transfer_leader`); snapshot policy is `Never` (log grows unbounded — fine for now, needs compaction for long-offline rejoins); POSIX permissions not enforced; the write path is O(filesize) per write.
