## Program Template
.template prog

## Installation prefix for ${NAME}
PREFIX ?= /usr/local

## Installation directory for ${NAME}'s binary
BINPREFIX ?= ${PREFIX}/bin

. if defined(MAN)
MANPREFIX ?= ${PREFIX}/man/man${MAN}
. endif

.DEFAULT: all

. if !target(all)
## Build ${NAME}
all: ${NAME}

.  if target(all-extra)
all: all-extra
.  endif
. endif

. if !target(clean)
## Clean ${NAME}'s build artifacts
clean:
	(cd ${.OBJDIR} && rm -f ${NAME} *.core *.o)

.  if target(clean-extra)
clean: clean-extra
.  endif
. endif

. if !target(install)
## Install ${NAME} into ${DESTDIR}${PREFIX}
install: install-bin

## Install ${NAME}'s binary into ${DESTDIR}${BINPREFIX}
install-bin: ${NAME}
	mkdir -p ${DESTDIR}${BINPREFIX}
	cp -f ${.OBJDIR}/${NAME} ${DESTDIR}${BINPREFIX}

.  if target(install-extra)
install: install-extra
.  endif

.  if defined(MAN)
## Install ${NAME}'s manual into ${DESTDIR}${MANPREFIX}
install-man: ${NAME}.${MAN}
	mkdir -p ${DESTDIR}${MANPREFIX}
	cp -f ${NAME}.${MAN} ${DESTDIR}${MANPREFIX}/
install: install-man
.  endif
. endif
.endt

## Directory Template
.template dir
__CLEAN = ${.SUBDIRS:=/clean}
__INSTALL = ${.SUBDIRS:=/install}

.DEFAULT: all

## Build: ${.SUBDIRS:J, }
all: ${.SUBDIRS}

## Clean: ${.SUBDIRS:J, }
clean: ${__CLEAN}

## Install: ${.SUBDIRS:J, }
install: ${__INSTALL}

. if target(all-extra)
all: all-extra
. endif

. if target(clean-extra)
clean: clean-extra
. endif

. if target(install-extra)
install: install-extra
. endif
.endt
