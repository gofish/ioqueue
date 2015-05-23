LDFLAGS := -pthread
LDLIBS  := -laio -lrt

TGTS := bench libioq.a
SRCS := bench.cc ioqueue.c

$(call depends,libioqueue.a,ioqueue.o)
$(call depends,bench,libioqueue.a)

TGTS += benchmt libioqmt.a
SRCS += benchmt.cc ioqueuemt.c

$(call depends,libioqueuemt.a,ioqueuemt.o)
$(call depends,benchmt,libioqueuemt.a)
