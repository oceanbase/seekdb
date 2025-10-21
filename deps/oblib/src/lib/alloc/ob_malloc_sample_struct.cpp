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

#include "lib/alloc/ob_malloc_sample_struct.h"

namespace oceanbase
{
namespace lib
{
#if defined(__x86_64__)
int32_t ObMallocSampleLimiter::min_sample_size = 16384;
#else
int32_t ObMallocSampleLimiter::min_sample_size = 0;
#endif
bool malloc_sample_allowed(const int64_t size, const ObMemAttr &attr)
{
  return ObMallocSampleLimiter::malloc_sample_allowed(size, attr);
}
} // end of namespace lib
} // end of namespace oceanbase
