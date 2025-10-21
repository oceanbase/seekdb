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

#ifndef OCEANBASE_STORAGE_CACHE_DDL_UTIL_H_
#define OCEANBASE_STORAGE_CACHE_DDL_UTIL_H_
#include "share/ob_define.h"
#include "share/schema/ob_table_schema.h"
#include "share/ob_rpc_struct.h"
namespace oceanbase
{
namespace sql
{
class ObStorageCacheUtil
{
public:
  static int print_table_storage_cache_policy(const ObTableSchema &table_schema, char* buf, const int64_t &buf_len, int64_t &pos);
  static int check_alter_column_validation(const AlterColumnSchema *alter_column_schema, const ObTableSchema &orig_table_schema);
  static bool is_type_change_allow(const ObObjType &src_type, const ObObjType &dst_type);
  static int check_alter_partiton_storage_cache_policy(const share::schema::ObTableSchema &orig_table_schema,
                                                       const obrpc::ObAlterTableArg &alter_table_arg);
  static int check_alter_subpartiton_storage_cache_policy(const share::schema::ObTableSchema &orig_table_schema,
                                                          const obrpc::ObAlterTableArg &alter_table_arg); 
  static int check_column_is_first_part_key(const ObPartitionKeyInfo &part_key_info, const uint64_t column_id);
  static int get_range_part_level(const share::schema::ObTableSchema &tbl_schema, const ObStorageCachePolicy &storage_cache_policy,
                                  int32_t &part_level);
  DISALLOW_COPY_AND_ASSIGN(ObStorageCacheUtil);
};
} // namespace sql
} // namespace oceanbase
#endif
