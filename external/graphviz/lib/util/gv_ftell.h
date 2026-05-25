/// @file
/// @brief Abstraction over `ftell`

#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/// `ftell`, accounting for platform limitations
static inline int64_t gv_ftell(FILE *stream) {
  assert(stream != NULL);

#ifdef _WIN32
  // on Windows, `long` is 32 bits so `ftell` cannot report >2GB file sizes
  return _ftelli64(stream);
#endif

  return ftell(stream);
}
