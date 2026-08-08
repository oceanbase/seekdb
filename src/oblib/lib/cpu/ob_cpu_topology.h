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

#ifndef OCEANBASE_LIB_OB_CPU_TOPOLOGY_
#define OCEANBASE_LIB_OB_CPU_TOPOLOGY_

#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/utility.h"

#include "lib/container/ob_bit_set.h"

namespace oceanbase
{
namespace common
{

int64_t get_cpu_count();

enum class CpuFlag { SSE4_2 = 0, AVX, AVX2, AVX512BW, NEON, MAX };
class CpuFlagSet {
  public:
  DISABLE_COPY_ASSIGN(CpuFlagSet);
  bool have_flag(const CpuFlag flag) const;
  CpuFlagSet();
private:
  void init_from_cpu(uint64_t& flags);
  int init_from_os(uint64_t& flags);
  int64_t flags_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_OB_CPU_TOPOLOGY_
