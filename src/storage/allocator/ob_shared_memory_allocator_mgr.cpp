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

#define USING_LOG_PREFIX SHARE

#include "ob_shared_memory_allocator_mgr.h"

namespace oceanbase {
namespace share {

#define THROTTLE_CONFIG_LOG(ALLOCATOR, LIMIT, TRIGGER_PERCENTAGE, MAX_DURATION) \
          "Unit Name",                                                          \
          ALLOCATOR::throttle_unit_name(),                                      \
          "Memory Limit(MB)",                                                   \
          LIMIT / 1024 / 1024,                                                  \
          "Throttle Trigger(MB)",                                               \
          LIMIT * trigger_percentage / 100 / 1024 / 1024,                       \
          "Trigger Percentage",                                                 \
          TRIGGER_PERCENTAGE,                                                   \
          "Max Alloc Duration",                                                 \
          MAX_DURATION

// moved definition to storage ob_tenant_freezer.cpp

}  // namespace share
}  // namespace oceanbase
