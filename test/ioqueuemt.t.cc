#define TEST_NAME(name) IOQueueMt ## name
#define HAVE_KAIO 0
#define HAVE_EVENTFD 0
#include "ioqueue.t.cc"
