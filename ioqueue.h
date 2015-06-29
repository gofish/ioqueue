#ifndef _ioqueue_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ioqueue_cb)(void *arg, ssize_t res, void *buf);

/* initialize the io queue to the given maximum outstanding requests */
int  ioqueue_init(unsigned int depth);

/* retrieve a file descriptor suitable for io readiness notifications via e.g. poll/epoll */
int  ioqueue_eventfd();

/* enqueue a pread request  */
int  ioqueue_pread(int fd, void *buf, size_t len, off_t offset, ioqueue_cb cb, void *cb_arg);

/* submit requests and handle completion events */
int  ioqueue_reap(unsigned int min);

/* reap all requests and destroy the queue */
void ioqueue_destroy();

#ifdef __cplusplus
}
#endif

#endif
