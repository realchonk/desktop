-include config.mk.local

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/man
SCRIPTDIR = ${PREFIX}/libexec/desktop
GAMESDIR = /var/games
TERM = st

FONT ?= Liberation Mono
FONT_SIZE_TOPBAR ?= 6
FONT_SIZE_TERM ?= 6

VERSION = "benni-0.1"

CPPFLAGS = -D_BSD_SOURCE -D__BSD_VISIBLE -D_XOPEN_SOURCE=700 -DPREFIX=\"${PREFIX}\" -DVERSION=\"${VERSION}\" -DTERM=\"${TERM}\"  -DXINERAMA
CFLAGS = -std=c2x -pedantic -Wall -Wextra -Wno-sign-compare -O2 ${CPPFLAGS}

