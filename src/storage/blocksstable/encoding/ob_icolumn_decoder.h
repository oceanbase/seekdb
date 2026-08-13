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

#ifndef OCEANBASE_ENCODING_OB_ICOLUMN_DECODER_H_
#define OCEANBASE_ENCODING_OB_ICOLUMN_DECODER_H_

#include "common/object/ob_object.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"
#include "storage/blocksstable/ob_data_buffer.h"
#include "storage/ob_storage_util.h"
#include "query/engine/basic/ob_pushdown_filter.h"
#include "lib/container/ob_bitmap.h"
#include "ob_bit_stream.h"
#include "ob_encoding_util.h"
#include "ob_row_index.h"
#include "storage/blocksstable/ob_micro_block_header.h"

namespace oceanbase
{
namespace storage
{
class ObGroupByCellBase;
}
namespace blocksstable
{

class ObBitStream;
class ObIColumnDecoder;
class ObIRowIndex;

struct ObBaseDecoderCtx
{
public:
  ObBaseDecoderCtx() : obj_meta_(), micro_block_header_(NULL), col_header_(NULL) {}

  common::ObObjMeta obj_meta_;
  const ObMicroBlockHeader *micro_block_header_;
  const ObColumnHeader *col_header_;

};

struct ObColumnDecoderCtx final : public ObBaseDecoderCtx
{
public:
  ObColumnDecoderCtx()
      : allocator_(NULL), ref_decoder_(NULL), ref_ctx_(NULL), col_param_(NULL)
  {
  }

  // performance critical, do not check parameters
  OB_INLINE void fill(const common::ObObjMeta &obj_meta,
      const ObMicroBlockHeader *micro_header,
      const ObColumnHeader *col_header,
      common::ObIAllocator *allocator,
      const int64_t store_idx)
  {
    obj_meta_ = obj_meta;
    micro_block_header_ = micro_header;
    col_header_ = col_header;
    allocator_ = allocator;

    cache_attributes_[ObColumnHeader::FIX_LENGTH] = col_header_->is_fix_length();
    cache_attributes_[ObColumnHeader::HAS_EXTEND_VALUE] = col_header_->has_extend_value();
    cache_attributes_[ObColumnHeader::BIT_PACKING] = col_header_->is_bit_packing();
    cache_attributes_[ObColumnHeader::IS_TRANS_VERSION] = micro_block_header_->is_trans_version_column_idx(store_idx);
  }
  // performance critical, do not check parameters
  OB_INLINE void fill_for_new_column(const share::schema::ObColumnParam *col_param, common::ObIAllocator *allocator)
  {
    reset();
    col_param_ = col_param;
    allocator_ = allocator;
  }
  OB_INLINE bool is_fix_length() const { return cache_attributes_[ObColumnHeader::FIX_LENGTH]; }
  OB_INLINE bool has_extend_value() const { return cache_attributes_[ObColumnHeader::HAS_EXTEND_VALUE]; }
  OB_INLINE bool is_bit_packing() const { return cache_attributes_[ObColumnHeader::BIT_PACKING]; }
  OB_INLINE void set_col_param(const share::schema::ObColumnParam *col_param)
  {
    col_param_ = col_param;
  }
  OB_INLINE bool is_trans_version_col() const { return cache_attributes_[ObColumnHeader::IS_TRANS_VERSION]; }
  OB_INLINE void reset() { MEMSET(this, 0, sizeof(ObColumnDecoderCtx)); }

  TO_STRING_KV(K_(obj_meta), K_(micro_block_header), K_(col_header), KP_(allocator),
      KP_(ref_decoder), KP_(ref_ctx));

  common::ObIAllocator *allocator_;
  const ObIColumnDecoder *ref_decoder_;
  ObColumnDecoderCtx *ref_ctx_;
  // Pointer to ColumnParam for padding in filter pushdown
  const share::schema::ObColumnParam *col_param_;
  bool cache_attributes_[ObColumnHeader::MAX_ATTRIBUTE];
};

class ObIColumnDecoder
{
public:
  static const uint64_t BITS_PER_BLOCK = 64;
public:
  ObIColumnDecoder() {}
  virtual ~ObIColumnDecoder() {}
  VIRTUAL_TO_STRING_KV(K(this));

  virtual int decode(const ObColumnDecoderCtx &ctx, common::ObDatum &datum, const int64_t row_id,
     const ObBitStream &bs, const char *data, const int64_t len) const = 0;

  virtual ObColumnHeader::Type get_type() const = 0;

  virtual int update_pointer(const char *old_block, const char *cur_block) = 0;

  virtual int get_ref_col_idx(int64_t &ref_col_idx) const
  {
    ref_col_idx = -1;
    return common::OB_SUCCESS;
  }
  virtual void dump_meta(const ObColumnDecoderCtx &) const {}

  // can_vectorized means decode data into datum column directly
  virtual bool can_vectorized() const { return true; }

