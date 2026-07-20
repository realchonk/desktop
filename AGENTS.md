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

`mk` drives two reusable templates from `templates.mk`: `.template prog` (single binary) and `.template dir` (subdirectory recursion), expanded via `.expand`. Each component `Mkfile` sets `NAME` (and optionally `MAN`) then does `.expand prog`.

## Config / overrides

`config.mk` is the committed default. `config.mk.local` is **gitignored** and `-include`d from `config.mk` — put machine-local overrides here (e.g. `FONT_SIZE_TOPBAR`, `FONT_SIZE_TERM`). Don't edit `config.mk` for personal settings.

Tunable knobs: `PREFIX` (default `/usr/local`), `TERM` (default `st`), `FONT`, `FONT_SIZE_TOPBAR`, `FONT_SIZE_TERM`, plus `DESTDIR`/`CONFDIR`/`USERCONFDIR` at install time.

## Verification

There is **no test suite**. A change is verified by `mk` building cleanly across the components you touched. There is no lint/typecheck step either; treat compiler warnings (`-Wall -Wextra -pedantic`) as the gate.

Two drift checks are available against an already-installed system:
- `mk check` — diffs installed `/etc` files vs `etc/<OS>/` + `etc/common/`
- `mk check-user` — diffs installed dotfiles in `$HOME` vs `dotfiles/`

## Layout

Suckless forks (each has its own `config.h` for customization, built against `../master.h`): `dwm/`, `dmenu/`, `st/`, `slock/`.

Original software: `bedstatus/` (status bar), `xbgcd/` (X background setter), `netris/` (tetris, needs ncurses + `-lbsd` on Linux), `misc/` (small utilities: `bidle`, `flash`, `slowcat`, `gurkx`, `xxed`, `pinentry-dmenu2`, `wt`; see `misc/AGENTS.md`), `scripts/` (`dmenu_*`, `launch-*`, etc.).

Deployed config: `dotfiles/` (copied to `$HOME` via `mk install-user`) and `etc/` (split into `common/`, `OpenBSD/`, `FreeBSD/`; installed to `/etc` via `mk install-etc`). There is no Linux-specific `etc/` dir.

## Platform conditionals

`OS != uname` is exported from the root Mkfile. Component Mkfiles branch on it:
- `bedstatus/` compiles `openbsd.c` / `freebsd.c` / `linux.c` / `unsupported.c`.
- On Linux, `slock` links `-lcrypt` and `netris` links `-lbsd`. Linux users need `libbsd-dev` (or equivalent) and `libcrypt-dev`.
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
- `bedstatus`'s `format_wt` reads the same file every refresh and renders `[<stopwatch-icon> stem]` between the BAT and TMR section when present.
- Right after writing the lock file, `wt` runs `pgrep -x bedstatus` and sends `SIGUSR1` to each PID for an immediate refresh. bedstatus already registers `sig_reset` for `SIGUSR1`, which interrupts its `sleep(1)` loop.
