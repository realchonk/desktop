PREFIX = /usr/local
MANPREFIX = ${PREFIX}/man
SCRIPTDIR = ${PREFIX}/libexec/desktop

VERSION = "benni-0.1"

CPPFLAGS = -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -DPREFIX=\"${PREFIX}\" -DVERSION=\"${VERSION}\" -DXINERAMA
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Wno-sign-compare -O2 ${CPPFLAGS}

