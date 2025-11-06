/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __EASY_MOD_STAT__
#define __EASY_MOD_STAT__

#include <stdint.h>
#include <sys/types.h>
#include "easy_define.h"
EASY_CPP_START

typedef struct mod_stat_t {
  uint64_t id;
  int64_t count;
  int64_t size;
} mod_stat_t;

extern __thread mod_stat_t* easy_cur_mod_stat;

EASY_CPP_END
#endif /* __EASY_MOD_STAT__ */
