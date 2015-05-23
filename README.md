ioqueue
====

Fast random-access asynchronous I/O operations in C.

Building
----

Use [`bake`](/pub/scm/?p=bake.git;a=summary).

Licence
----

The library is a small wrapper around each of two backends, Linux kernal AIO (kaio) and POSIX threads (pthreads).  The former is release under the terms of the LGPL version 3 or greater, due to the direct use of linux kernel APIs, and the latter is released under the MIT license, provided it is linked against a compatible pthread implementation.  All reads and write must be executed on block boundaries in multiples of 512 bytes

Synopsis
----

The high random-I/O performance of solid-state drives is a relatively novel development in computing that is potentially applicable to a large number of problems.

One approach to maximize performance of an SSD is to use posix threads to execute parallel requests via direct I/O file descriptors. Direct I/O will bypass the kernel buffer cache and execute read and writes directly to the SSD controller. Using threads is necessary, since file I/O is *always* blocking (except, below) and direct I/O will cause a disk read on every call. However, it is also necessary to use many threads in order to saturate the SSD controller, which is backed by several parallel, and individually slow, NANDs.

The exception to blocking direct I/O, in Linux, is the kernal [AIO](https://code.google.com/p/kernel/wiki/AIOUserGuide) interface. These syscalls provide user-space a low-level interface to queue, reap, and poll asynchronous direct I/O requests.  In writing and benchmarking `ioqueue` for random reads I found that KAIO on average could match or beat the throughput of the pthread implementation while decreasing overall CPU usage and request latency.  Both backends are able to acheive full read performance out of most SSDs.

Benchmark
----

Here is an example from the included micro-benchmark run on an Intel 530 series 240GB SSD. Maximum iops is reached immediately using the KAIO backend with a queue depth of 32 requests, until the read buffer size surpasses the disk block size of 4K, when iops halfs with each double in buffer size.  Meanwhile, throughput continues to increase even until the 64K buffer size.

    backend reqs    bufsize depth   rtime   utime   stime   cpu     us/op   op/s    MB/s
    kaio    262144  512     32      5134    193     1599    1793    619     51053   24.93   
    kaio    262144  1024    32      5064    210     1678    1888    617     51762   50.55   
    kaio    262144  2048    32      5078    189     1540    1729    619     51622   100.83  
    kaio    131072  4096    32      2489    106     720     826     607     52642   205.64  
    kaio    131072  8192    32      4321    67      847     915     1054    30327   236.93  
    kaio    131072  16384   32      7745    119     1324    1444    1890    16922   264.41  
    kaio    65536   32768   32      5979    43      627     671     2918    10959   342.49  
    kaio    65536   65536   32      9675    108     1066    1175    4722    6773    423.35  

The pthread backend here is configure to run with 32 parallel I/O threads.  Increasing the thread count will achieve higher
performance with only slightly more lock and CPU contention.

    backend reqs    bufsize depth   rtime   utime   stime   cpu     us/op   op/s    MB/s
    pthread 262144  512     32      5837    794     1819    2614    711     44906   21.93   
    pthread 262144  1024    32      5575    663     1947    2611    679     47013   45.91   
    pthread 262144  2048    32      5770    737     1879    2616    703     45430   88.73   
    pthread 131072  4096    32      2805    399     930     1330    683     46722   182.51  
    pthread 131072  8192    32      4454    255     920     1175    1086    29422   229.87  
    pthread 131072  16384   32      8186    564     651     1215    1997    16011   250.18  
    pthread 65536   32768   32      6193    141     533     675     3022    10581   330.66  
    pthread 65536   65536   32      9893    161     558     720     4828    6624    414.01  

**Caveat:** These numbers appear to surpass the published [performance specifications](http://www.intel.com/content/www/us/en/solid-state-drives/solid-state-drives-530-series.html) for the drive. This could be hedging by Intel, or an issue with my measurements, or possibly the size of the logical block range. If you spot an error please contact me! I will test again soon with an 8GB block range.

API
---

[ioqueue.h](/pub/scm/?p=ioqueue.git;a=blob_plain;f=ioqueue.h)

Development Notes
----

The library only supports read operations for the moment.  A matching write API, via `ioqueue_write()`, is planned for development.
