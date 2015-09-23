
TGTS := libioqueue.a
SRCS := ioqueue.c

$(call depends,libioqueue.a,ioqueue.o)

TGTS += libioqueuemt.a
SRCS += ioqueuemt.c

$(call depends,libioqueuemt.a,ioqueuemt.o)
