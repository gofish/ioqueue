ioqueue
====

Fast random-access asynchronous I/O operations in C.

Building
----

Use `make`.

Licence
----

The library is a small wrapper around each of two backends, Linux kernal AIO (kaio) and POSIX threads (pthreads). The former is release under the terms of the LGPL version 3 or greater, due to the direct use of linux kernel APIs, and the latter is released under the MIT license, provided it is linked against a compatible pthread implementation. All reads and write must be executed on block boundaries in multiples of 512 bytes

Synopsis
----

The high random-I/O performance of solid-state drives is a relatively novel development in computing that is potentially applicable to a large number of problems.

One approach to maximize performance of an SSD is to use posix threads to execute parallel requests via direct I/O file descriptors. Direct I/O will bypass the kernel buffer cache and execute read and writes directly to the SSD controller. Using threads is necessary, since file I/O is *always* blocking (except, below) and direct I/O will cause a disk read on every call. However, it is also necessary to use many threads in order to saturate the SSD controller, which is backed by several parallel, and individually slow, NANDs.

The exception to blocking direct I/O, in Linux, is the kernal [AIO](https://code.google.com/p/kernel/wiki/AIOUserGuide) interface. These syscalls provide user-space a low-level interface to queue, reap, and poll asynchronous direct I/O requests. In writing and benchmarking `ioqueue` for random reads I found that KAIO on average could match or beat the throughput of the pthread implementation while decreasing overall CPU usage and request latency. Both backends are able to acheive full read performance out of most SSDs.

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

The pthread backend here is configured to run with 32 parallel I/O threads.

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

These numbers appear to surpass the published [performance specifications](http://www.intel.com/content/www/us/en/solid-state-drives/solid-state-drives-530-series.html) for the drive, which use [Iometer](http://www.iometer.org/) over the same 8GB logical address space and with a queue depth of 32 (# of in-flight requests).

API
---

[ioqueue.h](/pub/scm/?p=ioqueue.git;a=blob_plain;f=ioqueue.h)

Development Notes
----

The library only supports read operations for the moment. A matching write API, via `ioqueue_write()`, is planned for development.
