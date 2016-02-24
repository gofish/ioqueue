ioqueue
====

Asynchronous direct I/O operations in C.

Building
----

Use `make`.

Requirements for KAIO backend (`make libioqueue.a`):

* Linux >= 2.6.22 ?
* libaio
* librt

Licence
----

The library is a small wrapper around each of two backends, Linux kernel AIO (kaio) and POSIX threads (Pthreads). The former is released under the terms of the LGPL version 3 or greater and uses the GPLv3 libaio. The latter is released under the 3-clause BSD license, provided it is linked against a compatible Pthreads implementation.

Overview
----

One approach to minimize kernel page caching over an SSD that backs an alternatively cached keyspace is to use direct I/O. Direct I/O will bypass the page cache and fault reads and writes to the SSD controller. Using threads to execute parallel requests is therefore necessary to prevent blocking, since regular file I/O always blocks on faults. Parallel requests also saturate the SSD controller, which is backed by several individually slower NANDs. There are many complaints by Linux developers about the direct I/O interface, however, and using threads has its own drawbacks. Are we forced to use direct I/O? Are we forced to use threads?

When only reads are concerned and the user does not need to disable write buffering then a viable alternative is to use
`posix_fadvise`. The advice parameter `POSIX_FADV_DONTNEED` can be used to drop regions of pages from the cache, while
`POSIX_FADV_NOREUSE` can acheive identical behavior to direct I/O on reads -- do not cache any pages on reads because they will only be accessed once. Meanwhile, the O\_DSYNC flag to `open(2)` may provide synchronous write behavior analogous to O\_DIRECT. Write performance has not been tested but preliminary testing on reads under threads shows NOREUSE is a drop-in replacement for O\_DIRECT.

Another option that offers potential improvements by eliminating threads is the kernel [AIO][AIO] interface (not to be confused with POSIX AIO). Regular file I/O, as mentioned, is always blocking on faults, but the AIO interface is different. These syscalls provide users an interface to queue, reap, and poll asynchronous direct I/O requests, without threads and without signals. As a result the direct I/O operations may be executed fully asynchronously, with fewer syscalls, on a single core, and with no user space lock contention.

Notes
----

This library implements two backends, one threaded and one using KAIO. When using the Linux KAIO backend file descriptors are required to be opened with flag [**O\_DIRECT**][odirect]. The threaded backend may be used with O\_DIRECT, or e.g. with POSIX\_FADV\_NOREUSE. Applications will likely incur lower CPU usage using the KAIO backend.

Benchmark
----

Below is the output of the micro-benchmark run on over 8GB of logical address space on an Intel 530 series 240GB SSD. Further analysis should show that maximum iops is reached immediately and is sustained until the read buffer size surpasses the disk page size of 4K, when iops decreases and latency increases but throughput continues to increase, with diminishing returns.

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

For comparison here the [Pthread backend][ioqueuemt.c] is configured to run with 32 parallel I/O threads.

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

The primary difference is the amount of user-space cpu time (utime) required, as would be expected vs. a userspace-lock-free design. An implementation of a pthread-backed lock-free I/O queue would be useful for further comparison, but as it stands the AIO queue has proved its worth, especially for integration with pthread-free binaries.

API
---

[ioqueue.h][ioqueue.h]

The included benchmark is the best usage example. The [`ioqueue_bench()`][ioqueue_bench] function contains the ioqueue API calls.

The API is single-threaded and is intended to be used in a single process with no threads, or via a single I/O manager thread. The I/O itself is asynchronous and will not begin to execute until after the next call to `ioqueue_reap()`.

On the KAIO backend, there is support for using `poll()` to detect I/O readiness. The file descriptor returned from `ioqueue_eventfd()` will receive `POLL_IN/OUT/ERR` notifications when individual requests have completed or failed.

[odirect]: http://man7.org/linux/man-pages/man2/open.2.html
[AIO]: https://web.archive.org/web/20150406015143/http://code.google.com/p/kernel/wiki/AIOUserGuide
[intel_perf]: http://www.intel.com/content/www/us/en/solid-state-drives/solid-state-drives-530-series.html
[iometer]: http://www.iometer.org/
[ioqueue.h]: ioqueue.h
[ioqueue_bench]: perf/bench.cc#L170
