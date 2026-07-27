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

#ifndef OCEANBASE_SHARE_DEADLOCK_OB_LCL_SCHEME_OB_LCL_PARAMETERS_H
#define OCEANBASE_SHARE_DEADLOCK_OB_LCL_SCHEME_OB_LCL_PARAMETERS_H
#include <stdint.h>

namespace oceanbase
{
namespace share
{
namespace detector
{

constexpr const int64_t LOCAL_TIMER_JITTER = 50L * 1000L;// 50ms
constexpr const int64_t LOCAL_DISPATCH_BUDGET = 40L * 1000L;// 40ms
constexpr const int64_t LOCAL_PROPAGATION_INTERVAL = 10L * 1000L;// 10ms
constexpr const int64_t MIN_CYCLE_SIZE_SUPPORTED = 10;// 10 nodes
constexpr const int64_t LOCAL_RANDOM_DELAY_RANGE = 100L * 1000L;// 100ms
constexpr const int64_t TIMER_SCHEDULE_RESERVE_TIME = 1L * 1000L;// 1ms
constexpr const int64_t PHASE_TIME = 2 * LOCAL_TIMER_JITTER +
                                     LOCAL_RANDOM_DELAY_RANGE +
                                     (LOCAL_DISPATCH_BUDGET + LOCAL_PROPAGATION_INTERVAL) *
                                     MIN_CYCLE_SIZE_SUPPORTED;// 700ms
constexpr const int64_t PERIOD = 2 * PHASE_TIME;// 1.4s
constexpr const int64_t COMMON_BLOCK_SIZE = 3;// assume element number in block list not more than 3
                                              // in most scenarios
}// namespace detector
}// namespace share
}// namespace oceanbase
#endif
