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

#include "ob_append_callback.h"

namespace oceanbase
{
namespace logservice
{
AppendCb* AppendCbBase::__get_class_address(ObLink *ptr)
{
  return NULL != ptr ? CONTAINER_OF(ptr, AppendCb, __next_) : NULL;
}
ObLink* AppendCbBase::__get_member_address(AppendCb *ptr)
{
  return NULL != ptr ? reinterpret_cast<ObLink*>(ADDRESS_OF(ptr, AppendCb, __next_)) : NULL;
}

void AppendCb::set_cb_first_handle_ts(const int64_t ts)
{
  if (OB_INVALID_TIMESTAMP != cb_first_handle_ts_) {
    cb_first_handle_ts_ = ts;
  }
}

} // end namespace logservice
} // end napespace oceanbase
