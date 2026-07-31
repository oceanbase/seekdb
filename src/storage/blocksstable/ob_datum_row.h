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

#ifndef OB_STORAGE_BLOCKSSTABLE_DATUM_ROW_H
#define OB_STORAGE_BLOCKSSTABLE_DATUM_ROW_H

#include "common/row/ob_row.h"
#include "data_plane/blocksstable/ob_datum_row.h"
#include "storage/ob_storage_util.h"
#include "storage/blocksstable/ob_datum_rowkey.h"
#include "storage/blocksstable/ob_storage_datum.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
struct ObColDesc;
}
}
namespace blocksstable
{

// Storage-only row conversion and MVCC utilities.  The concrete row layout is
// owned by the data-plane API included above.
class ObNewRowBuilder
{
public:
  ObNewRowBuilder()
    : cols_descs_(nullptr),
      new_row_(),
      obj_buf_()
  {}
  ~ObNewRowBuilder() = default;
  OB_INLINE int init(
      const common::ObIArray<share::schema::ObColDesc> &cols_descs,
      common::ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    cols_descs_ = &cols_descs;
    if (OB_FAIL(obj_buf_.init(&allocator))) {
      STORAGE_LOG(WARN, "Failed to init ObObjBufArray", K(ret));
    }
    return ret;
  }
  int build(
      const blocksstable::ObDatumRow &datum_row,
      common::ObNewRow *&new_row);
  int build_store_row(
      const blocksstable::ObDatumRow &datum_row,
      storage::ObStoreRow &store_row);
  TO_STRING_KV(KP_(cols_descs), K_(new_row));
private:
  const common::ObIArray<share::schema::ObColDesc> *cols_descs_;
  common::ObNewRow new_row_;
  storage::ObObjBufArray obj_buf_;
};

struct ObConstDatumRow
{
  OB_UNIS_VERSION(1);
public:
  ObConstDatumRow() { MEMSET(this, 0, sizeof(ObConstDatumRow)); }
  ObConstDatumRow(common::ObDatum *datums, uint64_t count, int64_t datum_row_offset)
    : datums_(datums),
      count_(count),
      datum_row_offset_(datum_row_offset)
  {}
  ~ObConstDatumRow() = default;
  OB_INLINE int64_t get_column_count() const { return count_; }
  OB_INLINE bool is_valid() const { return nullptr != datums_ && count_ > 0 && datum_row_offset_ >= 0; }
  OB_INLINE const common::ObDatum &get_datum(const int64_t col_idx) const
  {
    OB_ASSERT(col_idx < count_ && col_idx >= 0);
    return datums_[col_idx];
  }
  int set_datums_ptr(char *datums_ptr);
  TO_STRING_KV(K_(count), "datums_:", common::ObArrayWrap<common::ObDatum>(datums_, count_));
  common::ObDatum *datums_;
  uint64_t count_;
  int64_t datum_row_offset_;
};

struct ObGhostRowUtil
{
public:
  ObGhostRowUtil() = delete;
  ~ObGhostRowUtil() = delete;
  static int make_ghost_row(
      const int64_t sql_sequence_col_idx,
      blocksstable::ObDatumRow &row);
  static int is_ghost_row(
      const blocksstable::ObMultiVersionRowFlag &flag,
      bool &is_ghost_row);
  static const int64_t GHOST_NUM = INT64_MAX;
};

struct ObShadowRowUtil
{
public:
  ObShadowRowUtil() = delete;
  ~ObShadowRowUtil() = delete;
  static int make_shadow_row(
      const int64_t sql_sequence_col_idx,
      blocksstable::ObDatumRow &row);
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OB_STORAGE_BLOCKSSTABLE_DATUM_ROW_H
