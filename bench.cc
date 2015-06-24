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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <string>
#include <utility>
#include <vector>
#include "ioqueue.h"

#ifndef RANDSTATE
#define RANDSTATE 64
#endif
#ifndef IOQ_BACKEND
#define IOQ_BACKEND "kaio"
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
    for (int i = 0; i < _config_help.size(); i++) {
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

unsigned long
timestamp()
{
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
    return tp.tv_sec * 1000000000L + tp.tv_nsec;
}

unsigned long _start_time;
unsigned long _cumul_time;

void
aio_callback(void *closure, ssize_t result, void *buf)
{
    unsigned long start, finish;
    if (result < 0) {
        fprintf(stderr, "pread: %s", strerror(-result));
        exit(EXIT_FAILURE);
    }
    finish = timestamp();
    start = (unsigned long)closure;
    // track total request latency
    _cumul_time += finish - start;
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
        int fd = open(*path, O_RDONLY | O_DIRECT);
        if (fd == -1) {
            fprintf(stderr, "%s: open(%s): %s\n", *argv, *path, strerror(errno));
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
        _files.push_back(make_pair(fd, st.st_size / BUFSIZE * BUFSIZE));
    }
}

pair<int, off_t>
next_read_request(struct random_data *rdata)
{
    union {
        int r[2];
        long val;
    } res;
    random_r(rdata, &res.r[0]);
    random_r(rdata, &res.r[1]);
    // random file descriptor from those opened
    int i = (res.val & (BUFSIZE - 1)) % _files.size();
    pair<int, off_t> f = _files[i];
    // random offset with BUFSIZE alignment
    f.second = ((res.val & ~(BUFSIZE - 1)) % f.second);
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
            /* take a buffer and generate a random read request */
            const pair<int, off_t> req = next_read_request(&rdata);
            void *const buf = _buffers.back();
            _buffers.pop_back();

            /* enqueue the read request -- non-blocking */
            ret = ioqueue_pread(req.first, buf, BUFSIZE, req.second, &aio_callback, (void *)timestamp());
            if (ret == -1) {
                perror("ioqueue_pread");
                exit(EXIT_FAILURE);
            }
            i++;
        }

        /* reap completed requests -- blocking, if no buffers remain */
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
    unsigned long total_time, total_size, cpu_user, cpu_system;
    struct rusage rusage_start, rusage_finish;

    /* initialize global variables from env */
    env_init();
    if (argc < 2) {
        usage(stderr, *argv);
        exit(EXIT_FAILURE);
    }

    /* open input files and allocate buffers */
    open_files(argv);
    init_buffers();

    /* record start time and cpu usage */
    _start_time = timestamp();
    _cumul_time = 0;
    getrusage(RUSAGE_SELF, &rusage_start);

    /* run the benchmark */
    ioqueue_bench();

    /* calculate throughput and average request latency */
    total_time = timestamp() - _start_time;
    getrusage(RUSAGE_SELF, &rusage_finish);
    total_size = (unsigned long)BUFSIZE * REQUESTS;
    cpu_user = (rusage_finish.ru_utime.tv_sec - rusage_start.ru_utime.tv_sec) * 1e6 +
                (rusage_finish.ru_utime.tv_usec - rusage_start.ru_utime.tv_usec);
    cpu_system = (rusage_finish.ru_stime.tv_sec - rusage_start.ru_stime.tv_sec) * 1e6 +
                (rusage_finish.ru_stime.tv_usec - rusage_start.ru_stime.tv_usec);

    fprintf(stderr, "backend reqs    bufsize depth   rtime   utime   stime   cpu     us/op   op/s    MB/s\n");
    fprintf(stdout, "%-7s ", IOQ_BACKEND);
    fprintf(stdout, "%-7d ", REQUESTS);
    fprintf(stdout, "%-7d ", BUFSIZE);
    fprintf(stdout, "%-7d ", Q_DEPTH);
    fprintf(stdout, "%-7lu ", (unsigned long)(total_time / 1e6));
    fprintf(stdout, "%-7lu ", (unsigned long)(cpu_user / 1e3));
    fprintf(stdout, "%-7lu ", (unsigned long)(cpu_system / 1e3));
    fprintf(stdout, "%-7lu ", (unsigned long)((cpu_user + cpu_system) / 1e3));
    fprintf(stdout, "%-7lu ", (unsigned long)(_cumul_time / 1e3 / REQUESTS));
    fprintf(stdout, "%-7lu ", (unsigned long)(REQUESTS / (total_time / 1e9)));
    fprintf(stdout, "%-7.2f ", total_size / (total_time / 1e9) / (1 << 20));
    fprintf(stdout, "\n");

    /* close input files and exit*/
    close_files();
    free_buffers();
    exit(EXIT_SUCCESS);
}
