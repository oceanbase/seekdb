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

#pragma once

#include <stdint.h>

namespace oceanbase
{
namespace share
{

// Only select the default budget here. ObShareUtil::set_default_timeout_ctx
// continues to give an inherited context or worker deadline precedence.
constexpr int64_t get_rs_default_timeout_us(const int64_t rpc_timeout_us)
{
#if defined(__APPLE__) || defined(_WIN32)
  // Buffered startup I/O can exceed the normal two-second RPC budget on
  // these platforms, including the initial __all_global_stat update.
  const int64_t slow_io_timeout_us = 10LL * 1000 * 1000;
  return rpc_timeout_us < slow_io_timeout_us ? slow_io_timeout_us : rpc_timeout_us;
#else
  return rpc_timeout_us;
#endif
}

} // namespace share
} // namespace oceanbase
