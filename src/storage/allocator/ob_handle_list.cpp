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

#include "ob_handle_list.h"

namespace oceanbase
{
namespace common
{
void ObHandleList::init_handle(Handle& handle)
{
  handle.reset();
  total_list_.add(&handle.total_list_);
  ATOMIC_AAF(&total_count_, 1);
}

void ObHandleList::destroy_handle(Handle& handle)
{
  set_frozen(handle);
  total_list_.del(&handle.total_list_);
  ATOMIC_AAF(&total_count_, -1);
}

void ObHandleList::set_active(Handle& handle)
{
  if (handle.set_active()) {
    active_list_.add(&handle.active_list_, handle);
    update_hazard();
  }
  handle.set_id(alloc_id());
}

void ObHandleList::set_frozen(Handle& handle)
{
  if (handle.is_active()) {
    active_list_.del(&handle.active_list_);
    update_hazard();
  }
  handle.set_frozen();
}

void ObHandleList::update_hazard()
{
  ATOMIC_STORE(&hazard_, calc_hazard());
}

int64_t ObHandleList::calc_hazard()
{
  int64_t x = INT64_MAX;
  DLink* last = active_list_.tail_.prev_;
  if (&active_list_.head_ != last) {
    Handle* handle = CONTAINER_OF(last, Handle, active_list_);
    x = handle->get_clock();
  }
  COMMON_LOG(TRACE, "HandleList.calc_hazard", K(x));
  return x;
}

}; // end namespace common
}; // end namespace oceanbase
