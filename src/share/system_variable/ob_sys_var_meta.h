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

#ifndef OCEANBASE_SHARE_SYSTEM_VARIABLE_OB_SYS_VAR_META_
#define OCEANBASE_SHARE_SYSTEM_VARIABLE_OB_SYS_VAR_META_
#include "share/system_variable/ob_sys_var_class_type.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{

class ObSysVarMeta
{
public:
  const static int64_t MYSQL_SYS_VARS_COUNT = 99;
  const static int64_t OB_SYS_VARS_COUNT = 642;
  const static int64_t ALL_SYS_VARS_COUNT = MYSQL_SYS_VARS_COUNT + OB_SYS_VARS_COUNT;
  const static int64_t INVALID_MAX_READ_STALE_TIME = -1;

  const static int16_t OB_SPECIFIC_SYS_VAR_ID_OFFSET = 10000;
  // Represents the maximum value of sys var id that OB can currently use. Under normal circumstances, there is no need to apply for sys var id greater than OB_MAX_SYS_VAR_ID,
  // If you need to apply for sys var id greater than OB_MAX_SYS_VAR_ID, you need to adjust the value of ob_max_sys_var_id first
  const static int32_t OB_MAX_SYS_VAR_ID = 20000;

  static ObSysVarClassType find_sys_var_id_by_name(const common::ObString &sys_var_name, bool is_from_sys_table = false); //binary search
  static int calc_sys_var_store_idx(ObSysVarClassType sys_var_id, int64_t &store_idx);
  static int calc_sys_var_store_idx_by_name(const common::ObString &sys_var_name, int64_t &store_idx);
  static bool is_valid_sys_var_store_idx(int64_t store_idx);
  static int get_sys_var_name_by_id(ObSysVarClassType sys_var_id, common::ObString &sys_var_name);
  static const common::ObString get_sys_var_name_by_id(ObSysVarClassType sys_var_id);

private:
  static bool sys_var_name_case_cmp(const char *name1, const common::ObString &name2);
  const static char *SYS_VAR_NAMES_SORTED_BY_NAME[ALL_SYS_VARS_COUNT];
  const static ObSysVarClassType SYS_VAR_IDS_SORTED_BY_NAME[ALL_SYS_VARS_COUNT];
  const static char *SYS_VAR_NAMES_SORTED_BY_ID[ALL_SYS_VARS_COUNT];
};

}
}
#endif //OCEANBASE_SHARE_SYSTEM_VARIABLE_OB_SYS_VAR_META_