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

#ifndef OCEABASE_STORAGE_OB_LS_SWITCH_CHECKER_
#define OCEABASE_STORAGE_OB_LS_SWITCH_CHECKER_
#include <stdint.h>

namespace oceanbase
{
namespace storage
{
class ObLS;

class ObLSSwitchChecker
{
public:
  ObLSSwitchChecker() : ls_(nullptr), record_switch_epoch_(UINT64_MAX) {}
  int check_ls_switch_state(ObLS *ls, bool &is_online);
  int double_check_epoch(bool &is_online) const;
private:
  ObLS *ls_;
  uint64_t record_switch_epoch_;
};

}
}
#endif
