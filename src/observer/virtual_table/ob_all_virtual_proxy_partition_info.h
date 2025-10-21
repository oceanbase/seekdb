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

#ifndef OCEANBASE_OBSERVER_ALL_VIRTUAL_PROXY_PARTITION_INFO_
#define OCEANBASE_OBSERVER_ALL_VIRTUAL_PROXY_PARTITION_INFO_

#include "ob_all_virtual_proxy_base.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
namespace sql
{
class ObRawExpr;
}
namespace observer
{
class ObAllVirtualProxyPartitionInfo : public ObAllVirtualProxyBaseIterator
{
  enum ALL_VIRTUAL_PROXY_PARTITION_INFO_TABLE_COLUMNS
  {
    TENANT_NAME = oceanbase::common::OB_APP_MIN_COLUMN_ID,
    TABLE_ID,

    PART_LEVEL,
    ALL_PART_NUM,
    TEMPLATE_NUM,
    PART_ID_RULE_VER,

    PART_TYPE,
    PART_NUM,
    IS_COLUMN_TYPE,
    PART_SPACE,
    PART_EXPR,
    PART_EXPR_BIN,
    PART_RANGE_TYPE,
    PART_INTERVAL,
    PART_INTERVAL_BIN,
    INTERVAL_START,
    INTERVAL_START_BIN,

    SUB_PART_TYPE,
    SUB_PART_NUM,
    IS_SUB_COLUMN_TYPE,
    SUB_PART_SPACE, // not used yet, for reserved
    SUB_PART_EXPR,
    SUB_PART_EXPR_BIN,
    SUB_PART_RANGE_TYPE,
    DEF_SUB_PART_INTERVAL,
    DEF_SUB_PART_INTERVAL_BIN,
    DEF_SUB_INTERVAL_START,
    DEF_SUB_INTERVAL_START_BIN,

    PART_KEY_NUM,
    PART_KEY_NAME,
    PART_KEY_TYPE,
    PART_KEY_IDX, // used for calc insert stmt
    PART_KEY_LEVEL,
    PART_KEY_EXTRA, // reserved for other info
    PART_KEY_COLLATION_TYPE,
    PART_KEY_ROWKEY_IDX,
    PART_KEY_EXPR,
    PART_KEY_LENGTH,
    PART_KEY_PRECISION,
    PART_KEY_SCALE,

    SPARE1, // replace this with a table-level schema_version
    SPARE2,
    SPARE3,
    SPARE4,
    SPARE5,
    SPARE6,
    PART_KEY_DEFAULT_VALUE,
  };

public:
  ObAllVirtualProxyPartitionInfo();
  virtual ~ObAllVirtualProxyPartitionInfo();
  virtual int inner_open();
  virtual int inner_get_next_row();
private:
  int fill_row_(const share::schema::ObTableSchema &table_schema);
  int gen_proxy_part_pruning_str_(
      const share::schema::ObTableSchema &table_schema,
      const share::schema::ObColumnSchemaV2 *column_schema,
      common::ObString &proxy_check_partition_str);
  int build_check_str_to_raw_expr_(
      const common::ObString &check_expr_str,
      const share::schema::ObTableSchema &table_schema,
      sql::ObRawExpr *&check_expr);
private:
  int64_t next_table_idx_;
  int64_t next_part_key_idx_;
  common::ObSEArray<const share::schema::ObTableSchema *, 1> table_schemas_;
  char buf_[common::OB_TMP_BUF_SIZE_256];
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualProxyPartitionInfo);
};

} // end of namespace observer
} // end of namespace oceanbase
#endif /* OCEANBASE_OBSERVER_ALL_VIRTUAL_PROXY_PARTITION_INFO_ */
