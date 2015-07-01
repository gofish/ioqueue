#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "../ioqueue.h"

TEST(IOQueueInitTest, InitTest) {
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
}

static const int BUFSIZE = 4096;

class IOQueueTest : public ::testing::Test {
  protected:
    virtual void SetUp() {
        // initialize the ioqueue library
        ioqueue_init(32);
        // initialize an aligned memory buffer
        ASSERT_EQ(0, posix_memalign((void **)&buf_, 512, BUFSIZE)) << "posix_memalign: " << strerror(errno);
        memset(buf_, 0, BUFSIZE);
        res_ = 0;
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
        ioqueue_destroy();
        close(fd_);
        free(buf_);
    }

    static void Callback(void *arg, ssize_t res, void *buf) {
        ((IOQueueTest *) arg)->res_ = res;
    }

    int fd_;
    char path_[256];
    char *buf_;
    int res_;
};

TEST_F(IOQueueTest, ReadTest) {
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

TEST_F(IOQueueTest, WriteTest) {
    res_ = 0;
    buf_[250] = 1;
    ASSERT_EQ(0, ioqueue_pwrite(fd_, buf_, BUFSIZE, 0, &Callback, this));
    ASSERT_EQ(1, ioqueue_reap(1));
    ASSERT_EQ(BUFSIZE, res_);

    memset(buf_, 0, BUFSIZE);
    ASSERT_EQ(BUFSIZE, pread(fd_, buf_, BUFSIZE, 0));
    ASSERT_EQ(1, buf_[250]);
}
