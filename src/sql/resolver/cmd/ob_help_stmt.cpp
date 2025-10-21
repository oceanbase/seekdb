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

#include "sql/resolver/cmd/ob_help_stmt.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;
ObHelpStmt::ObHelpStmt()
    :ObDMLStmt(stmt::T_HELP),
    row_store_(),
     col_names_()
{
}

ObHelpStmt::~ObHelpStmt()
{

}
int ObHelpStmt::add_col_name(ObString col_name)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(col_names_.push_back(col_name))) {
    SQL_RESV_LOG(WARN, "fail to push back column name", K(ret), K(col_name));
  }
  return ret;
}
int ObHelpStmt::get_col_name(int64_t idx, ObString &col_name)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(idx < 0 || idx >= col_names_.count())) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(
        WARN, "invalid argument", K(ret), K(idx), K(col_names_.count()));
  } else {
    col_name = col_names_.at(idx);
  }
  return ret;
}
