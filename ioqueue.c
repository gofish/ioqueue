#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <linux/aio_abi.h>
#include <sys/eventfd.h>
#include "ioqueue.h"

/** KAIO l-value helpers **/
/* the request file operation */
#define IOCB_OP(iocbp)       (*(unsigned short*)&((iocbp)->aio_lio_opcode))
/* the request file descriptor */
#define IOCB_FD(iocbp)         (*(unsigned int*)&((iocbp)->aio_fildes))
/* the request buffer */
#define IOCB_BUF(iocbp)               (*(void**)&((iocbp)->aio_buf))
/* the request length */
#define IOCB_LEN(iocbp)              (*(size_t*)&((iocbp)->aio_nbytes))
/* the request file offset */
#define IOCB_OFF(iocbp)               (*(off_t*)&((iocbp)->aio_offset))
/* the request closure data */
#define IOCB_DATA(iocbp)              (*(void**)&((iocbp)->aio_data))
/* the request flags */
#define IOCB_FLAGS(iocbp)      (*(unsigned int*)&((iocbp)->aio_flags))
/* the request eventfd */
#define IOCB_RESFD(iocbp)      (*(unsigned int*)&((iocbp)->aio_resfd))
/* the event closure data */
#define IOEV_DATA(ioev)               (*(void**)&(ioev)->data)

/* KAIO syscalls - wrappers provided by libaio */
extern int io_setup(int depth, aio_context_t *ctxp);
extern int io_destroy(aio_context_t ctx);
extern int io_submit(aio_context_t ctx, long nr, struct iocb *ios[]);
extern int io_cancel(aio_context_t ctx, struct iocb *iocbp, struct io_event *evp);
extern int io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events, struct timespec *timeout);

/**
 * ioqueue request closure
 *   Contains reference to the callback, the callback closure, and the
 *   KAIO struct iocb that is passed to the kernel.  The struct iocb in
 *   turn contains a pointer back to the struct ioqueue_request.
 */
struct ioqueue_request {
    ioqueue_cb cb;
    void *cb_data;
    struct iocb iocb; /* IO_DATA(&request.iocb) == (void*)&request */
};

/** global variables **/

/* KAIO request buffer, when not in-flight, as passed to io_submit()
 *   - the array head is a queue of requests yet to be submitted
 *   - the array tail is a stack of completed and unused requests
 */
static struct iocb **_io_reqs;
/* KAIO event buffer for completed I/O events, as received from io_getevents() */
static struct io_event *_io_evs;
/* KAIO context - opaque integer handle */
static aio_context_t _ctx = 0;
static int _depth;      /* maximum outstanding requests */
static int _nreqs;      /* allocated request objects */
static int _nfree;      /* free request stack size */
static int _nwait;      /* waiting request stack size */
static int _eventfd;    /* eventfd(2) for poll/epoll */


/* initiliaze the io queue to the given maximum outstanding requests */
int ioqueue_init(unsigned int depth)
{
    int ret;
    _io_reqs = malloc(depth * sizeof(struct iocb *));
    if (_io_reqs == NULL) {
        return -1;
    }
    _io_evs = malloc(depth * sizeof(struct io_event));
    if (_io_evs == NULL) {
        free(_io_reqs);
        return -1;
    }
    ret = io_setup(depth, &_ctx);
    if (ret < 0) {
        free(_io_reqs);
        free(_io_evs);
        errno = -ret;
        return -1;
    }
    _depth = depth;
    _nreqs = 0;
    _nfree = 0;
    _nwait = 0;
    _eventfd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    return 0;
}

/* retrieve a file descrptor suitable for io readiness notifications via e.g. poll/epoll */
int ioqueue_eventfd()
{
    return _eventfd;
}

