# what flags you want to pass to the C compiler & linker
AM_CFLAGS =
AM_LDFLAGS =

# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
sbin_PROGRAMS = vcontrold
bin_PROGRAMS = vclient vsim
vcontrold_SOURCES = vcontrold.cpp parser.cpp framer.cpp io.cpp common.cpp socket.cpp xmlconfig.cpp conversion.cpp parser.h framer.h io.h common.h socket.h xmlconfig.h conversion.h
vclient_SOURCES = vclient.c client.c client.h prompt.h common.cpp common.h socket.cpp socket.h io.cpp io.h
