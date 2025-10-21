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

#ifndef OCEANBASE_COMMON_OB_ABORT_H_
#define OCEANBASE_COMMON_OB_ABORT_H_
#include <stdlib.h>
#include "lib/utility/ob_macro_utils.h"

extern void ob_abort (void) __THROW;

// we use int instead of bool to compatible with c code.
#ifdef __cplusplus
static inline void abort_unless(bool result)
#else
static inline void abort_unless(int result)
#endif
{
  if (OB_UNLIKELY(!result)) {
    ob_abort();
  }
}
#ifdef ENABLE_DEBUG_ASSERT
#define DEBUG_ASSERT(result) abort_unless(result)
#else
#define DEBUG_ASSERT(result)
#endif

#endif // OCEANBASE_COMMON_OB_ABORT_H_
