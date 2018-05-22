ioqueue
====

Asynchronous direct I/O operations in C.

Licence
----

The library is a small wrapper around each of two backends, one implemented using Linux kernel AIO ([KAIO][KAIO]) and one using POSIX threads (Pthreads). The former is released under the terms of the LGPL version 3 or greater and uses the GPLv3 libaio. The latter is released under the 3-clause BSD license, provided it is linked against a compatible Pthreads implementation.

API
---

The API is single-threaded and is intended to be used in a single process with no threads, or via a single I/O manager thread. I/O requests submitted via `ioqueue_{pread,pwrite}` are asynchronous and will not begin to execute until after the next call to `ioqueue_reap`, which blocks for the specified number of completed requests and executes their callback functions.

When using the Linux KAIO backend, file descriptors passed to `ioqueue_{pread,write}` are required to have been [opened][open] with flag O\_DIRECT. The threaded backend may be used with O\_DIRECT or e.g. with POSIX\_FADV\_NOREUSE. Applications will likely incur lower CPU usage using the KAIO backend.

From [ioqueue.h][ioqueue.h]:

```c
/* initialize the queue to the given maximum outstanding requests */
int  ioqueue_init(unsigned int depth);

/* read/write callback function type (required) */
typedef void (*ioqueue_cb)(void *arg, ssize_t res, void *buf);

/* enqueue a pread request */
int  ioqueue_pread(int fd, void *buf, size_t len, off_t offset, ioqueue_cb cb, void *cb_arg);

/* enqueue a pwrite request */
int  ioqueue_pwrite(int fd, void *buf, size_t len, off_t offset, ioqueue_cb cb, void *cb_arg);

/* submit requests and handle completion events */
int  ioqueue_reap(unsigned int min);

/* reap all requests and destroy the queue */
void ioqueue_destroy();
```

When a completed I/O request is reaped from the queue, the callback will be executed with three arguments:
* `arg` - the \[optional] `cb_arg` argument supplied with the callback to the original ioqueue request
* `res` - the return value of the `pread` or `pwrite` call
* `buf` - the buffer passed to `pread` or `pwrite`, as supplied to the original ioqueue request

The included [benchmark][benchmark] is the best usage example. The [`ioqueue_bench()`][ioqueue_bench] function contains the ioqueue API calls.

**Polling**

When using the KAIO backend there is support for using `poll()` (and family) to detect I/O readiness. The file descriptor returned from `ioqueue_eventfd()` will receive `POLL_IN/OUT/ERR` notifications when individual requests have completed or failed.

```C
/* retrieve a file descriptor suitable for io readiness notifications via e.g. poll/epoll */
int  ioqueue_eventfd();
```

Build
----

Use `make`. Requirements:

* Linux kernel >= 2.6.22
* librt (for benchmark)
* libaio (for KAIO backend, via package "libaio-dev" on Ubuntu)
* libgtest (a.k.a. googletest for tests, via "libgtest-dev" on Ubuntu)

For example, on Ubuntu 16.04 a manual build might look like

```
$ cc -g -O1 -c ioqueue.c -o ioqueue.o
$ g++ -g -O1 -c benchmark/bench.cc -o benchmark/bench.o
$ g++ -g -pthread -o benchmark/bench benchmark/bench.o ioqueue.o -laio -lrt
```

[open]: http://man7.org/linux/man-pages/man2/open.2.html
[KAIO]: https://web.archive.org/web/20150406015143/http://code.google.com/p/kernel/wiki/AIOUserGuide
[ioqueue.h]: ioqueue.h
[benchmark]: benchmark/
[bench.cc]: benchmark/bench.cc
[ioqueue_bench]: benchmark/bench.cc#L170