/* allocate (or retrieve) a request object */
static struct ioqueue_request * ioqueue_request_alloc()
{
    struct ioqueue_request *req;
    if (_nfree > 0) {
        /* pop a request from the tail free-stack */
        req = IOCB_DATA(_io_reqs[_depth - (_nfree--)]);
    } else if (_nreqs < _depth) {
        /* allocate a new request */
        req = malloc(sizeof(struct ioqueue_request));
        if (req == NULL) return NULL;
        _nreqs++;
    } else {
        /* queue overflow */
        errno = EAGAIN;
        return NULL;
    }
    /* clear request and set self pointer */
    memset(req, 0, sizeof(struct ioqueue_request));
    IOCB_DATA(&req->iocb) = req;
    /* push onto the head wait-queue */
    _io_reqs[_nwait++] = &req->iocb;
    return req;
}

/* free a request */
static void ioqueue_request_free(struct ioqueue_request *req)
{
    /* push onto the tail free-stack */
    _io_reqs[_depth - (++_nfree)] = &req->iocb;
}

/* enqueue a pread request  */
int ioqueue_pread(int fd, void *buf, size_t len, off_t offset, ioqueue_cb cb, void *cb_data)
{
    struct ioqueue_request *req;
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    req = ioqueue_request_alloc();
    if (req == NULL) return -1;

    req->cb = (ioqueue_cb) cb;
    req->cb_data = cb_data;
    IOCB_OP(&req->iocb) = IOCB_CMD_PREAD;
    IOCB_FD(&req->iocb) = fd;
    IOCB_BUF(&req->iocb) = buf;
    IOCB_LEN(&req->iocb) = len;
    IOCB_OFF(&req->iocb) = offset;
    if (_eventfd != -1) {
        IOCB_FLAGS(&req->iocb) |= IOCB_FLAG_RESFD;
        IOCB_RESFD(&req->iocb) = _eventfd;
    }
    return 0;
}

/* submit as many requests as possible from the front of the queue */
static int ioqueue_submit()
{
    int i;
    int ret;
    for (i = 0; i < _nwait; i += ret) {
        ret = io_submit(_ctx, _nwait - i, _io_reqs + i);
        if (ret < 0) {
            /* ensure wait-queue occupies the head of the array */
            memmove(_io_reqs, _io_reqs + i, _nwait - i);
            _nwait -= i;
            errno = -ret;
            return -1;
        }
    }
    _nwait -= i;
    return i;
}

/* fetch and process any completed requests */
int ioqueue_reap(unsigned int min)
{
    int ret, i;
    struct ioqueue_request *req;

    /* cannot wait for more requests than have been submitted */
    if (_nfree == _nreqs || min > _nreqs - _nfree) {
        errno = EINVAL;
        return -1;
    }

    /* ensure some requests have been submitted */
    ret = ioqueue_submit();
    if (ret == -1) return ret;

    /* block for at least 'min' completion events */
    ret = io_getevents(_ctx, min, _depth, _io_evs, NULL);
    if (ret <= 0) return ret;

    for (i = 0; i < ret; i++) {
        req = IOEV_DATA(&_io_evs[i]);
        if (_io_evs[i].res < 0) {
            /* set errno for callback */
            errno = _io_evs[i].res2;
        }
        /* run callback */
        switch (IOCB_OP(&req->iocb)) {
        case IOCB_CMD_PREAD:
            (* (ioqueue_cb) req->cb)(req->cb_data, _io_evs[i].res, IOCB_BUF(&req->iocb));
            break;
        default:
            /* unreachable */
            abort();
        }
        /* push free'd request onto tail-stack */
        ioqueue_request_free(req);
    }
    /* return the number of completed requests */
    return ret;
}

void ioqueue_destroy()
{
    while (_nfree != _nreqs) {
        /* assume latency matters -- block for requests one at a time */
        ioqueue_reap(1);
    }
    free(_io_evs);
    while (_nfree > 0) {
        free(IOCB_DATA(_io_reqs[_depth - _nfree]));
        _io_reqs[_depth - _nfree] = 0;
        _nfree--;
        _nreqs--;
    }
    free(_io_reqs);
    io_destroy(_ctx);
}
