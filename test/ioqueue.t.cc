#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "../ioqueue.h"

#ifndef TEST_NAME
#define TEST_NAME(name) IOQueue ## name
#endif

#ifndef HAVE_KAIO
#define HAVE_KAIO 1
#endif

#ifndef HAVE_EVENTFD
#define HAVE_EVENTFD 1
#endif

TEST(TEST_NAME(InitTest), InitTest) {
    int ret;
    EXPECT_EQ(-1, (ret = ioqueue_init(0))) << "ioqueue_init: " << strerror(errno);
    if (ret == 0) {
        ioqueue_destroy();
        FAIL();
    };
    for (int i = 0; i < 13; i++) {
        ASSERT_EQ(0, (ret = ioqueue_init(1 << i))) << "ioqueue_init: " << strerror(errno);
        if (ret == 0) {
            ioqueue_destroy();
        }
    }
    ASSERT_EQ(0, (ret = ioqueue_init(1))) << "ioqueue_init: " << strerror(errno);
    EXPECT_EQ(-1, (ret = ioqueue_init(1))) << "ioqueue_init: " << strerror(errno);
#if HAVE_EVENTFD
    EXPECT_NE(-1, ioqueue_eventfd());
#else
    EXPECT_EQ(-1, ioqueue_eventfd());
#endif
    ioqueue_destroy();
}

static const int BUFSIZE = 4096;

class TEST_NAME(TestClass) : public ::testing::Test {
  public:
    static const int DEPTH = 32;

  protected:
    virtual void SetUp() {
        // initialize an aligned memory buffer
        ASSERT_EQ(0, posix_memalign((void **)&buf_, 512, BUFSIZE)) << "posix_memalign: " << strerror(errno);
        memset(buf_, 0, BUFSIZE);
        res_ = 0;
        // initialize the ioqueue library
        ASSERT_EQ(0, ioqueue_init(DEPTH)) << "ioqueue_init: " << strerror(errno);
        // create and open a temporary test file
        strcpy(path_, P_tmpdir "/ioqueue.tmp.XXXXXX");
        fd_ = mkstemp(path_);
        ASSERT_NE(-1, fd_) << "mkstemp: " << strerror(errno);
        close(fd_);
        fd_ = open(path_, O_RDWR | O_DIRECT);
        ASSERT_NE(-1, fd_) << "open: " << strerror(errno);
        unlink(path_);
    }

    virtual void TearDown() {
        if (buf_) {
            ioqueue_destroy();
            close(fd_);
            free(buf_);
        }
        buf_ = NULL;
    }

    static void Callback(void *arg, ssize_t res, void *buf) {
        ((TEST_NAME(TestClass) *) arg)->res_ = res;
    }

    int fd_;
    char path_[256];
    char *buf_;
    int res_;
};

TEST_F(TEST_NAME(TestClass), ReadTest) {
    buf_[512] = 1;
    ASSERT_EQ(BUFSIZE, pwrite(fd_, buf_, BUFSIZE, 0)) << "pwrite: " << strerror(errno);

    res_ = 0;
    memset(buf_, 0, BUFSIZE);
    ASSERT_EQ(0, ioqueue_pread(fd_, buf_, 512, 0, &Callback, this)) << "ioqueue_pread: " << strerror(errno);
    ASSERT_EQ(1, ioqueue_reap(1));
    ASSERT_EQ(512, res_);
    ASSERT_EQ(0, memcmp(buf_, buf_ + 1024, 512));

    res_ = 0;
    memset(buf_, 0, BUFSIZE);
    ASSERT_EQ(0, ioqueue_pread(fd_, buf_, 512, 512, &Callback, this));
    ASSERT_EQ(1, ioqueue_reap(1));
    ASSERT_EQ(512, res_);
    ASSERT_EQ(1, buf_[0]);
    ASSERT_EQ(0, memcmp(buf_ + 1, buf_ + 1024, 511));
}

TEST_F(TEST_NAME(TestClass), WriteTest) {
    res_ = 0;
    buf_[250] = 1;
    ASSERT_EQ(0, ioqueue_pwrite(fd_, buf_, BUFSIZE, 0, &Callback, this));
    ASSERT_EQ(1, ioqueue_reap(1));
    ASSERT_EQ(BUFSIZE, res_);

    memset(buf_, 0, BUFSIZE);
    ASSERT_EQ(BUFSIZE, pread(fd_, buf_, BUFSIZE, 0));
    ASSERT_EQ(1, buf_[250]);
}

TEST_F(TEST_NAME(TestClass), BadReapTest)
{
    ASSERT_EQ(-1, ioqueue_reap(0));
    ASSERT_EQ(-1, ioqueue_reap(1));
    ASSERT_EQ(0, ioqueue_pread(fd_, buf_, BUFSIZE, 0, &Callback, this));
    ASSERT_EQ(-1, ioqueue_reap(2));
    ASSERT_EQ(1, ioqueue_reap(1));
}

TEST_F(TEST_NAME(TestClass), ReapOnDestroyTest)
{
    ASSERT_EQ(0, ioqueue_pwrite(fd_, buf_, BUFSIZE, 0, &Callback, this));
    TearDown();
    ASSERT_EQ(res_, BUFSIZE);
}

TEST_F(TEST_NAME(TestClass), FullQueueTest)
{
    for (int i = 0; i < DEPTH; i++) {
        ASSERT_EQ(0, ioqueue_pread(fd_, buf_, BUFSIZE, 0, &Callback, this));
    }
    ASSERT_EQ(-1, ioqueue_pread(fd_, buf_, BUFSIZE, 0, &Callback, this));
}
