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

#include "ob_vector_add.h"
namespace oceanbase
{
namespace common
{
int ObVectorAdd::calc(float *a, float *b, const int64_t len)
{
  int ret = OB_SUCCESS;
#if OB_USE_MULTITARGET_CODE
  if (common::is_arch_supported(ObTargetArch::AVX512)) {
    ret = common::specific::avx512::vector_add(a, b, len);
  } else if (common::is_arch_supported(ObTargetArch::AVX2)) {
    ret = common::specific::avx2::vector_add(a, b, len);
  } else if (common::is_arch_supported(ObTargetArch::AVX)) {
    ret = common::specific::avx::vector_add(a, b, len);
  } else if (common::is_arch_supported(ObTargetArch::SSE42)) {
    ret = common::specific::sse42::vector_add(a, b, len);
  } else {
    ret = common::specific::normal::vector_add(a, b, len);
  }
#else
  ret = common::specific::normal::vector_add(a, b, len);
#endif
  return ret;
}
}  // namespace common
}  // namespace oceanbase
