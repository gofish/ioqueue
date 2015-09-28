LDFLAGS := -pthread
LDLIBS  := -lgtest -lgtest_main

TGTS += ioqueue.t
SRCS += ioqueue.t.cc

$(call depends,ioqueue.t,../libioqueue.a)
$(call depends_ext,ioqueue.t,-laio)
$(call depends_ext,ioqueue.t,-lrt)
$(call test,ioqueue.t)

TGTS += ioqueuemt.t
SRCS += ioqueuemt.t.cc

$(call depends,ioqueuemt.t,../libioqueuemt.a)
$(call test,ioqueuemt.t)
