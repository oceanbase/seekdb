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

#define USING_LOG_PREFIX LIB_MYSQLC
#include "lib/mysqlclient/ob_mysql_result.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
ObMySQLResult::ObMySQLResult()
{
}

ObMySQLResult::~ObMySQLResult()
{

}

int ObMySQLResult::varchar2datetime(const ObString &varchar, int64_t &datetime) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(varchar.length() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid empty value", K(varchar), K(ret));
  } else {
    ObTimeConvertCtx cvrt_ctx(NULL, false);
    ret = ObTimeConverter::str_to_datetime(varchar, cvrt_ctx, datetime, NULL);
  }
  return ret;
}

int ObMySQLResult::get_single_int(const int64_t row_idx, const int64_t col_idx, int64_t &int_val)
{
  int ret = OB_SUCCESS;
  if (row_idx < 0 || col_idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid idx", K(row_idx), K(col_idx));
  } else {
    int64_t i = 0;
    for (; i < row_idx && OB_SUCC(ret); ++i) {
      ret = this->next();
    }
    if (OB_FAIL(ret)) {
      if (OB_ITER_END == ret) {
        LOG_WARN("too few result rows", K(row_idx), K(i));
      }
    } else {
      // get the single row
      if (OB_FAIL(this->next())) {
        LOG_WARN("too few result rows", K(ret), K(row_idx));
      } else {
        ret = get_int(col_idx, int_val);
      }
    }
  }
  return ret;
}

int ObMySQLResult::print_info() const
{
  return OB_SUCCESS;
}
} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase
