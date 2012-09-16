# Makefile for monsterwm - see LICENSE for license and copyright information

VERSION = cookies-git
WMNAME  = monsterwm

PREFIX ?= /usr/local
BINDIR ?= ${PREFIX}/bin
MANPREFIX = ${PREFIX}/share/man

X11INC = -I/usr/X11R6/include
X11LIB = -L/usr/X11R6/lib -lX11
XINERAMALIB = -lXinerama

INCS = -I. -I/usr/include ${X11INC}
LIBS = -L/usr/lib -lc ${X11LIB} ${XINERAMALIB}

CFLAGS   = -std=c99 -pedantic -Wall -Wextra ${INCS} -DVERSION=\"${VERSION}\"
LDFLAGS  = ${LIBS}

CC 	 = cc
EXEC = ${WMNAME}

SRC = ${WMNAME}.c
OBJ = ${SRC:.c=.o}

all: CFLAGS += -Os
all: LDFLAGS += -s
all: options ${WMNAME} monsterpager monsterstatus

debug: CFLAGS += -O0 -g
debug: options ${WMNAME}

monsterpager:
	@echo "Building monsterpager"
	@${CC} 3rdparty/monsterpager.c -o monsterpager

monsterstatus:
	@echo "Building monsterstatus"
	@${CC} 3rdparty/monsterstatus.c -lasound -lmpdclient -o monsterstatus

options:
	@echo ${WMNAME} build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h

config.h:
	@echo creating $@ from config.def.h
	@cp config.def.h $@

${WMNAME}: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -fv ${WMNAME} ${OBJ} ${WMNAME}-${VERSION}.tar.gz
	@rm monsterpager
	@rm monsterstatus

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@install -Dm755 ${WMNAME} ${DESTDIR}${PREFIX}/bin/${WMNAME}
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man.1
	@install -Dm644 ${WMNAME}.1 ${DESTDIR}${MANPREFIX}/man1/${WMNAME}.1
	@echo installing 3rdparty executables
	@[[ -f monsterpager ]] && install -Dm755 monsterpager ${DESTDIR}${PREFIX}/bin/monsterpager
	@[[ -f monsterstatus ]] && install -Dm755 monsterstatus ${DESTDIR}${PREFIX}/bin/monsterstatus

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/{${WMNAME},monsterpager,monsterstatus}
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/${WMNAME}.1

.PHONY: all options clean install uninstall
