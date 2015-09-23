LDFLAGS := -pthread

TGTS += bench
SRCS += bench.cc

$(call depends_ext,bench,libioqueue.a)
$(call depends_ext,bench,-laio)
$(call depends_ext,bench,-lrt)

TGTS += benchmt
SRCS += benchmt.cc

$(call depends_ext,benchmt,libioqueuemt.a)
