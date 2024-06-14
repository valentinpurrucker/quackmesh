/*
Copyright (c) 2023 Valentin Purrucker. All rights reserved.

This work is licensed under the terms of the MIT license.
For a copy, see <https://opensource.org/licenses/MIT> or
the LICENSE file.
*/

#pragma once

#ifndef QUACK_DEBUG_LEVEL
#define QUACK_DEBUG_LEVEL 3
#endif

#define DEBUG_LEVEL_INFO 0
#define DEBUG_LEVEL_ERR 1
#define DEBUG_LEVEL_WARN 2
#define DEBUG_LEVEL_DEBUG 3

#ifdef QuackDebug
#define DEBUG(level, message)                 \
  do {                                        \
    if (QUACK_DEBUG_LEVEL == level) {         \
      Serial.printf("%s", message);           \
    }                                         \
  } while (0)

#define FDEBUG(level, format, ...)        \
  do {                                    \
    if (QUACK_DEBUG_LEVEL == level) {     \
      Serial.printf(format, __VA_ARGS__); \
    }                                     \
  } while (0)
#else
#define DEBUG(level, message)
#define FDEBUG(level, format, ...)
#endif
