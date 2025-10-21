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
 
#include "ob_ls_switch_checker.h"
#include "ob_ls.h"

namespace oceanbase
{
namespace storage
{


int ObLSSwitchChecker::check_ls_switch_state(ObLS *ls, bool &is_online)
{
  int ret = OB_SUCCESS;
  ls_ = ls;
  if (OB_ISNULL(ls)) {
    ret = OB_BAD_NULL_ERROR;
  } else {
    record_switch_epoch_ = ATOMIC_LOAD(&(ls_->switch_epoch_));
    if (!(record_switch_epoch_ & 1)) {
      is_online = false;
    } else {
      is_online = true;
    }
  }
  return ret;
}

int ObLSSwitchChecker::double_check_epoch(bool &is_online) const
{
  int ret = OB_SUCCESS;
  int64_t switch_state = 0;
  if (OB_ISNULL(ls_)) {
    ret = OB_NOT_INIT;
  } else if (FALSE_IT(switch_state = ATOMIC_LOAD(&(ls_->switch_epoch_)))) {
  } else if (OB_UNLIKELY(record_switch_epoch_ != switch_state)) {
    ret = OB_VERSION_NOT_MATCH;
    is_online = (switch_state & 1) ? false : true;
  }
  return ret;
}

}
}
