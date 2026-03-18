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

#include "ob_cdc_util.h"

namespace oceanbase
{
namespace cdc
{
int ObExtRpcQit::init(const int64_t deadline)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(deadline <= BASE_DEADLINE)) {
    ret = OB_INVALID_ARGUMENT;
    EXTLOG_LOG(WARN, "invalid deadline", K(ret), K(deadline));
  } else {
    deadline_ = deadline;
  }
  return ret;
}

bool ObExtRpcQit::should_hurry_quit() const
{
  bool bool_ret = false;

  if (OB_LIKELY(common::OB_INVALID_TIMESTAMP != deadline_)) {
    int64_t now = ObTimeUtility::current_time();
    bool_ret = (now > deadline_ - RESERVED_INTERVAL);
  }

  return bool_ret;
}
} // namespace cdc
} // namespace oceanbase
