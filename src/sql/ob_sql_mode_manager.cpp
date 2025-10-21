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

#include "lib/string/ob_string.h"
#include "sql/ob_sql_mode_manager.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

int compatibility_mode2index(const common::ObCompatibilityMode mode, int64_t &index)
{
  static const ObCompatibilityMode modes[] = { OCEANBASE_MODE, MYSQL_MODE, ORACLE_MODE };
  int ret = OB_ENTRY_NOT_EXIST;
  for (int64_t i = 0; OB_ENTRY_NOT_EXIST == ret && i < ARRAYSIZEOF(modes); ++i) {
    if (mode == modes[i]) {
      index = i;
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

/*
int ObSQLModeManager::get_compatibility_index(ObCompatibilityMode comp_mode, int64_t &index) const
{
  int ret = OB_ENTRY_NOT_EXIST;
  for (int64_t i = 0; i < COMPATIBILITY_MODE_COUNT && OB_ENTRY_NOT_EXIST == ret; i++) {
    if (comp_mode == sql_standard_[i].comp_mode_) {
      index = i;
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObSQLModeManager::set_compatibility_mode(ObCompatibilityMode comp_mode)
{
  int ret = OB_SUCCESS;
  int64_t index = -1;
  if (OB_FAIL(get_compatibility_index(comp_mode, index))) {
    OB_LOG(WARN, "fail to get sql mode", K(ret), K(comp_mode));
  } else {
    current_mode_index_ = index;
  }
  return ret;
}

ObCompatibilityMode ObSQLModeManager::get_compatibility_mode() const
{
  return sql_standard_[current_mode_index_].comp_mode_;
}

void ObSQLModeManager::reset()
{
  sql_standard_[0].sql_mode_ = DEFAULT_OCEANBASE_MODE;
  sql_standard_[0].comp_mode_ = ObCompatibilityMode::OCEANBASE_MODE;
  sql_standard_[1].sql_mode_ = DEFAULT_MYSQL_MODE;
  sql_standard_[1].comp_mode_ = ObCompatibilityMode::MYSQL_MODE;
  sql_standard_[2].sql_mode_ = DEFAULT_ORACLE_MODE;
  sql_standard_[2].comp_mode_ = ObCompatibilityMode::ORACLE_MODE;
}

ObSQLMode ObSQLModeManager::get_sql_mode() const
{
  return sql_standard_[current_mode_index_].sql_mode_;
}

void ObSQLModeManager::set_sql_mode(ObSQLMode mode)
{
  sql_standard_[current_mode_index_].sql_mode_ = mode;
}
*/
}
}
