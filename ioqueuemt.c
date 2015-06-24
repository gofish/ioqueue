#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include "ioqueue.h"

enum ioqueue_op {
    ioqueue_OP_PREAD,
};

struct ioqueue_request {
    enum ioqueue_op op;
    int fd;
    ioqueue_cb cb;
    void *cb_arg;
    union {
        struct {
            void *buf;
            ssize_t x;
            off_t off;
        } rw;
    } u;
};

struct ioqueue_queue {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct ioqueue_request *reqs;
    unsigned short head;       /* the first request in the queue */
    unsigned short done;       /* the number of requests that have been completed */
    unsigned short size;       /* the total number of requests on the queue */
    unsigned short wait;       /* the thread needs a signal when reaped */
};

static unsigned int _depth;
static unsigned int _nqueue;
static int _running;

static struct ioqueue_queue *_queues = NULL;
static pthread_mutex_t _reap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _reap_cond = PTHREAD_COND_INITIALIZER;

static int
ioqueue_request_push(struct ioqueue_queue *queue, const struct ioqueue_request *req)
{
    int ret;
    pthread_mutex_lock(&queue->lock);

    if (queue->size < _depth) {
        /* there is space on the queue - append to the tail */
        queue->reqs[(queue->head + queue->size++) % _depth] = *req;
        ret = 0;
    } else {
        /* no space on the queue - temporary failure */
        errno = EAGAIN;
        ret = -1;
    }

    pthread_mutex_unlock(&queue->lock);
    return ret;
}

static struct ioqueue_request *
ioqueue_request_next(struct ioqueue_queue *queue, int done)
{
    struct ioqueue_request *req;
    pthread_mutex_lock(&queue->lock);

    if (done) {
        /* register completion of the last request handled */
        if (!queue->done++) {
            /* first request completed, main thread could be waiting */
            pthread_mutex_unlock(&queue->lock);
            pthread_mutex_lock(&_reap_lock);
            pthread_cond_signal(&_reap_cond);
            pthread_mutex_unlock(&_reap_lock);
            pthread_mutex_lock(&queue->lock);
        }
    }

    /* wait for at least one request on the queue */
    while (!queue->size || queue->done == queue->size) {
        if (!_running) break;
        queue->wait = 1;
        pthread_cond_wait(&queue->cond, &queue->lock);
    }
    /* return the next request ready for processing */
    if (_running) {
        req = &queue->reqs[(queue->head + queue->done) % _depth];
    } else {
        req = NULL;
    }

    pthread_mutex_unlock(&queue->lock);
    return req;
}

static int
ioqueue_request_take(struct ioqueue_queue *queue, struct ioqueue_request *req)
{
    int ret;
    pthread_mutex_lock(&queue->lock);

    /* signal when thread is waiting for ready work */
    if (queue->wait && queue->size != queue->done) {
        queue->wait = 0;
        pthread_cond_signal(&queue->cond);
    }

    /* pop request from the queue */
    if (queue->done) {
        ret = 1;
        *req = queue->reqs[queue->head];
        queue->head = (queue->head + 1) % _depth;
        --queue->done;
        --queue->size;
    } else if (queue->size) {
        ret = -1;
        errno = EAGAIN;
    } else {
        ret = 0;
    }

    pthread_mutex_unlock(&queue->lock);
    return ret;
}

static void *
ioqueue_thread_run(void *tdata)
{
    struct ioqueue_request *req;
    struct ioqueue_queue *const queue = &_queues[(int)(unsigned long)tdata];

    req = ioqueue_request_next(queue, 0);
    while (req) {
        /* process the request */
        switch (req->op) {
        case ioqueue_OP_PREAD:
            req->u.rw.x = pread(req->fd, req->u.rw.buf, req->u.rw.x, req->u.rw.off);
            break;

        default:
            /* unreachable */
            abort();
        }

        /* pull the next request off the queue */
        req = ioqueue_request_next(queue, 1);
    }

    pthread_exit(NULL);
}

static void
ioqueue_stop_wait()
{
    int i;
    /* flip the switch */
    _running = 0;
    /* signal any waiting threads */
    for (i = 0; i < _nqueue; ++i) {
        pthread_mutex_lock(&_queues[i].lock);
        pthread_cond_signal(&_queues[i].cond);
        pthread_mutex_unlock(&_queues[i].lock);
    }
    /* wait and cleanup */
    for (i = 0; i < _nqueue; ++i) {
        pthread_join(_queues[i].thread, NULL);
        free(_queues[i].reqs);
    }
}

