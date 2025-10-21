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

#ifndef OCEANBASE_SHARE_INDEX_BUILDER_UTIL_H_
#define OCEANBASE_SHARE_INDEX_BUILDER_UTIL_H_

#include "lib/container/ob_array.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace common
{
class ObString;
class ObRowDesc;
};
namespace obrpc
{
struct ObColumnSortItem;
struct ObCreateIndexArg;
};

namespace sql
{
class ObRawExpr;
}  // namespace sql

namespace share
{
namespace schema
{
class ObTableSchema;
class ObColumnSchemaV2;
};
class ObIndexBuilderUtil
{
public:
  static bool is_do_create_dense_vec_index(const ObIndexType index_type);
  static int adjust_expr_index_args(
      obrpc::ObCreateIndexArg &arg,
      share::schema::ObTableSchema &data_schema,
      common::ObIAllocator &allocator,
      common::ObIArray<share::schema::ObColumnSchemaV2*> &gen_columns);
  static int generate_ordinary_generated_column(
      sql::ObRawExpr &expr,
      const sql::ObSQLSessionInfo &session,
      share::schema::ObTableSchema &data_schema,
      share::schema::ObColumnSchemaV2 *&gen_col,
      share::schema::ObSchemaGetterGuard *schema_guard,
      const uint64_t index_id = OB_INVALID_ID);
  static int set_index_table_columns(
      const obrpc::ObCreateIndexArg &arg,
      const share::schema::ObTableSchema &data_schema,
      share::schema::ObTableSchema &index_schema,
      bool check_data_schema = true);
  static void del_column_flags_and_default_value(share::schema::ObColumnSchemaV2 &column);
  static int add_column(
      const share::schema::ObColumnSchemaV2 *data_column,
      const bool is_index,
      const bool is_rowkey,
      const common::ObOrderType order_type,
      common::ObRowDesc &row_desc,
      share::schema::ObTableSchema &table_schema,
      const bool is_hidden,
      const bool is_specified_storing_col);
  static int set_shadow_column_info(
      const ObString &src_column_name,
      const uint64_t src_column_id,
      ObColumnSchemaV2 &shadow_column_schema);
private:
  static const int SPATIAL_MBR_COLUMN_MAX_LENGTH = 32;
  static int generate_prefix_column(
      const obrpc::ObColumnSortItem &sort_item,
      const ObSQLMode sql_mode,
      share::schema::ObTableSchema &data_schema,
      share::schema::ObColumnSchemaV2 *&prefix_col);
  static int adjust_ordinary_index_column_args(
      obrpc::ObCreateIndexArg &arg,
      share::schema::ObTableSchema &data_schema,
      common::ObIAllocator &allocator,
      common::ObIArray<share::schema::ObColumnSchemaV2*> &gen_columns);
  static int add_shadow_pks(
      const share::schema::ObTableSchema &data_schema,
      common::ObRowDesc &row_desc,
      share::schema::ObTableSchema &schema,
      bool check_data_schema = true);
  static int add_shadow_partition_keys(
      const share::schema::ObTableSchema &data_schema,
      common::ObRowDesc &row_desc,
      share::schema::ObTableSchema &schema);
  static int adjust_spatial_args(
      obrpc::ObCreateIndexArg &arg,
      share::schema::ObTableSchema &data_schema,
      common::ObIAllocator &allocator,
      common::ObIArray<share::schema::ObColumnSchemaV2*> &spatial_cols);
  static int generate_spatial_columns(
      const common::ObString &col_name,
      share::schema::ObTableSchema &data_schema,
      common::ObIArray<share::schema::ObColumnSchemaV2*> &spatial_cols);
  static int generate_spatial_cellid_column(
      share::schema::ObColumnSchemaV2 &col_schema,
      share::schema::ObTableSchema &data_schema,
      share::schema::ObColumnSchemaV2 *&cellid_col);
  static int generate_spatial_mbr_column(
    share::schema::ObColumnSchemaV2 &col_schema,
    share::schema::ObTableSchema &data_schema,
    share::schema::ObColumnSchemaV2 *&mbr_col);
};
}//end namespace rootserver
}//end namespace oceanbase

#endif //OCEANBASE_SHARE_INDEX_BUILDER_UTIL_H_
