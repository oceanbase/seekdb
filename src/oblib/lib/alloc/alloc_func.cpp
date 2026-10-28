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

#include "alloc_func.h"
#include <atomic>

using namespace oceanbase;
using namespace oceanbase::lib;

namespace oceanbase
{
namespace lib
{

namespace
{
std::atomic<int64_t> g_memory_budget(DEFAULT_MEMORY_BUDGET);
}

void set_memory_budget(int64_t bytes)
{
  g_memory_budget.store(bytes, std::memory_order_release);
}

int64_t get_memory_budget()
{
  return g_memory_budget.load(std::memory_order_acquire);
}

} // end of namespace lib
} // end of namespace oceanbase
