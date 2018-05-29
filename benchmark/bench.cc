#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <utility>
#include <vector>
#include "../ioqueue.h"

#ifndef RANDSTATE
#define RANDSTATE 64
#endif
#ifndef IOQ_BACKEND
#define IOQ_BACKEND "kaio"
#endif
#ifndef IOQ_OPEN_FLAGS
#define IOQ_OPEN_FLAGS (O_RDONLY | O_DIRECT)
#endif

using namespace std;

static int VERBOSE;
static int Q_DEPTH;
static int BUFSIZE;
static int REQUESTS;
static int RANDSEED;

static vector<void *> _buffers;
static vector<string> _config_help;

#define ENVOPT(var, def, help) \
do { \
    var = getenv(#var) ? atoi(getenv(#var)) : (def); \
    if (VERBOSE) fprintf(stderr, "%-8s = %d\n", #var, var); \
    _config_help.push_back(#var ": " help " (default " #def ")\n"); \
} while (0);

static void
env_init()
{
    ENVOPT(VERBOSE, 0, "print config options at start");
    ENVOPT(Q_DEPTH, 20, "kaio or pthread queue depth");
    ENVOPT(BUFSIZE, 512, "write buffer size");
    ENVOPT(REQUESTS, 262144, "number of requests to execute");
    ENVOPT(RANDSEED, 0, "seed for random number generator");
}

void
usage(FILE *fp, const char *me)
{
    fprintf(fp, "usage: %s <path>..\n\n", me);
    fprintf(fp, "  Environment:\n");
    for (size_t i = 0; i < _config_help.size(); i++) {
        fprintf(fp, "    %s", _config_help[i].c_str());
    }
}

void
init_buffers()
{
    for (int i = 0; i < Q_DEPTH; i++) {
        _buffers.push_back(NULL);
        int ret = posix_memalign(&_buffers[i], 512, BUFSIZE);
        if (ret != 0) {
            fprintf(stderr, "posix_memalign: %s\n", strerror(ret));
            exit(EXIT_FAILURE);
        }
    }
}

void
free_buffers()
{
    for (unsigned int i = 0; i < _buffers.size(); i++) {
        free(_buffers[i]);
    }
}

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

int64_t
timestamp()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
    return (int64_t)(tp.tv_sec) * 1000000000L + tp.tv_nsec;
}

int64_t
timevalue(struct timeval tv)
{
    return (int64_t)(tv.tv_sec) * 1000000000L + (int64_t)(tv.tv_usec) * 100L;
}

int64_t _time_wait_total = 0;

void
aio_callback(void *closure, ssize_t result, void *buf)
{
    // fail benchmark on read error
    if (result < 0) {
        fprintf(stderr, "pread: %s", strerror(-(int)result));
        exit(EXIT_FAILURE);
    }
    // track total request latency
    _time_wait_total += timestamp() - (int64_t)(closure);
    // return buffer to free pool
    _buffers.push_back(buf);
}

vector< pair<int, off_t> > _files;

void
open_files(char **argv)
{
    int ret;
    struct stat st;
    for (char **path = argv + 1; *path; path++) {
        int fd = open(*path, IOQ_OPEN_FLAGS);
        if (fd == -1) {
            fprintf(stderr, "%s: open(%s, %d): %s\n", *argv, *path, IOQ_OPEN_FLAGS, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ret = fstat(fd, &st);
        if (ret == -1) {
            fprintf(stderr, "%s: fstat(%s): %s", *argv, *path, strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (!S_ISREG(st.st_mode) || st.st_size == 0) {
            fprintf(stderr, "%s: not a regular non-empty file: %s\n", *argv, *path);
            exit(EXIT_FAILURE);
        }
        /* explicitly drop existing file caches */
        if (0 != posix_fadvise(fd, 0, st.st_size, POSIX_FADV_DONTNEED)) {
            fprintf(stderr, "%s: posix_fadvise(%s, 0, %lld, %d): %s\n", *argv,
                    *path, (unsigned long long) st.st_size, POSIX_FADV_DONTNEED,
                    strerror(errno));
        }
#ifdef IOQ_FADV_POLICY
        /* advise the kernel of our access pattern */
        if (0 != posix_fadvise(fd, 0, st.st_size, IOQ_FADV_POLICY)) {
            fprintf(stderr, "%s: posix_fadvise(%s, 0, %lld, %d): %s\n", *argv,
                    *path, (unsigned long long) st.st_size, IOQ_FADV_POLICY,
                    strerror(errno));
        }
#endif
        _files.push_back(make_pair(fd, st.st_size / BUFSIZE * BUFSIZE));
    }
}

pair<int, off_t>
next_read_request(struct random_data *rdata)
{
    union {
        int32_t r[2];
        uint64_t val;
    } res;
    random_r(rdata, &res.r[0]);
    random_r(rdata, &res.r[1]);
    // use the low order bits to pick a random file descriptor and length from those opened
    size_t i = (size_t)((res.val & (BUFSIZE - 1)) % (uint64_t)(_files.size()));
    pair<int, off_t> f = _files[i];
    // use the high order bits as a random offset with BUFSIZE alignment within the length of the file
    f.second = (off_t)((res.val & ~(BUFSIZE - 1)) % (uint64_t)(f.second));
    return f;
}

void
close_files()
{
    for (unsigned int i = 0; i < _files.size(); i++) {
        close(_files[i].first);
    }
}

void
ioqueue_bench()
{
    int ret;
    char rstate[RANDSTATE];
    struct random_data rdata;

    /* initialize the RNG */
    memset(&rdata, 0, sizeof(rdata));
    initstate_r(RANDSEED, rstate, sizeof(rstate), &rdata);

    /* initialize an aio context */
    ret = ioqueue_init(Q_DEPTH);
    if (ret == -1) {
        perror("ioqueue_init");
        exit(EXIT_FAILURE);
    }

    /* queue all the requests */
    for (int i = 0; i < REQUESTS; ) {
        while (!_buffers.empty()) {
            /* generate a random read request */
            const pair<int, off_t> req = next_read_request(&rdata);

            /* take the next available buffer */
            void *const buf = _buffers.back();
            _buffers.pop_back();

            /* record the start time as a pointer (TODO: pass actual pointer to timestamp) */
            void *const closure = (void *)(timestamp());

            /* enqueue the read request -- non-blocking */
            ret = ioqueue_pread(req.first, buf, BUFSIZE, req.second, &aio_callback, closure);
            if (ret == -1) {
                perror("ioqueue_pread");
                exit(EXIT_FAILURE);
            }
            i++;
        }

        /* reap completed requests -- blocking, as no buffers remain */
        ret = ioqueue_reap(1);
        if (ret == -1) {
            perror("ioqueue_reap");
            exit(EXIT_FAILURE);
        }
    }

    /* reap all requests and destroy the queue */
    ioqueue_destroy();
}

int
main(int argc, char **argv)
{
    int64_t time_start;
    int64_t time_total;
    int64_t time_cpu_user;
    int64_t time_cpu_system;

    struct rusage rusage_start;
    struct rusage rusage_finish;

    /* initialize global variables from env */
    env_init();
    if (argc < 2) {
        usage(stderr, *argv);
        exit(EXIT_FAILURE);
    }

    /* open input files and allocate buffers */
    open_files(argv);
    init_buffers();

    /* record start time */
    time_start = timestamp();

    /* record cpu usage at start */
    getrusage(RUSAGE_SELF, &rusage_start);

    /* run the benchmark */
    ioqueue_bench();

    /* record cpu usage at finish */
    getrusage(RUSAGE_SELF, &rusage_finish);
    time_cpu_user = timevalue(rusage_finish.ru_utime) - timevalue(rusage_start.ru_utime);
    time_cpu_system = timevalue(rusage_finish.ru_stime) - timevalue(rusage_start.ru_stime);

    /* record finish time */
    time_total = timestamp() - time_start;

    /* report throughput and average request latency */
    fprintf(stderr, "backend         reqs    bufsize depth   rtime   utime   stime   cpu     us/op   op/s    MB/s\n");
    fprintf(stdout, "%-15s ", IOQ_BACKEND);
    fprintf(stdout, "%-7d ", REQUESTS);
    fprintf(stdout, "%-7d ", BUFSIZE);
    fprintf(stdout, "%-7d ", Q_DEPTH);
    fprintf(stdout, "%-7lld ", (long long)((double)time_total / 1e6));
    fprintf(stdout, "%-7lld ", (long long)((double)time_cpu_user / 1e3));
    fprintf(stdout, "%-7lld ", (long long)((double)time_cpu_system / 1e3));
    fprintf(stdout, "%-7lld ", (long long)((double)(time_cpu_user + time_cpu_system) / 1e3));
    fprintf(stdout, "%-7lld ", (long long)((double)_time_wait_total / 1e3 / REQUESTS));
    fprintf(stdout, "%-7lld ", (long long)(REQUESTS / ((double)_time_wait_total / 1e9)));
    fprintf(stdout, "%-7.2f ", ((double)BUFSIZE * REQUESTS / (1 << 20)) / ((double)_time_wait_total / 1e9));
    fprintf(stdout, "\n");

    /* close input files and exit*/
    close_files();
    free_buffers();
    exit(EXIT_SUCCESS);
}
