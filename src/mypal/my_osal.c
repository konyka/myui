/**
 * @file my_osal.c
 * @brief Real POSIX implementation of my_osal_t.
 */
/* POSIX (open/ioctl/mmap/poll/read) under strict -std=c99 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "mypal/my_osal.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int os_open(void* ctx, const char* path, int flags) {
  (void)ctx;
  return open(path, flags);
}

static int os_close(void* ctx, int fd) {
  (void)ctx;
  return close(fd);
}

static int os_ioctl(void* ctx, int fd, unsigned long request, void* arg) {
  (void)ctx;
  return ioctl(fd, request, arg);
}

static void* os_mmap(void* ctx, size_t length, int fd) {
  (void)ctx;
  return mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

static int os_munmap(void* ctx, void* addr, size_t length) {
  (void)ctx;
  return munmap(addr, length);
}

static int os_poll(void* ctx, my_osal_pollfd_t* fds, int nfds, int timeout_ms) {
  struct pollfd pfd;
  int ret;
  (void)ctx;
  if (nfds != 1) {
    return -1; /* ports here poll a single fd */
  }
  pfd.fd = fds[0].fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  ret = poll(&pfd, 1, timeout_ms);
  fds[0].revents = (pfd.revents & POLLIN) != 0 ? MY_OSAL_POLLIN : 0;
  return ret;
}

static long os_read(void* ctx, int fd, void* buf, size_t count) {
  ssize_t n;
  (void)ctx;
  n = read(fd, buf, count);
  return (long)n;
}

const my_osal_t* my_osal_default(void) {
  static const my_osal_t os = {os_open,  os_close, os_ioctl, os_mmap,
                               os_munmap, os_poll,  os_read,  NULL};
  return &os;
}
