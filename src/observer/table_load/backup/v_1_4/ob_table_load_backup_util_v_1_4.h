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

#pragma once
#include "share/rc/ob_tenant_base.h"
#include "share/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace observer
{

class ObTableLoadBackupUtil_V_1_4
{
public:
  ObTableLoadBackupUtil_V_1_4() {}
  ~ObTableLoadBackupUtil_V_1_4() {}
  static int get_column_ids_from_create_table_sql(const ObString &sql, ObIArray<int64_t> &column_ids); 
};

} // namespace observer
} // namespace oceanbase
