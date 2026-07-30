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

#ifndef OCEANBASE_SHARE_OB_LOCAL_MERGE_TABLE_OPERATOR_
#define OCEANBASE_SHARE_OB_LOCAL_MERGE_TABLE_OPERATOR_

#include "lib/container/ob_iarray.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "share/storage/ob_local_merge_info_table_storage.h"

namespace oceanbase
{
namespace common
{
class ObMySQLTransaction;
}
namespace share
{
class ObLocalMergeInfo;

// CRUD operations for the local merge record.
class ObLocalMergeTableOperator
{
public:
  // Initialize SQLite storage (called once at startup)
  static int init();
  static int load_local_merge_info(common::ObISQLClient &sql_client,
                                  share::ObLocalMergeInfo &info,
                                  const bool print_sql = false);
  static int insert_local_merge_info(common::ObISQLClient &sql_client,
                                    const share::ObLocalMergeInfo &info);
  static int update_partial_local_merge_info(common::ObISQLClient &sql_client,
                                            const share::ObLocalMergeInfo &info);

private:
  static ObLocalMergeInfoTableStorage storage_;
};

} // end namespace share
} // end namespace oceanbase

#endif  // OCEANBASE_SHARE_OB_LOCAL_MERGE_TABLE_OPERATOR_
