/// @file
/// @brief C analog of C++’s `std::optional`

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/// a container that may or may not contain a value
#define OPTIONAL(type)                                                         \
  struct {                                                                     \
    bool has_value; /**< does this have a value? */                            \
    type value_;    /**< the value if `has_value` is true */                   \
  }

/// set the value of an optional
///
/// This utility macro is intended to avoid the easy typo of setting the
/// value while forgetting to set the `has_value` member.
///
/// @param me The optional whose value to set
/// @param value The value to assign
#define OPTIONAL_SET(me, value)                                                \
  do {                                                                         \
    assert((me) != NULL);                                                      \
    (me)->has_value = true;                                                    \
    (me)->value_ = (value);                                                    \
  } while (0)

/// get the value of an optional
///
/// The caller must know the optional has a value before calling this.
///
/// @param me The optional whose value to retrieve
/// @return Value of the optional
#define OPTIONAL_VALUE(me)                                                     \
  (((me).has_value ? (void)0                                                   \
                   : (fprintf(stderr,                                          \
                              "%s:%d: internal error: attempted to read the "  \
                              "value of an empty optional type\n",             \
                              __FILE__, __LINE__),                             \
                      abort())),                                               \
   (me).value_)

/// get the value of an optional or a given value if the optional is empty
///
/// @param me The optional whose value to retrieve
/// @param fallback The value to return if the optional is empty
/// @return Value of the optional or `fallback` if it was empty
#define OPTIONAL_VALUE_OR(me, fallback)                                        \
  ((me).has_value ? (me).value_ : (fallback))
