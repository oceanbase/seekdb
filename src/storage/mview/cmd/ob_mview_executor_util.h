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

#include "lib/number/ob_number_v2.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace storage
{
class ObMViewExecutorUtil
{
public:

  static int check_min_data_version(const uint64_t tenant_id, const uint64_t min_data_version,
                                    const char *errmsg);
  static int split_table_list(const common::ObString &table_list,
                              common::ObIArray<common::ObString> &tables);
  static int resolve_table_name(const common::ObCollationType cs_type,
                                const ObNameCaseMode case_mode, const bool is_oracle_mode,
                                const common::ObString &name, common::ObString &database_name,
                                common::ObString &table_name);
  static void upper_db_table_name(const ObNameCaseMode case_mode, const bool is_oracle_mode,
                                  common::ObString &name);

  static int to_refresh_method(const char c, share::schema::ObMVRefreshMethod &refresh_method);
  static int to_collection_level(const common::ObString &str,
                                 share::schema::ObMVRefreshStatsCollectionLevel &collection_level);

  static int generate_refresh_id(const uint64_t tenant_id, int64_t &refresh_id);

  static bool is_mview_refresh_retry_ret_code(int ret_code);
};

} // namespace storage
} // namespace oceanbase
