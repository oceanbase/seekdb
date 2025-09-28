/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */
#ifndef OCEANBASE_SHARE_INNER_TABLE_OB_LOAD_INNER_TABLE_SCHEMA_H_
#define OCEANBASE_SHARE_INNER_TABLE_OB_LOAD_INNER_TABLE_SCHEMA_H_

#include "ob_inner_table_schema.h"

namespace oceanbase
{
namespace share
{
class ObLoadInnerTableSchemaInfo
{
public:
  ObLoadInnerTableSchemaInfo(
      const uint64_t table_id = OB_INVALID_ID,
      const char *table_name = nullptr,
      const char *column_names = nullptr,
      const uint64_t *table_ids = nullptr,
      const char **rows = nullptr,
      const uint64_t count = 0) : inner_table_id_(table_id), inner_table_name_(table_name),
    inner_table_column_names_(column_names), inner_table_table_ids_(table_ids), inner_table_rows_(rows),
    row_count_(count) {}
  uint64_t get_inner_table_id() const { return inner_table_id_; }
  const char *get_inner_table_name() const { return inner_table_name_; }
  const char *get_inner_table_column_names() const { return inner_table_column_names_; }
  int get_row(const int64_t idx, const char *&row, uint64_t &table_id) const;
  int64_t get_row_count() const { return row_count_; }
  TO_STRING_KV(K(inner_table_id_), K(inner_table_name_), K(inner_table_column_names_), K(row_count_));
private:
  uint64_t inner_table_id_;
  const char *inner_table_name_;
  const char *inner_table_column_names_;
  const uint64_t *inner_table_table_ids_;
  const char **inner_table_rows_;
  int64_t row_count_;
};

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_INNER_TABLE_OB_LOAD_INNER_TABLE_SCHEMA_H_
