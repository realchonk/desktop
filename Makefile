include config.mk

SRC_DWM		= dwm/dwm.c dwm/drw.c dwm/util.c
HDR_DWM		= dwm/drw.h dwm/util.h dwm/config.h master.h

SRC_ST		= st/st.c st/x.c
HDR_ST		= st/arg.h st/st.h st/win.h st/config.h master.h

SRC_BS		= bedstatus/bedstatus.c
HDR_BS		= bedstatus/bedstatus.h				\
		  bedstatus/unsupported.c			\
		  bedstatus/openbsd.c				\
		  bedstatus/linux.c				\
		  bedstatus/freebsd.c

SRC_DMENU	= dmenu/dmenu.c dmenu/drw.c dmenu/util.c
HDR_DMENU	= dmenu/arg.h dmenu/config.h dmenu/drw.h dmenu/util.h master.h

SRC_STEST	= dmenu/stest.c
HDR_STEST	= dmenu/arg.h

SRC_SLOCK	= slock/slock.c
HDR_SLOCK	= slock/arg.h slock/config.h

SRC_PD2 =	pinentry-dmenu2.sh

SRC_NETRIS	= netris/input.c				\
		  netris/screen.c				\
		  netris/shapes.c				\
		  netris/scores.c				\
		  netris/tetris.c

HDR_NETRIS	= netris/input.h				\
		  netris/pathnames.h				\
		  netris/scores.h				\
		  netris/screen.h				\
		  netris/tetris.h

SRC_TMR		= bedstatus/timer

MAN		= dwm/dwm.1 st/st.1 dmenu/dmenu.1 dmenu/stest.1 slock/slock.1 netris/netris.6

all: bin/dwm bin/st bin/bedstatus bin/dmenu bin/stest bin/xbgcd bin/slock bin/pinentry-dmenu2 bin/netris bin/timer bin/flash bin/slowcat

check:
	find etc/common etc/$$(uname) -not -type d | awk '{a=$$0; sub(/etc\/[^\/]+/, "/etc", a); system("diff -u " $$0 " " a)}'

check-user:
	find dotfiles/ -not -type d -exec sh -c 'diff -u {} "$$HOME/$$(echo "{}" | sed 's@^dotfiles/@@')"' \;

clean:
	rm -rf bin

install:
	mkdir -p	${DESTDIR}${PREFIX}/bin		\
			${DESTDIR}${MANPREFIX}/man1	\
			${DESTDIR}${MANPREFIX}/man6	\
			${DESTDIR}${SCRIPTDIR}		\
			${DESTDIR}${GAMESDIR}
	cp -f bin/* ${DESTDIR}${PREFIX}/bin/
	for f in scripts/*; do										\
		sed 's#@PREFIX@#${PREFIX}#g; s#@SCRIPTS@#${SCRIPTDIR}#; s#@TERM@#${TERM}#' < $$f 	\
		> ${DESTDIR}${SCRIPTDIR}/$$(basename $$f);						\
		chmod +x ${DESTDIR}${SCRIPTDIR}/$$(basename $$f);					\
	done
	for f in ${MAN}; do										\
		s=$$(echo "$$f" | sed 's/^[^.]*.\([0-9]\)$$/\1/');					\
		sed 's/VERSION/${VERSION}/g' < $$f > ${DESTDIR}${MANPREFIX}/man$$s/$$(basename "$$f");	\
	done
	touch ${DESTDIR}${GAMESDIR}/netris.scores
	-chgrp games ${DESTDIR}${PREFIX}/bin/netris ${DESTDIR}${GAMESDIR}/netris.scores
	chmod u+s ${DESTDIR}${PREFIX}/bin/slock
	chmod g+s ${DESTDIR}${PREFIX}/bin/netris
	chmod g+rw ${DESTDIR}${GAMESDIR}/netris.scores

install-etc:
	mkdir -p ${DESTDIR}/etc
	cp -rf etc/common/* etc/$$(uname)/* ${DESTDIR}/etc/

install-user:
	find dotfiles -mindepth 1 -maxdepth 1 | xargs -I '{}' cp -rf '{}' "$$HOME/"

install-pkgs:
	pkg_add -l pkgs

bin/dwm: ${SRC_DWM} ${HDR_DWM}
	@mkdir -p bin
	${CC} -o $@ ${SRC_DWM} ${CFLAGS} `pkg-config --cflags --libs fontconfig freetype2 x11 xft xinerama`

bin/st: ${SRC_ST} ${HDR_ST}
	@mkdir -p bin
	${CC} -o $@ ${SRC_ST} ${CFLAGS} `pkg-config --cflags --libs fontconfig freetype2 x11 xft xrender` -lm -lutil

bin/bedstatus: ${SRC_BS} ${HDR_BS}
	@mkdir -p bin
	${CC} -o $@ bedstatus/bedstatus.c ${CFLAGS} `pkg-config --cflags --libs x11`

bin/dmenu: ${SRC_DMENU} ${HDR_DMENU}
	@mkdir -p bin
	${CC} -o $@ ${SRC_DMENU} ${CFLAGS} `pkg-config --cflags --libs fontconfig freetype2 x11 xft xinerama` -lm

bin/stest: ${SRC_STEST} ${HDR_STEST}
	@mkdir -p bin
	${CC} -o $@ ${SRC_STEST} ${CFLAGS}

bin/xbgcd: xbgcd/xbgcd.c xbgcd/config.h
	@mkdir -p bin
	${CC} -o $@ xbgcd/xbgcd.c ${CFLAGS} `pkg-config --cflags --libs x11`

bin/slock: ${SRC_SLOCK} ${HDR_SLOCK}
	@mkdir -p bin
	${CC} -o $@ ${SRC_SLOCK} ${CFLAGS} `pkg-config --cflags --libs x11 xext xrandr` -lpthread

bin/pinentry-dmenu2: ${SRC_PD2}
	@mkdir -p bin
	cp -f ${SRC_PD2} $@
	chmod +x $@

bin/netris: ${SRC_NETRIS} ${HDR_NETRIS}
	@mkdir -p bin
	${CC} -o $@ ${SRC_NETRIS} ${CFLAGS} -lncurses

bin/timer: ${SRC_TMR}
	@mkdir -p bin
	cp -f ${SRC_TMR} $@
	chmod +x $@

bin/flash: flash.c
	@mkdir -p bin
	${CC} -o $@ flash.c ${CFLAGS}

bin/slowcat: slowcat.c
	@mkdir -p bin
	${CC} -o $@ slowcat.c ${CFLAGS}

bin/bidle: bidle.c
	@mkdir -p bin
	${CC} -o $@ bidle.c ${CFLAGS} `pkg-config --cflags --libs x11 xscrnsaver`

.PHONY: all clean install
