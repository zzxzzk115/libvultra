#pragma once

#include "graphviz_version.h"

#define DEFAULT_DPI 96
#define WITH_CGRAPH 1

#if defined(_WIN32)
#define WIN32 1
#define HAVE_IO_H 1
#else
#define HAVE_UNISTD_H 1
#define HAVE_STRINGS_H 1
#endif

#define HAVE_STDINT_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_MATH_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_EXPAT 1