  // This API should be implemented according to characteris of batch column data
  // for better utilization of CPU Pipeline/Cache and process data in batch
  // Currently only used in vectorized table scan, NOP values not supported.
  // Performance critical, only check pointer once in caller
  virtual int batch_decode(
      const ObColumnDecoderCtx &ctx,
      const ObIRowIndex* row_index,
      const int32_t *row_ids,
      const char **cell_datas,
      const int64_t row_cap,
      common::ObDatum *datums) const
  {
    UNUSEDx(ctx, row_index, row_ids, cell_datas, row_cap, datums);
    return common::OB_NOT_SUPPORTED;
  }

  virtual int pushdown_operator(
      const sql::ObPushdownFilterExecutor *parent,
      const ObColumnDecoderCtx &col_ctx,
      const sql::ObWhiteFilterExecutor &filter,
      const char* meta_data,
      const ObIRowIndex* row_index,
      const sql::PushdownFilterInfo &pd_filter_info,
      ObBitmap &result_bitmap) const
  {
    UNUSEDx(parent, col_ctx, filter, meta_data, row_index, pd_filter_info, result_bitmap);
    return common::OB_NOT_SUPPORTED;
  }

  virtual int pushdown_operator(
      const sql::ObPushdownFilterExecutor *parent,
      const ObColumnDecoderCtx &col_ctx,
      sql::ObBlackFilterExecutor &filter,
      const char* meta_data,
      const ObIRowIndex* row_index,
      sql::PushdownFilterInfo &pd_filter_info,
      ObBitmap &result_bitmap,
      bool &filter_applied) const
  {
    UNUSEDx(parent, col_ctx, filter, meta_data, row_index, pd_filter_info, result_bitmap, filter_applied);
    return common::OB_NOT_SUPPORTED;
  }

  OB_INLINE virtual int locate_row_data(
      const ObColumnDecoderCtx &col_ctx,
      const ObIRowIndex* row_index,
      const int64_t row_id,
      const char *&row_data,
      int64_t &row_len) const;

  OB_INLINE virtual int batch_locate_row_data(
      const ObColumnDecoderCtx &col_ctx,
      const ObIRowIndex *row_index,
      const int32_t *row_ids,
      const int64_t row_cap,
      const char **row_datas,
      common::ObDatum *datums) const;

  virtual int get_is_null_bitmap_from_fixed_column(
      const ObColumnDecoderCtx &col_ctx,
      const unsigned char* col_data,
      const sql::PushdownFilterInfo &pd_filter_info,
      ObBitmap &result_bitmap) const;

  virtual int get_is_null_bitmap_from_var_column(
      const ObColumnDecoderCtx &col_ctx,
      const ObIRowIndex* row_index,
      const sql::PushdownFilterInfo &pd_filter_info,
      ObBitmap &result_bitmap) const;

  virtual int set_null_datums_from_fixed_column(
      const ObColumnDecoderCtx &ctx,
      const int32_t *row_ids,
      const int64_t row_cap,
      const unsigned char *col_data,
      common::ObDatum *datums) const;

  virtual int set_null_datums_from_var_column(
      const ObColumnDecoderCtx &ctx,
      const ObIRowIndex* row_index,
      const int32_t *row_ids,
      const int64_t row_cap,
      common::ObDatum *datums) const;

  virtual int get_null_count(
      const ObColumnDecoderCtx &ctx,
      const ObIRowIndex *row_index,
      const int32_t *row_ids,
      const int64_t row_cap,
      int64_t &null_count) const;

  virtual int get_null_count_from_fixed_column(
      const ObColumnDecoderCtx &ctx,
      const int32_t *row_ids,
      const int64_t row_cap,
      const unsigned char *col_data,
      int64_t &null_count) const;

  virtual int get_null_count_from_var_column(
      const ObColumnDecoderCtx &ctx,
      const ObIRowIndex* row_index,
      const int32_t *row_ids,
      const int64_t row_cap,
      int64_t &null_count) const;

  virtual bool fast_decode_valid(const ObColumnDecoderCtx &ctx) const
  {
    UNUSED(ctx);
    return false;
  }

  virtual int get_distinct_count(int64_t &distinct_count) const
  { UNUSED(distinct_count); return OB_NOT_SUPPORTED; }

  virtual int read_distinct(
      const ObColumnDecoderCtx &ctx,
      const char **cell_datas,
      storage::ObGroupByCellBase &group_by_cell)  const
  { return OB_NOT_SUPPORTED; }

  virtual int read_reference(
      const ObColumnDecoderCtx &ctx,
      const int32_t *row_ids,
      const int64_t row_cap,
      storage::ObGroupByCellBase &group_by_cell) const
  { return OB_NOT_SUPPORTED; }

  virtual bool is_new_column() const { return false; }

  static bool need_padding(const bool is_padding_mode, const ObObjMeta &obj_meta)
  {
    return is_padding_mode && obj_meta.is_fixed_len_char_type();
  }

protected:
  int get_null_count_from_extend_value(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    const char *meta_data_,
    int64_t &null_count) const;

  template <typename Header, bool HAS_NULL>
  static int batch_locate_cell_data(
      const ObColumnDecoderCtx &ctx,
      const Header &header,
      const char **data_arr,
      uint32_t *len_arr,
      const int32_t *row_ids,
      const int64_t row_cap);

