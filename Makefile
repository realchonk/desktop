include config.mk

SRC_DWM		= dwm/dwm.c dwm/drw.c dwm/util.c
HDR_DWM		= dwm/drw.h dwm/util.h dwm/config.h

SRC_ST		= st/st.c st/x.c
HDR_ST		= st/arg.h st/st.h st/win.h st/config.h

SRC_DMENU	= dmenu/dmenu.c dmenu/drw.c dmenu/util.c
HDR_DMENU	= dmenu/arg.h dmenu/config.h dmenu/drw.h dmenu/util.h

SRC_STEST	= dmenu/stest.c
HDR_STEST	= ${HDR_DMENU}

SRC_SLOCK	= slock/slock.c
HDR_SLOCK	= slock/arg.h slock/config.h slock/util.h

MAN		= dwm/dwm.1 st/st.1 dmenu/dmenu.1 dmenu/stest.1 slock/slock.1

all: bin/dwm bin/st bin/bedstatus bin/dmenu bin/stest bin/xbgcd bin/slock

clean:
	rm -rf bin

install:
	mkdir -p ${DESTDIR}${PREFIX}/bin ${DESTDIR}${MANPREFIX}/man1 ${DESTDIR}${SCRIPTDIR} ${DESTDIR}/etc
	cp -f bin/* ${DESTDIR}${PREFIX}/bin/
	cp -rf etc/* ${DESTDIR}/etc/
	for f in scripts/*; do										\
		sed 's#@PREFIX@#${PREFIX}#g; s#@SCRIPTS@#${SCRIPTDIR}#' < $$f 				\
		> ${DESTDIR}${SCRIPTDIR}/$$(basename $$f);						\
		chmod +x ${DESTDIR}${SCRIPTDIR}/$$(basename $$f);					\
	done
	for f in ${MAN}; do										\
		sed 's/VERSION/${VERSION}/g' < $$f > ${DESTDIR}${MANPREFIX}/man1/$$(basename "$$f");	\
	done
	chmod u+s ${DESTDIR}${PREFIX}/bin/slock

install-user:
	cp -rf dotfiles/.* ${HOME}/

bin/dwm: ${SRC_DWM} ${HDR_DWM}
	@mkdir -p bin
	${CC} -o $@ ${SRC_DWM} ${CFLAGS} `pkg-config --cflags --libs fontconfig freetype2 x11 xft xinerama`

bin/st: ${SRC_ST} ${HDR_ST}
	@mkdir -p bin
	${CC} -o $@ ${SRC_ST} ${CFLAGS} `pkg-config --cflags --libs fontconfig freetype2 x11 xft xrender` -lm -lutil

bin/bedstatus: bedstatus/bedstatus.c
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

.PHONY: all clean install
