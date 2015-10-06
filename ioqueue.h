#ifndef _ioqueue_H
#define _ioqueue_H

// ioqueue.h - ioqueue library API
//
// Copyright (c) 2015  Jeremy R. Fishman
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the <organization> nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

/* enqueue a pwrite request  */
int  ioqueue_pwrite(int fd, void *buf, size_t len, off_t offset, ioqueue_cb cb, void *cb_arg);

/* submit requests and handle completion events */
int  ioqueue_reap(unsigned int min);

/* reap all requests and destroy the queue */
void ioqueue_destroy();

#ifdef __cplusplus
}
#endif

#endif