  template <typename T>
  inline void update_pointer(T *&ptr, const char *old_block, const char *cur_block)
  {
    ptr = reinterpret_cast<T *>(cur_block + (reinterpret_cast<const char *>(ptr) - old_block));
  }
};

class ObSpanColumnDecoder : public ObIColumnDecoder
{};

// decoder for column not exist in schema
class ObNoneExistColumnDecoder : public ObIColumnDecoder
{
public:
  static const ObColumnHeader::Type type_ = ObColumnHeader::MAX_TYPE;

  virtual int decode(const ObColumnDecoderCtx &ctx, common::ObDatum &datum, const int64_t row_id,
      const ObBitStream &bs, const char *data, const int64_t len)const override
  {
    datum.set_ext();
    datum.no_cv(datum.extend_obj_)->set_ext(common::ObActionFlag::OP_NOP);
    return common::OB_SUCCESS;
  }

  virtual ObColumnHeader::Type get_type() const { return type_; }

  virtual int update_pointer(const char *, const char *) { return common::OB_SUCCESS; }

  virtual bool can_vectorized() const override { return false; }
};

// Read row data offset and row length from row index
OB_INLINE int ObIColumnDecoder::locate_row_data(
    const ObColumnDecoderCtx &col_ctx,
    const ObIRowIndex* row_index,
    const int64_t row_id,
    const char *&row_data,
    int64_t &row_len) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(row_index)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Null pointer of row index", K(ret));
  } else if (OB_FAIL(row_index->get(row_id, row_data, row_len))) {
  }
  return ret;
}

// Batch read row data in row_datas, row_len in datums.len_
OB_INLINE int ObIColumnDecoder::batch_locate_row_data(
    const ObColumnDecoderCtx &col_ctx,
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    const char **row_datas,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(row_ids) || OB_ISNULL(datums) || OB_ISNULL(row_datas) || OB_ISNULL(row_index)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument", K(ret),
        KP(row_ids), KP(row_datas), KP(datums), KP(row_index));
  } else if (OB_FAIL(row_index->batch_get(
      row_ids, row_cap, col_ctx.has_extend_value(),
      row_datas, datums))) {
  }
  return ret;
}

template <typename Header, bool HAS_NULL>
int ObIColumnDecoder::batch_locate_cell_data(
    const ObColumnDecoderCtx &ctx,
    const Header &header,
    const char **data_arr,
    uint32_t *len_arr,
    const int32_t *row_ids,
    const int64_t row_cap)
{
  // for var-length data, nullptr == data_arr[row_id] represent for this row is null
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(data_arr) || OB_ISNULL(len_arr) || OB_ISNULL(row_ids)) {
    ret = common::OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument", K(ret), KP(row_ids), KP(data_arr), KP(len_arr));
  } else if (ctx.is_fix_length()) {
    for (int64_t i = 0; i < row_cap; ++i) {
      data_arr[i] += header.offset_;
      len_arr[i] = header.length_;
    }
  } else if (1 == ctx.micro_block_header_->var_column_count_) {
    for (int64_t i = 0; i < row_cap; ++i) {
      if (!HAS_NULL || nullptr != data_arr[i]) {
        data_arr[i] += header.offset_;
        len_arr[i] -= header.offset_;
      }
    }
  } else {
    ObIntegerArrayGenerator gen;
    if (ctx.col_header_->is_last_var_field()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
        if (!HAS_NULL || nullptr != data_arr[i]) {
          const uint8_t col_idx_byte = *(data_arr[i] + header.offset_);
          const char *var_data = data_arr[i] + header.offset_ + sizeof(uint8_t);
          if (OB_FAIL(gen.init(var_data - col_idx_byte, col_idx_byte))) {
          } else {
            var_data += (ctx.micro_block_header_->var_column_count_ - 1) * col_idx_byte;
            const int64_t offset = 0 == header.length_ ? 0 : gen.get_array().at(header.length_);
            const int64_t datum_offset_in_row = offset + (var_data - data_arr[i]);
            // datum_offset_in_row is ensured to be included in range of int32
            len_arr[i] = len_arr[i] - static_cast<const int32_t>(datum_offset_in_row);
            data_arr[i] = var_data + offset;
          }
        }
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
        if (!HAS_NULL || nullptr != data_arr[i]) {
          const int8_t col_idx_byte = *(data_arr[i] + header.offset_);
          const char *var_data = data_arr[i] + header.offset_ + sizeof(uint8_t);
          if (OB_FAIL(gen.init(var_data - col_idx_byte, col_idx_byte))) {
          } else {
            var_data += (ctx.micro_block_header_->var_column_count_ - 1) * col_idx_byte;
            // 0 if header.length_ == 0
            const int64_t offset = 0 == header.length_ ? 0 : gen.get_array().at(header.length_);
            len_arr[i] = gen.get_array().at(header.length_ + 1) - offset;
            data_arr[i] = var_data + offset;
          }
        }
      }
    }
  }
  return ret;
}

} // end namespace blocksstable
} // end namespace oceanbase

#endif // OCEANBASE_ENCODING_OB_ICOLUMN_DECODER_H_
