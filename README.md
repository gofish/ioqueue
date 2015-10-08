ioqueue
====

Fast asynchronous direct I/O operations in C.

Building
----

Use `make`.

Requirements for KAIO backend (`make libioqueue.a`):

* Linux >= 2.6.22 ?
* libaio
* librt

Licence
----

The library is a small wrapper around each of two backends, Linux kernal AIO (kaio) and POSIX threads (Pthreads). The former is released under the terms of the LGPL version 3 or greater, due to the direct use of Linux Kernel APIs. The latter is released under the 3-clause BSD license, provided it is linked against a compatible Pthread implementation.

Notes
----

File descriptors are required to be opened with flag [**O\_DIRECT**][odirect].

Synopsis
----

The high random-I/O performance of solid-state drives is a relatively novel development in computing that is potentially applicable to a large number of problems.

One approach to maximize performance of an SSD is to use POSIX threads to execute parallel requests via direct I/O file descriptors. Direct I/O will bypass the kernel buffer cache and execute read and writes directly to the SSD controller. Using threads is necessary, since file I/O is *always* blocking (except, below) and direct I/O will cause a disk read on every call. However, it is also necessary to use many threads in order to saturate the SSD controller, which is backed by several parallel and individually slow NANDs.

The exception to blocking direct I/O on Linux is the kernel [AIO][AIO] interface. These syscalls provide user-space a low-level interface to queue, reap, and poll asynchronous direct I/O requests. In writing and benchmarking `ioqueue` for random reads I found that KAIO on average could match or beat the throughput of the Pthread implementation while decreasing overall CPU usage and request latency. Both backends are able to acheive full read performance out of most SSDs.

Benchmark
----

Here is an example from the included micro-benchmark run on an Intel 530 series 240GB SSD over an 8GB logical address space. Maximum iops is reached immediately and is sustained until the read buffer size surpasses the disk page size of 4K, when iops roughly halfs and latency roughly doubles with each double in buffer size. Meanwhile, throughput continues to increase, with diminishing returns.

    backend reqs    bufsize depth   rtime   utime   stime   cpu     us/op   op/s    MB/s
    kaio    262144  512     32      5632    199     1860    2060    686     46544   22.73
    kaio    262144  1024    32      5518    170     1756    1926    672     47503   46.39
    kaio    262144  2048    32      5535    138     1534    1673    675     47356   92.49
    kaio    131072  4096    32      2829    114     829     943     689     46324   180.95
    kaio    131072  8192    32      4849    72      910     982     1183    27025   211.14
    kaio    131072  16384   32      7386    65      999     1065    1802    17744   277.25
    kaio    65536   32768   32      5688    18      557     575     2776    11520   360.02
    kaio    65536   65536   32      9228    61      791     852     4504    7101    443.84
    kaio    65536   131072  32      17365   142     1606    1749    8476    3773    471.75
    kaio    32768   262144  32      17025   57      1182    1240    16616   1924    481.15

The Pthread backend here is configured to run with 32 parallel I/O threads.

    backend reqs    bufsize depth   rtime   utime   stime   cpu     us/op   op/s    MB/s
    pthread 262144  512     32      5669    1017    1996    3014    690     46239   22.58
    pthread 262144  1024    32      5676    993     2050    3044    691     46179   45.10
    pthread 262144  2048    32      5841    964     2079    3043    711     44875   87.65
    pthread 131072  4096    32      2835    525     1022    1547    690     46222   180.56
    pthread 131072  8192    32      5057    506     875     1381    1233    25917   202.48
    pthread 131072  16384   32      10199   632     858     1490    2488    12850   200.79
    pthread 65536   32768   32      5534    206     587     793     2700    11841   370.06
    pthread 65536   65536   32      9244    253     676     929     4511    7089    443.07
    pthread 65536   131072  32      17135   442     1057    1499    8363    3824    478.06
    pthread 32768   262144  32      16974   219     856     1076    16566   1930    482.61

These numbers appear to surpass the published [performance specifications][intel_perf] for the drive, which use [Iometer][iometer] over the same 8GB logical address space and with a queue depth of 32 (# of in-flight requests). Iometer has not yet been run with these settings on the same drive.

API
---

[ioqueue.h][ioqueue.h]

Development Notes
----

The included benchmark is the best usage example. The [`ioqueue_bench()`][ioqueue_bench] function contains the ioqueue API calls.

The benchmark generates `REQUEST` read requests of size `BUFSIZE` each with a random offset aligned to `BUFSIZE`. Each request is queued, using `ioqueue_pread()`, which takes a file descriptor, an output buffer, a buffer length, the file offset, and a callback. The file descriptor must be open for reading with flag `O_DIRECT` and the callback will be executed asynchronously on the current thread during some call to `ioqueue_reap`.

The function `ioqueue_reap()` checks the queue for completed requests and executes their callback functions, returning the number of requests processed, which may be 0, or -1 on error. A non-zero `min` parameter, if specified, will cause the function to block until `min` requests have completed (KAIO), or at least 1 request has completed (Pthreads).

In the benchmark, the reap is executed after each pread with a `min` parameter of either 0, when fewer requests are in flight than the depth of the I/O queue, or 1. It is assumed that minimizing request latency is a priority and that reaping an empty queue is a fast operation (no syscall). For the KAIO backend this feature is provided by libaio. For the Pthread backend, an empty queue implies no lock contention. Once the maximum number of in-flight requests is reached, the value 1 is passed and the reap will block until one or more requests have completed, thus freeing some space in the queue.

The API is single-threaded and is intended to be used in a single process with no threads, or via a single I/O manager thread. The I/O itself is asynchronous so this main process/thread can do other work while the queue is full and `ioqueue_reap(0)` returns 0. Once the I/O is complete, callback functions are executed on main thread during the next call to `ioqueue_reap()`.

On the KAIO backend, there is support for using `poll()` to detect I/O readiness. The file descriptor returned from `ioqueue_eventfd()` will receive `POLL_IN/OUT/ERR` notifications when individual requests have completed or failed. This is less efficient than directly reaping requests in a data-driven program, but may be useful for e.g. a network server that already processes events on many file descriptors via polling. In this case the server may process other network I/O events as they occurs instead of blocking during `ioqueue_reap()` for the completion of disk I/O.

[odirect]: http://man7.org/linux/man-pages/man2/open.2.html
[AIO]: https://web.archive.org/web/20150406015143/http://code.google.com/p/kernel/wiki/AIOUserGuide
[intel_perf]: http://www.intel.com/content/www/us/en/solid-state-drives/solid-state-drives-530-series.html
[iometer]: http://www.iometer.org/
[ioqueue.h]: ioqueue.h
[ioqueue_bench]: perf/bench.cc#L170
