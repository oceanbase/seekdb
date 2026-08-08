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

#ifndef OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_READ_INFO_H_
#define OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_READ_INFO_H_

#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStorageDatumUtils;
}
namespace share
{
namespace schema
{
class ObColumnParam;
class ObColDesc;
class ObColExtend;
}
}
namespace storage
{
class ObColumnIndexArray;

// Query-facing read metadata. Storage retains the concrete ObTableReadInfo;
// this interface is the stable vocabulary needed by SQL scan plans.
class ObITableReadInfo
{
public:
  ObITableReadInfo() = default;
  virtual ~ObITableReadInfo() = default;
  virtual int64_t get_schema_column_count() const = 0;
  virtual int64_t get_seq_read_column_count() const = 0;
  virtual int64_t get_request_count() const = 0;
  virtual int64_t get_schema_rowkey_count() const = 0;
  virtual int64_t get_rowkey_count() const = 0;
  virtual int64_t get_group_idx_col_index() const = 0;
  virtual int64_t get_trans_col_index() const = 0;
  virtual const common::ObIArray<share::schema::ObColDesc> &get_columns_desc() const = 0;
  virtual const ObColumnIndexArray &get_columns_index() const = 0;
  virtual const ObColumnIndexArray &get_memtable_columns_index() const = 0;
  virtual const blocksstable::ObStorageDatumUtils &get_datum_utils() const = 0;
  virtual const common::ObIArray<share::schema::ObColumnParam *> *get_columns() const = 0;
  virtual const common::ObIArray<share::schema::ObColExtend> *get_columns_extend() const = 0;
  virtual bool is_access_rowkey_only() const = 0;
  virtual bool need_truncate_filter() const = 0;
  virtual bool is_valid() const = 0;
  virtual void reset() = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_ACCESS_OB_TABLE_READ_INFO_H_
