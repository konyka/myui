/**
 * @file my_osal.h
 * @brief OS abstraction for POSIX-ish PAL ports (linux_fb today).
 *
 * Every syscall a port needs goes through this table, so unit tests can
 * inject a scripted fake (memory framebuffer + canned input events)
 * while production uses my_osal_default().
 */
#ifndef MY_OSAL_H
#define MY_OSAL_H

#include "myc/my_types.h"

/** @brief poll() event flags (subset). */
#define MY_OSAL_POLLIN 0x01

/** @brief Simplified pollfd. */
typedef struct my_osal_pollfd_t {
  int fd;
  short events;
  short revents;
} my_osal_pollfd_t;

/** @brief OS call table. */
typedef struct my_osal_t {
  int (*open)(void* ctx, const char* path, int flags);
  int (*close)(void* ctx, int fd);
  int (*ioctl)(void* ctx, int fd, unsigned long request, void* arg);
  void* (*mmap)(void* ctx, size_t length, int fd);
  int (*munmap)(void* ctx, void* addr, size_t length);
  /** @brief Returns number of ready fds, 0 on timeout, < 0 on error. */
  int (*poll)(void* ctx, my_osal_pollfd_t* fds, int nfds, int timeout_ms);
  long (*read)(void* ctx, int fd, void* buf, size_t count);
  void* ctx;
} my_osal_t;

/** @brief Real POSIX implementation (Linux builds). */
const my_osal_t* my_osal_default(void);

#endif /* MY_OSAL_H */
