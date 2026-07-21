OS != uname
HOME != echo $$HOME
VERSION = "benni-0.1"

# Directories

## Top-level installation directory
PREFIX ?= /usr/local

## System configuration files directory
CONFDIR ?= /etc

## Binary install directory
BINPREFIX ?= ${PREFIX}/bin

## Manual pages install directory
MANPREFIX ?= ${PREFIX}/man

## Scripts install directory
SCRIPTSDIR ?= ${PREFIX}/libexec/desktop

## Games data directory
GAMESDIR ?= /var/games

## Desktop data directory
DATADIR ?= ${PREFIX}/share/desktop

## User configuration files directory
USERCONFDIR ?= ${HOME}

# Compilation Settings

## C Compiler
CC ?= cc

## C preprocessor flags
CPPFLAGS += -D_BSD_SOURCE -D__BSD_VISIBLE -D_XOPEN_SOURCE=700 -DPREFIX=\"${PREFIX}\" -DVERSION=\"${VERSION}\" -DTERM=\"${TERM}\"  -DXINERAMA

## C compiler flags
CFLAGS += -std=c2x -pedantic -Wall -Wextra -Wno-sign-compare -O2 ${CPPFLAGS}

## Linker Flags
LDFLAGS +=

## Cargo binary
CARGO ?= cargo

## Rust compiler flags
RUSTFLAGS +=

# Other settings

## Default Terminal
TERM = st

## Default Font
FONT ?= Liberation Mono

## Font size for the top bar
FONT_SIZE_TOPBAR ?= 6

## Default font size for the terminal
FONT_SIZE_TERM ?= 8


-include config.mk.local

.EXPORTS: PREFIX CONFDIR BINPREFIX MANPREFIX SCRIPTSDIR GAMESDIR DATADIR USERCONFDIR
.EXPORTS: CC CPPFLAGS CFLAGS LDFLAGS CARGO RUSTFLAGS
.EXPORTS: TERM FONT FONT_SIZE_TOPBAR FONT_SIZE_TERM
