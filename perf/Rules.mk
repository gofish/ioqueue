LDFLAGS := -pthread

TGTS += bench
SRCS += bench.cc

$(call depends,bench,../libioqueue.a)
$(call depends_ext,bench,-laio)
$(call depends_ext,bench,-lrt)

TGTS += benchmt
SRCS += benchmt.cc

$(call depends,benchmt,../libioqueuemt.a)
