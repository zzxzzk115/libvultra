/// @file
/// @brief C implementation of `gv_fopen`

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <util/gv_fopen.h>
#include <util/streq.h>

/// platform abstraction over `fopen`
static FILE *fopen_(const char *filename, const char *mode) {
  assert(filename != NULL);
  assert(mode != NULL);
#ifdef _MSC_VER
  {
    FILE *result = NULL;
    if (fopen_s(&result, filename, mode) != 0) {
      return NULL;
    }
    return result;
  }
#else
  return fopen(filename, mode);
#endif
}

FILE *gv_fopen(const char *filename, const char *mode) {
  assert(filename != NULL);
  assert(mode != NULL);
  assert(streq(mode, "r") || streq(mode, "rb") || streq(mode, "w") ||
         streq(mode, "wb"));

  // does `fopen` support 'e' for setting close-on-exec?
  bool have_e = false;

  // does `fopen` support 'N' for setting close-on-exec?
  bool have_n = false;

#if defined(__DragonFly__) || defined(__FreeBSD__) || defined(__linux__) ||    \
    defined(__NetBSD__) || defined(__OpenBSD__)
  have_e = true;
#endif

#ifdef _MSC_VER
  have_n = true;
#endif

  // Attempt 1: `fopen`+'e'
  if (have_e) {
    char mode_with_cloexec[4] = {0};
    snprintf(mode_with_cloexec, sizeof(mode_with_cloexec), "%se", mode);
    return fopen_(filename, mode_with_cloexec);
  }

  // Attempt 2: `fopen`+'N'
  if (have_n) {
    char mode_with_cloexec[4] = {0};
    snprintf(mode_with_cloexec, sizeof(mode_with_cloexec), "%sN", mode);
    return fopen_(filename, mode_with_cloexec);
  }

  // Attempt 3: `open`+`O_CLOEXEC`; `fdopen` (e.g. for macOS)
#ifdef O_CLOEXEC
  {
    int flags = mode[0] == 'w' ? (O_WRONLY | O_CREAT | O_TRUNC) : O_RDONLY;
#ifdef O_BINARY
    if (strchr(mode, 'b') != NULL) {
      flags |= O_BINARY;
    }
#endif
#ifdef _O_BINARY
    if (strchr(mode, 'b') != NULL) {
      flags |= _O_BINARY;
    }
#endif
    const int fd = open(filename, flags | O_CLOEXEC, 0666);
    if (fd < 0) {
      return NULL;
    }

    FILE *f = fdopen(fd, mode);
    if (f == NULL) {
      const int err = errno;
      (void)close(fd);
      errno = err;
    }
    return f;
  }
#endif

  // Attempt 4: `fopen`; `fcntl`+`FD_CLOEXEC`
  // We are on a platform that supported none of the above, so we need to
  // fallback on an unmodified `fopen`. The platform either does not have a
  // concept of close-on-exec or does not support a race-free way of achieving
  // it.

  FILE *f = fopen_(filename, mode);
  if (f == NULL) {
    return NULL;
  }

#ifdef FD_CLOEXEC
  {
    const int fd = fileno(f);
    const int flags = fcntl(fd, F_GETFD);
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
      const int err = errno;
      (void)fclose(f);
      errno = err;
      return NULL;
    }
  }
#endif

  return f;
}
