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

#define USING_LOG_PREFIX PALF
#include "log_io_context.h"

namespace oceanbase
{
namespace palf
{
LogIOContext::LogIOContext()
    : palf_id_(0),
      user_()
{
}

LogIOContext::LogIOContext(const LogIOUser &user)
    : palf_id_(0),
      user_(user)
{
  const bool allow_filling_cache = is_enable_fill_cache_user_();
  iterator_info_.set_allow_filling_cache(allow_filling_cache);
}

LogIOContext::LogIOContext(const uint64_t tenant_id, const int64_t palf_id, const LogIOUser &user)
    : palf_id_(palf_id),
      user_(user)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(tenant_id <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), K(tenant_id));
  } else {
    // it's not allowed to fill cache when reading for FETCHLOG, RESTART, META_INFO
    const bool allow_filling_cache = is_enable_fill_cache_user_();
    iterator_info_.set_allow_filling_cache(allow_filling_cache);
  }
}

} // namespace palf
} // namespace oceanbase
