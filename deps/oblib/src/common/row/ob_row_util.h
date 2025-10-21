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

#ifndef OCEANBASE_COMMON_OB_ROW_UTIL_
#define OCEANBASE_COMMON_OB_ROW_UTIL_

#include "common/row/ob_row.h"
#include "common/rowkey/ob_rowkey.h"

namespace oceanbase
{
namespace common
{

class ObRowUtil
{
public:
  static int convert(const common::ObString &compact_row, ObNewRow &row);
  static int convert(const char *compact_row, int64_t buf_len, ObNewRow &row);
  static int compare_row(const ObNewRow &lrow, const ObNewRow &rrow, int &cmp);
};

} // end namespace common
} // end namespace oceanbase

#endif /* OCEANBASE_COMMON_OB_ROW_UTIL_ */
