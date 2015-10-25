#define IOQ_BACKEND "pthread"
#define IOQ_OPEN_FLAGS (O_RDONLY)
#define IOQ_FADV_POLICY POSIX_FADV_NOREUSE
#include "bench.cc"

//
// Multi-threaded ioqueue benchmark
//

// Nothing to see here, move along.
