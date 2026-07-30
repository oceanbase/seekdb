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

#include <algorithm>

#include "share/ob_cpu_share_calculator.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{

namespace common
{

int64_t ObCpuShareCalculator::calc_px_pool_share(int64_t min_cpu)
{
  return std::max(static_cast<int64_t>(3), min_cpu * GCONF.px_workers_per_cpu_quota);
}

} // namespace common
} // namespace oceanbase
