PREFIX = /usr/local
MANPREFIX = ${PREFIX}/man
SCRIPTDIR = ${PREFIX}/libexec/desktop

VERSION = "benni-0.1"

CPPFLAGS = -D_BSD_SOURCE -D_POSIX_C_SOURCE=200809L -DPREFIX=\"${PREFIX}\" -DVERSION=\"${VERSION}\"
CFLAGS = -std=c99 -pedantic -Wall -Wunused-parameter -O2 ${CPPFLAGS}

