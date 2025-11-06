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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_OB_ROW_READER_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_OB_ROW_READER_H_
#include <stdint.h>
#include "ob_block_sstable_struct.h"
#include "lib/allocator/page_arena.h"
#include "storage/ob_i_store.h"
#include "common/object/ob_obj_type.h"
#include "common/rowkey/ob_rowkey.h"
#include "share/schema/ob_schema_struct.h"
#include "storage/access/ob_table_read_info.h"
#include "ob_datum_rowkey.h"

namespace oceanbase
{
namespace memtable
{
class ObNopBitMap;
}
namespace blocksstable
{
typedef common::ObIArray<share::schema::ObColDesc> ObColDescIArray;
typedef common::ObIArray<ObColumnParam *> ObColumnParamIArray;

class ObClusterColumnReader
{
public:
  ObClusterColumnReader()
   : cluster_buf_(nullptr),
     cell_end_pos_(0),
     column_cnt_(0),
     sparse_column_cnt_(0),
     cur_idx_(0),
     column_offset_(nullptr),
     column_idx_array_(nullptr),
     special_vals_(nullptr),
     offset_bytes_(ObColClusterInfoMask::BYTES_MAX),
     col_idx_bytes_(ObColClusterInfoMask::BYTES_MAX),
     is_sparse_row_(false),
     is_inited_(false)
  {
  }
  bool is_init() const { return is_inited_; }
  int init(
      const char *cluster_buf,
      const uint64_t cluster_len,
      const uint64_t cluster_col_cnt,
      const ObColClusterInfoMask &info_mask);
  void reset();
  int read_storage_datum(const int64_t column_idx, ObStorageDatum &datum);
  int sequence_read_datum(const int64_t column_idx, ObStorageDatum &datum);
  OB_INLINE int64_t get_sparse_col_idx(const int64_t column_idx);
  OB_INLINE int64_t get_column_count() const { return column_cnt_; }

  TO_STRING_KV(KP_(cluster_buf), K_(cell_end_pos), K_(column_cnt),
      K_(is_sparse_row), K_(offset_bytes), K_(col_idx_bytes), KP_(special_vals));
private:
  int sequence_deep_copy_datums_of_sparse(const int64_t start_idx, ObStorageDatum *datums);
  int sequence_deep_copy_datums_of_dense(const int64_t start_idx, ObStorageDatum *datums);
  int read_8_bytes_column(
      const char *buf,
      const int64_t buf_len,
      ObStorageDatum &datum);
  int read_column_from_buf(
      int64_t tmp_pos,
      int64_t next_pos,
      const ObRowHeader::SPECIAL_VAL special_val,
      ObStorageDatum &datum);
  int read_datum(const int64_t column_idx, ObStorageDatum &datum);
  OB_INLINE uint8_t read_special_value(const int64_t column_idx)
  {
    const int64_t index = column_idx >> 1;
    const int64_t shift = (column_idx % 2) << 2;
    return (special_vals_[index] >> shift) & 0x0F;
  }
private:
  const char *cluster_buf_;
  int64_t cell_end_pos_;
  int64_t column_cnt_;
  int64_t sparse_column_cnt_;
  int64_t cur_idx_; // only use when sequence read sparse cell
  const void *column_offset_;
  const void *column_idx_array_;
  const uint8_t *special_vals_;
  ObColClusterInfoMask::BYTES_LEN offset_bytes_;
  ObColClusterInfoMask::BYTES_LEN col_idx_bytes_;
  bool is_sparse_row_;
  bool is_inited_;
};

class ObRowReader
{
public:
  ObRowReader();
  virtual ~ObRowReader() { reset(); }
  // read row from flat storage(RowHeader | cells array | column index array)
  // @param (row_buf + pos) point to RowHeader
  // @param row_len is buffer capacity
  // @param column_map use when schema version changed use column map to read row
  // @param [out]row parsed row object.
  // @param out_type indicates the type of ouput row
  int read_row(
      const char *row_buf,
      const int64_t row_len,
      const storage::ObITableReadInfo *read_info,
      ObDatumRow &datum_row);
  // only read cells where bitmap shows col_idx = TRUE
  int read_memtable_row(
      const char *row_buf,
      const int64_t row_len,
      const storage::ObITableReadInfo &read_info,
      ObDatumRow &datum_row,
      memtable::ObNopBitMap &nop_bitmap,
      bool &read_finished,
      const ObRowHeader *&row_header);
  int read_row_header(const char *row_buf, const int64_t row_len, const ObRowHeader *&row_header);
  int read_column(
      const char *row_buf,
      const int64_t row_len,
      const int64_t col_index,
      ObStorageDatum &datum);
  int compare_meta_rowkey(
      const ObDatumRowkey &rhs,
      const blocksstable::ObStorageDatumUtils &datum_utils,
      const char *buf,
      const int64_t row_len,
      int32_t &cmp_result);
  void reset();
  TO_STRING_KV(KP_(buf), K_(row_len), KPC_(row_header), K_(cluster_cnt),
      K_(cur_read_cluster_idx), K_(cluster_reader));
private:
  int setup_row(const char *buf, const int64_t row_len);
  OB_INLINE int analyze_row_header();
  OB_INLINE int analyze_cluster_info();

  OB_INLINE int read_specific_column_in_cluster(const int64_t store_idx, ObStorageDatum &datum);
  template<class T> static const T *read(const char *row_buf, int64_t &pos);
  OB_INLINE bool is_valid() const;
  OB_INLINE uint64_t get_cluster_offset(const int64_t cluster_idx) const;
  OB_INLINE uint64_t get_cluster_end_pos(const int64_t cluster_idx) const;
  OB_INLINE int analyze_info_and_init_reader(const int64_t cluster_idx);
protected:
  const char *buf_;
  int64_t row_len_;
  const ObRowHeader *row_header_;
  const void *cluster_offset_;
  const void *column_offset_;
  const void *column_idx_array_;
  ObClusterColumnReader cluster_reader_;
  uint32_t cur_read_cluster_idx_;
  uint32_t cluster_cnt_;
  bool rowkey_independent_cluster_;
  bool is_setuped_;
};

template<class T>
inline const T *ObRowReader::read(const char *row_buf, int64_t &pos)
{
  const T *ptr = reinterpret_cast<const T*>(row_buf + pos);
  pos += sizeof(T);
  return ptr;
}

}//end namespace blocksstable
}//end namespace oceanbase
#endif
