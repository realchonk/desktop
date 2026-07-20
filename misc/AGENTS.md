# misc/AGENTS.md

Small standalone utilities. Each program is one C file (or one `.sh` source), built by `misc/Mkfile`. `mk misc` builds all of `PROGS`; `mk misc/clean` removes them.

# `wt` — ncurses work-time stopwatch

Single C file `wt.c`, links `-lncurses` directly (no pkg-config, matching `netris`). Stores sessions in CSV files under `~/Documents/wt/` (dir is `mkdir -p`'d on first run).

## CSV format

```
date,start_time,end_time,description
```

- Header line is always written first; `load_csv` skips any line whose first field isn't a valid `YYYY-MM-DD` date (this naturally skips headers, blanks, and stray text — preserves robustness against manual edits).
- Times are `HH:MM` (minute precision). Dates `YYYY-MM-DD`.
- RFC 4180 quoting on descriptions containing `,`, `"`, `\n`, `\r`.
- Descriptions are trimmed of whitespace (incl. newlines) on both `edit_description` and `load_csv`.

## Crash-safe writes

`write_csv` always:
1. Writes the header line.
2. Writes all rows (skipping rows whose fields are all empty).
3. `fflush` → `fsync` → `fclose` → `rename` from `<file>.wt.tmp` to the real path.

Atomicity comes from the final `rename`. While a session runs, the in-progress row's `end` is rolled forward to "now" on every wall-clock minute (`next_flush = (now / 60 + 1) * 60`) and rewritten — a kill leaves the file with the last minute's `end`.

## UI state machine

1. **Picker** — lists `*.csv` in `~/Documents/wt/`. `n` creates a new file (auto-appends `.csv` if missing, touches empty file). `Enter` opens. `q`/Esc quits the program (this is the only way out).
2. **Session list** — `DATE  START -> END  DURATION  DESCRIPTION`. `Space` starts a session (refused if the lock file exists). `e` edits the highlighted row's description. `d` deletes (with `y/N` confirm). `q`/Esc **goes back to the picker** (does not quit).
3. **Running** — big bold `HH:MM:SS` elapsed, redraws every second. Keys:
   - `Space` — stop. **Refused if description is empty** (beep + status line).
   - `e` — edit description (prefilled, see Editor below).
   - `c` — cancel session (drops the in-progress row, rewrites file).
   - `q`/Esc — refused with a hint pointing to `Space` / `c`.
   - `^L` — redraw.

`main` wraps the picker + session-list loop in an outer `for (;;)` so that returning from the session list re-runs `pick_file`. `free_ctx` + `memset(&ctx, 0, ...)` runs between iterations.

## Editor (`prompt_input`)

Custom ncurses line editor — does **not** use `getnstr` (we need prefill + emacs bindings). Cancel via Esc (7/C-g) returns 0; Enter returns 1. Caller only commits on 1.

- Always prefills `buf` (initialized by caller; passed in as-is).
- `ESCDELAY` is set to 250ms in `main` so lone-Esc feels responsive.
- Bindings: `C-a`/`C-e` line ends, `C-b`/`C-f` char motion, `C-d`/`KEY_DC` forward delete, `KEY_BACKSPACE`/`127`/`8` backward delete, `C-k` kill-to-end, `C-u` unix-line-discard, `C-w` unix-word-rubout, `C-t` transpose, `C-l` redraw, `M-b`/`M-f` word motion (parsed from ESC prefix via `nodelay` peek), `C-Left`/`C-Right` word motion (resolved via `key_defined`/`define_key` to `KEY_MAX+1`/`+2` because terminfo's extended `kLFT5`/`kRIT5` capability is consumed by ncurses in `keypad()` mode and never reaches the ESC parser).
- Arrow/Home/End/Delete via standard ncurses `KEY_*` constants.

The editor uses `timeout(-1)` internally; the running loop resets `timeout(1000)` at the top of each iteration so the 1-second elapsed redraw keeps firing after edits.

## Lock file `$XDG_RUNTIME_DIR/wt.active`

- Path is computed lazily by `lock_path()` from `XDG_RUNTIME_DIR` (falls back to `/tmp` if unset or empty). Cached in a function-local `static char *p`.
- Written atomically-ish on session start (`fopen("w")` + `fprintf("%d %s\n", pid, stem)` + `fclose`). Stem is the selected CSV's basename with `.csv` stripped.
- `unlink`'d on every exit path of `run_session`: Space (stop), `c` (cancel), and the `got_signal` branch (SIGINT/SIGTERM → final flush with `end=now`, then unlink).
- `show_sessions` calls `lock_exists()` (just `stat`) before starting; on conflict, beeps and shows `session active (<path>)`.
- **No stale-lock recovery** — after `kill -9` the file remains and a manual `rm` is required. (Checking liveness via `kill(pid, 0)` would be the obvious extension.)
- After writing the lock file, `wt` runs `pgrep -x bedstatus` via `popen` and sends `SIGUSR1` to every match, so bedstatus refreshes immediately (its `sig_reset` handler interrupts the `sleep(1)` loop). Best-effort: silently no-ops if `pgrep` is missing or no bedstatus is running.

## Signals

`SIGINT` and `SIGTERM` are caught via `sigaction` (no `SA_RESTART`) and set a `volatile sig_atomic_t got_signal` flag. The flag is polled at the top of each loop. In `run_session`, signal-exit triggers a final flush + lock removal before returning.

## Misc conventions used

- `atexit(restore_terminal)` calls `endwin()` so any `err()` exit still restores the terminal.
- `_BSD_SOURCE` deprecation warning from `config.mk`'s CPPFLAGS is repo-wide pre-existing — don't try to silence it from `wt.c`.
- No comments in `wt.c` (per repo rule) other than the ISC license header copied from `flash.c`.
