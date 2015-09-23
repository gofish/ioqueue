LDFLAGS := -pthread

TGTS := bench libioqueue.a
SRCS := bench.cc ioqueue.c

$(call depends,libioqueue.a,ioqueue.o)
$(call depends,bench,libioqueue.a)
$(call depends_ext,bench,-laio)
$(call depends_ext,bench,-lrt)

TGTS += benchmt libioqueuemt.a
SRCS += benchmt.cc ioqueuemt.c

$(call depends,libioqueuemt.a,ioqueuemt.o)
$(call depends,benchmt,libioqueuemt.a)