static int
ioqueue_threads_start(pthread_attr_t *attr)
{
    int i;
    int err;
    struct ioqueue_queue *queue;
    /* flip the switch */
    _running = 1;
    /* create threads */
    for (i = 0; i < _nqueue; ++i) {
        queue = &_queues[i];
        pthread_mutex_init(&queue->lock, NULL);
        pthread_cond_init(&queue->cond, NULL);
        queue->reqs = malloc(_depth * sizeof(struct ioqueue_request));
        queue->head = 0;
        queue->done = 0;
        queue->size = 0;
        if (queue->reqs == NULL) {
            err = errno;
            break;
        }
        err = pthread_create(&queue->thread, attr, &ioqueue_thread_run, (void*)(unsigned long)i);
        if (err) break;
    }
    if (err) {
        /* an error occurred, exit existing threads */
        _nqueue = i;
        ioqueue_stop_wait();
        errno = err;
        return -1;
    }
    return 0;
}

/* initiliaze the io queue to the given maximum outstanding requests */
int
ioqueue_init(unsigned int depth)
{
    int err;
    pthread_attr_t attr;
    if (_queues) {
        errno = EINVAL;
        return -1;
    }
    _depth = depth;
    _nqueue = depth;
    _queues = calloc(_nqueue, sizeof(_queues[0]));
    if (!_queues) {
        return -1;
    }
    err = pthread_attr_init(&attr);
    if (err) {
        free(_queues);
        errno = err;
        return -1;
    }
    err = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if (err) {
        pthread_attr_destroy(&attr);
        free(_queues);
        errno = err;
        return -1;
    }
    err = ioqueue_threads_start(&attr);
    pthread_attr_destroy(&attr);
    if (err) {
        free(_queues);
        errno = err;
        return -1;
    }
    return 0;
}

/* retrieve a file descrptor suitable for io readiness notifications via e.g. poll/epoll */
int
ioqueue_eventfd()
{
    errno = ENOTSUP;
    return -1;
}

static unsigned int _next_queue = 0;

/* enqueue a pread request  */
int
ioqueue_pread(int fd, void *buf, size_t len, off_t offset, ioqueue_cb_pread cb, void *cb_arg)
{
    int ret;
    unsigned int tries;
    struct ioqueue_request req;

    req.op = ioqueue_OP_PREAD;
    req.fd = fd;
    req.cb = (ioqueue_cb) cb;
    req.cb_arg = cb_arg;
    req.u.rw.buf = buf;
    req.u.rw.x = len;
    req.u.rw.off = offset;

    for (tries = 0; tries < _nqueue; tries ++) {
        ret = ioqueue_request_push(&_queues[_next_queue], &req);
        _next_queue = (_next_queue + 1) % _nqueue;
        if (!ret) return 0;
    }
    return -1;
}

/* submit requests and handle completion events */
int
ioqueue_reap(unsigned int min)
{
    int r;
    int flag_wait;
    int flag_loop;
    unsigned int i, j, n;
    struct ioqueue_request req;

    pthread_mutex_lock(&_reap_lock);

    n = 0;
    flag_wait = 0;
    do {
        flag_loop = 0;
        for (i = 0, j = 0; i < _nqueue; i++) {
            r = ioqueue_request_take(&_queues[i], &req);
            if (r == 1) {
                /* we took a request */
                ++n; /* count the request so we will not wait later */
                flag_loop = 1; /* try to take another */

                /* release lock and perform callback */
                pthread_mutex_unlock(&_reap_lock);
                switch (req.op) {
                case ioqueue_OP_PREAD:
                    (* (ioqueue_cb_pread) req.cb)(req.cb_arg, req.u.rw.x, req.u.rw.buf);
                    break;
                default:
                    /* unreachable */
                    abort();
                }
                /* reacquire reap lock */
                pthread_mutex_lock(&_reap_lock);

            } else if (r == -1 && errno == EAGAIN) {
                /* there is at least one request queued, so wait will not deadlock */
                flag_wait = 1;
            }
        }
        if (!n && flag_wait && min) {
            /* there is at least one request enqueued */
            /* since we have reaped no requests, wait for it */
            pthread_cond_wait(&_reap_cond, &_reap_lock);
            flag_loop = 1;
        }
    } while (flag_loop);

    pthread_mutex_unlock(&_reap_lock);
    return n;
}

/* reap all requests and destroy the queue */
void
ioqueue_destroy()
{
    while (ioqueue_reap(1)) { }
    ioqueue_stop_wait();
    free(_queues);
}
