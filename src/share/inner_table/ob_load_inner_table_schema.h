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
