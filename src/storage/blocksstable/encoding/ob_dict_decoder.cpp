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

#define USING_LOG_PREFIX STORAGE

#include "data_plane/encoding/ob_cpu_features.h"
#include "ob_dict_decoder.h"
#include "ob_raw_decoder.h"
#include "storage/access/ob_pushdown_aggregate.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;
const ObColumnHeader::Type ObDictDecoder::type_;

template <int32_t REF_BYTE_TAG, bool IS_SIGNED_SC, int32_t STORE_LEN_TAG,
    int32_t DATUM_LEN_TAG, int32_t STORE_CLASS>
struct DictFixBatchDecodeFunc_T
{
  static void dict_fix_batch_decode_func(
      const char *ref_data, const char *base_data,
      const int64_t fixed_len,
      const int64_t dict_cnt,
      const int32_t *row_ids, const int64_t row_cap,
      common::ObDatum *datums)
  {
    typedef typename ObEncodingTypeInference<false, REF_BYTE_TAG>::Type RefType;
    const RefType *ref_array = reinterpret_cast<const RefType *>(ref_data);
    for (int64_t i = 0; i < row_cap; i++) {
      ObDatum &datum = datums[i];
      const int64_t ref = ref_array[row_ids[i]];
      if (ref >= dict_cnt) {
        datum.set_null();
      } else {
        if (0 == STORE_CLASS) {
          ENCODING_ADAPT_MEMCPY(const_cast<char *>(datum.ptr_), base_data + fixed_len * ref, fixed_len);
        } else if (1 == STORE_CLASS) {
          datum.ptr_ = base_data + fixed_len * ref;
        }
        datum.pack_ = fixed_len;
      }
    }
  }
};

template <int32_t REF_BYTE_TAG, bool IS_SIGNED_SC, int32_t STORE_LEN_TAG, int32_t DATUM_LEN_TAG>
struct DictFixBatchDecodeFunc_T<REF_BYTE_TAG, IS_SIGNED_SC, STORE_LEN_TAG, DATUM_LEN_TAG, 2>
{
  static void dict_fix_batch_decode_func(
      const char *ref_data, const char *base_data,
      const int64_t fixed_len,
      const int64_t dict_cnt,
      const int32_t *row_ids, const int64_t row_cap,
      common::ObDatum *datums)
  {
    UNUSED(fixed_len);
    typedef typename ObEncodingTypeInference<false, REF_BYTE_TAG>::Type RefType;
    typedef typename ObEncodingTypeInference<IS_SIGNED_SC, STORE_LEN_TAG>::Type StoreType;
    typedef typename ObEncodingTypeInference<IS_SIGNED_SC, DATUM_LEN_TAG>::Type DatumType;
    const RefType *ref_array = reinterpret_cast<const RefType *>(ref_data);
    const StoreType *input = reinterpret_cast<const StoreType *>(base_data);
    for (int64_t i = 0; i < row_cap; i++) {
      ObDatum &datum = datums[i];
      const int64_t ref = ref_array[row_ids[i]];
      if (ref >= dict_cnt) {
        datum.set_null();
      } else {
        *reinterpret_cast<DatumType *>(const_cast<char *>(datum.ptr_)) = input[ref];
        datum.pack_ = sizeof(DatumType);
      }
    }
  }
};

template <int32_t REF_BYTE, int32_t OFF_BYTE, int32_t STORE_CLASS>
struct DictVarBatchDecodeFunc_T
{
  static void dict_var_batch_decode_func(const char *ref_data,
                              const char *off_data,
                              const char *base_data,
                              const char *base_data_end,
                              const int64_t dict_cnt,
                              const int32_t *row_ids, const int64_t row_cap,
                              common::ObDatum *datums)
  {
    typedef typename ObEncodingByteLenMap<false, REF_BYTE>::Type RefType;
    typedef typename ObEncodingByteLenMap<false, OFF_BYTE>::Type OffType;
    const RefType *ref_array = reinterpret_cast<const RefType *>(ref_data);
    const OffType *off_array = reinterpret_cast<const OffType *>(off_data);
    for (int64_t i = 0; i < row_cap; i++) {
      ObDatum &datum = datums[i];
      const int64_t ref = ref_array[row_ids[i]];
      if (ref >= dict_cnt) {
        datum.set_null();
      } else {
        int64_t offset = (0 == ref) ? 0 : off_array[ref - 1];
        int64_t data_len = (ref == dict_cnt - 1)
            ? (base_data_end - base_data) - offset
            : off_array[ref] - offset;

        if (0 == STORE_CLASS) {
          ENCODING_ADAPT_MEMCPY(const_cast<char *>(datum.ptr_), base_data + offset, data_len);
        } else {
          datum.ptr_ = base_data + offset;
        }
        datum.pack_ = data_len;
      }
    }
  }
};

// Initialize fast batch decode func family while compiling
static ObMultiDimArray_T<dict_var_batch_decode_func, 3, 3, 2> dict_var_batch_decode_funcs;

template<int32_t REF_BYTE, int32_t OFF_BYTE, int32_t STORE_CLASS>
struct DictVarDecoderArrayInit
{
  bool operator()()
  {
    dict_var_batch_decode_funcs[REF_BYTE][OFF_BYTE][STORE_CLASS]
        = &(DictVarBatchDecodeFunc_T<REF_BYTE, OFF_BYTE, STORE_CLASS>::dict_var_batch_decode_func);
    return true;
  }
};

static bool dict_var_batch_decode_funcs_inited
    = ObNDArrayIniter<DictVarDecoderArrayInit, 3, 3, 2>::apply();


static ObMultiDimArray_T<dict_fix_batch_decode_func, 2, 2, 4, 4, 3> dict_fix_batch_decode_funcs;

template <int32_t REF_BYTE_TAG, int32_t IS_SIGNED_SC, int32_t STORE_LEN_TAG,
    int32_t DATUM_LEN_TAG, int32_t STORE_CLASS>
struct DictFixDecoderArrayInit
{
  bool operator()()
  {
    dict_fix_batch_decode_funcs[REF_BYTE_TAG][IS_SIGNED_SC][STORE_LEN_TAG][DATUM_LEN_TAG][STORE_CLASS]
        = &(DictFixBatchDecodeFunc_T<REF_BYTE_TAG, IS_SIGNED_SC, STORE_LEN_TAG,
                                    DATUM_LEN_TAG, STORE_CLASS>::dict_fix_batch_decode_func);
    return true;
  }
};

static bool dict_fix_batch_decode_funcs_inited
    = ObNDArrayIniter<DictFixDecoderArrayInit, 2, 2, 4, 4, 3>::apply();

int ObDictDecoder::init(const common::ObObjType &store_obj_type, const char *meta_header)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited())) {
    ret = OB_INIT_TWICE;
  } else {
    store_class_ = get_store_class_map()[ob_obj_type_class(store_obj_type)];
    if (ObIntTC == ob_obj_type_class(store_obj_type)) {
      int64_t type_store_size = get_type_size_map()[store_obj_type];
      integer_mask_ = ~INTEGER_MASK_TABLE[type_store_size];
    } else {
      integer_mask_ = 0;
    }
    meta_header_ = reinterpret_cast<const ObDictMetaHeader *>(meta_header);

    if (!meta_header_->is_fix_length_dict()) {
      var_data_ = meta_header_->payload_ + (meta_header_->count_ - 1) * meta_header_->index_byte_;
    }
  }
  return ret;
}

int ObDictDecoder::decode(const ObColumnDecoderCtx &ctx, ObDatum &datum, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len) const
{
  UNUSEDx(data, len, bs);
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(NULL == data || len < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t ref = 0;
    const unsigned char *col_data = reinterpret_cast<unsigned char *>(
        const_cast<ObDictMetaHeader *>(meta_header_)) + ctx.col_header_->length_;

    // get ref value
    if (ctx.is_bit_packing()) {
      if (OB_FAIL(ObBitStream::get(col_data, row_id * meta_header_->row_ref_size_,
          meta_header_->row_ref_size_, ref))) {
      }
    } else {
      MEMCPY(&ref, col_data + row_id * meta_header_->row_ref_size_,
          meta_header_->row_ref_size_);
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(decode(ctx.obj_meta_.get_type(), datum, ref, ctx.col_header_->length_))) {
    }

  }

  return ret;
}

int ObDictDecoder::decode(const common::ObObjType &obj_type, common::ObDatum &datum, const int64_t ref, const int64_t meta_length) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(0 > ref) || OB_UNLIKELY(0 > meta_length)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const char *cell_data = NULL;
    int64_t cell_len = 0;
    const int64_t count = meta_header_->count_;

    // 1. get offset and length
    if (ref >= count || 0 == count) {
      // null or nope
    } else if (meta_header_->is_fix_length_dict()) {
      cell_data = meta_header_->payload_ + ref * meta_header_->data_size_;
      cell_len = meta_header_->data_size_;
    } else {
      const ObIntArrayFuncTable &off_array = ObIntArrayFuncTable::instance(meta_header_->index_byte_);
      int64_t offset = 0;
      if (0 != ref) { // not first, the first offset is 0
        offset = off_array.at_(meta_header_->payload_, ref - 1);
      }
      cell_data = var_data_ + offset;

      if (ref == count - 1) { // the last one
        cell_len = reinterpret_cast<const char *>(meta_header_) + meta_length - cell_data;
      } else {
        cell_len = off_array.at_(meta_header_->payload_, ref) - offset;
      }
    }

    // 2. fill data
    if (OB_FAIL(ret)) {
    } else if (ref >= count) {
      if (ref == count) {
        datum.set_null();
      } else if (ref == count + 1) {
        datum.set_ext();
        datum.no_cv(datum.extend_obj_)->set_ext(common::ObActionFlag::OP_NOP);
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    } else {
      if (OB_FAIL(load_data_to_datum(
          obj_type,
          cell_data,
          cell_len,
          integer_mask_,
          datum))) {
      }
    }
  }

  return ret;
}

#define DICT_UNPACK_REFS(row_ids, row_cap, col_data, datums, ref_size, unpack_type) \
  int64_t row_id = 0; \
  int64_t ref = 0; \
  int64_t bs_len = meta_header_->count_ * ref_size; \
  for (int64_t i = 0; i < row_cap; ++i) { \
    row_id = row_ids[i]; \
    ObBitStream::get<unpack_type>(col_data, row_id * row_ref_size, row_ref_size, bs_len, ref); \
    datums[i].pack_ = static_cast<uint32_t>(ref); \
  }

#define DICT_GET_NULL_COUNT(dict_count, row_ids, row_cap, col_data, ref_size, null_count, unpack_type) \
  int64_t row_id = 0; \
  int64_t ref = 0; \
  int64_t bs_len = meta_header_->count_ * ref_size; \
  for (int64_t i = 0; i < row_cap; ++i) { \
    row_id = row_ids[i]; \
    ObBitStream::get<unpack_type>(col_data, row_id * row_ref_size, row_ref_size, bs_len, ref); \
    if (ref >= dict_count) { \
      null_count++; \
    } \
  }

int ObDictDecoder::batch_get_bitpacked_refs(
    const int32_t *row_ids,
    const int64_t row_cap,
    const unsigned char *col_data,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  const uint8_t row_ref_size = meta_header_->row_ref_size_;
  if (row_ref_size < 10) {
    DICT_UNPACK_REFS(
        row_ids, row_cap, col_data, datums, row_ref_size, ObBitStream::PACKED_LEN_LESS_THAN_10)
  } else if (row_ref_size < 26) {
    DICT_UNPACK_REFS(
        row_ids, row_cap, col_data, datums, row_ref_size, ObBitStream::PACKED_LEN_LESS_THAN_26)
  } else if (row_ref_size <= 64) {
    DICT_UNPACK_REFS(
        row_ids, row_cap, col_data, datums, row_ref_size, ObBitStream::DEFAULT)
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObDictDecoder::batch_get_null_count(
    const int32_t *row_ids,
    const int64_t row_cap,
    const unsigned char *col_data,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  const int64_t dict_count = meta_header_->count_;
  const uint8_t row_ref_size = meta_header_->row_ref_size_;
  if (row_ref_size < 10) {
    DICT_GET_NULL_COUNT(
        dict_count, row_ids, row_cap, col_data, row_ref_size, null_count, ObBitStream::PACKED_LEN_LESS_THAN_10)
  } else if (row_ref_size < 26) {
    DICT_GET_NULL_COUNT(
        dict_count, row_ids, row_cap, col_data, row_ref_size, null_count, ObBitStream::PACKED_LEN_LESS_THAN_26)
  } else if (row_ref_size <= 64) {
    DICT_GET_NULL_COUNT(
        dict_count, row_ids, row_cap, col_data, row_ref_size, null_count, ObBitStream::DEFAULT)
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

#undef DICT_UNPACK_REFS
#undef DICT_GET_NULL_COUNT

// Internal call, not check parameters for performance
int ObDictDecoder::batch_decode(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  UNUSED(row_index);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (fast_decode_valid(ctx)) {
    const ObObjType store_obj_type = ctx.col_header_->get_store_obj_type();
    const ObObjTypeStoreClass store_class = get_store_class_map()[ob_obj_type_class(store_obj_type)];

    const char *ref_data = reinterpret_cast<char *>(
        const_cast<ObDictMetaHeader *>(meta_header_)) + ctx.col_header_->length_;
    const char *base_data = meta_header_->payload_;
    if (meta_header_->is_fix_length_dict()) {
      // Only need store_len for UIntSC/IntSC
      const int64_t store_size = meta_header_->data_size_;
      const ObObjDatumMapType map_type = ObDatum::get_obj_datum_map_type(store_obj_type);
      bool read_as_signed_data = ObIntSC == store_class
                                && store_size == get_type_size_map()[store_obj_type];
      const int64_t func_entry_store_size_idx = (store_class == ObIntSC || store_class == ObUIntSC)
                                              ? store_size : 0;
      dict_fix_batch_decode_func decode_func = dict_fix_batch_decode_funcs
                      [get_value_len_tag_map()[meta_header_->row_ref_size_]]
                      [read_as_signed_data]
                      [get_value_len_tag_map()[func_entry_store_size_idx]]
                      [get_value_len_tag_map()[get_datum_store_len(map_type)]]
                      [get_store_class_tag_map()[store_class]];
      decode_func(ref_data, base_data, store_size, meta_header_->count_, row_ids, row_cap, datums);
    } else {
      dict_var_batch_decode_func decode_func = dict_var_batch_decode_funcs
                      [meta_header_->row_ref_size_]
                      [meta_header_->index_byte_]
                      [get_store_class_tag_map()[store_class]];
      decode_func(ref_data, base_data, var_data_,
               reinterpret_cast<const char *>(meta_header_) + ctx.col_header_->length_,
               meta_header_->count_, row_ids, row_cap, datums);
    }
  } else {
    // Batch read ref to datum.len_
    const unsigned char *col_data = reinterpret_cast<unsigned char *>(
        const_cast<ObDictMetaHeader *>(meta_header_)) + ctx.col_header_->length_;
    const uint8_t row_ref_size = meta_header_->row_ref_size_;
    int64_t row_id = 0;
    if (ctx.is_bit_packing()) {
      if (OB_FAIL(batch_get_bitpacked_refs(row_ids, row_cap, col_data, datums))) {
      }
    } else {
      for (int64_t i = 0; i < row_cap; ++i) {
        row_id = row_ids[i];
        datums[i].pack_ = 0;
        MEMCPY(&datums[i].pack_, col_data + row_id * row_ref_size, row_ref_size);
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(batch_decode_dict(
        ctx.col_header_->get_store_obj_type(),
        cell_datas,
        row_cap,
        ctx.col_header_->length_,
        datums))) {
    }
  }
  return ret;
}

// Internal call, not check parameters for performance
// datums[i].len_ should stand for the reference to dictionary as input parameter
int ObDictDecoder::batch_decode_dict(
    const common::ObObjType &obj_type,
    const char **cell_datas,
    const int64_t row_cap,
    const int64_t meta_length,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t count = meta_header_->count_;
    const char *dict_payload = meta_header_->payload_;

    // Batch locate cell_data
    if (meta_header_->is_fix_length_dict()) {
      const int64_t dict_data_size = meta_header_->data_size_;
      for (int64_t i = 0; i < row_cap; ++i) {
        uint32_t ref = datums[i].pack_;
        if (ref >= count) {
          datums[i].set_null();
        } else {
          int64_t data_offset = ref * dict_data_size;
          cell_datas[i] = dict_payload + data_offset;
          datums[i].pack_ = static_cast<uint32_t>(dict_data_size);
        }
      }
    } else {
      const ObIntArrayFuncTable &off_array
          = ObIntArrayFuncTable::instance(meta_header_->index_byte_);
      const char* end_of_col_meta = reinterpret_cast<const char *>(meta_header_) + meta_length;
      int32_t var_dict_data_len_arr[count];

      var_dict_data_len_arr[0] = off_array.at_(dict_payload, 0);
      int32_t last_dict_element_offset = 1 == count ? 0 : off_array.at_(dict_payload, 0);
      for (int64_t i = 1; i < count - 1; ++i) {
        int32_t curr_dict_element_offset = off_array.at_(dict_payload, i);
        var_dict_data_len_arr[i] = curr_dict_element_offset - last_dict_element_offset;
        last_dict_element_offset = curr_dict_element_offset;
      }
      var_dict_data_len_arr[count - 1] = end_of_col_meta - (var_data_ + last_dict_element_offset);
      for (int64_t i = 0; i < row_cap; ++i) {
        int64_t offset = 0;
        uint32_t ref = datums[i].pack_;
        if (ref < count) {
          // 0 if ref == 0
          offset = off_array.at_(dict_payload - meta_header_->index_byte_, ref)
              & (static_cast<int64_t>(ref == 0) - 1);
          cell_datas[i] = var_data_ + offset;
          datums[i].pack_ = var_dict_data_len_arr[ref];
        } else {
          datums[i].set_null();
        }
      }
    }

    // Batch load data
    if (OB_FAIL(batch_load_data_to_datum(
        obj_type,
        cell_datas,
        row_cap,
        integer_mask_,
        datums))) {
    }
  }
  return ret;
}

bool ObDictDecoder::fast_decode_valid(const ObColumnDecoderCtx &ctx) const
{
  bool valid = false;
  const ObObjTypeStoreClass store_class =
      get_store_class_map()[ob_obj_type_class(ctx.col_header_->get_store_obj_type())];
  if (meta_header_->is_fix_length_dict()) {
    valid = !ctx.is_bit_packing()
          && meta_header_->row_ref_size_ <= 2
          && (store_class >= ObIntSC && store_class <= ObStringSC)
          && dict_fix_batch_decode_funcs_inited;
    if (ObIntSC == store_class || ObUIntSC == store_class) {
      uint16_t store_size = meta_header_->data_size_;
      valid = valid && (store_size == 1 || store_size == 2 || store_size == 4 || store_size == 8);
    }
  } else {
    valid = !ctx.is_bit_packing()
            && (ObNumberSC == store_class || ObDecimalIntSC == store_class || ObStringSC == store_class)
            && meta_header_->index_byte_ <= 2
            && meta_header_->row_ref_size_ <= 2
            && dict_var_batch_decode_funcs_inited;
  }
  return valid;
}

int ObDictDecoder::get_null_count(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    int64_t &null_count) const
{
  UNUSED(row_index);
  int ret = OB_SUCCESS;
  null_count = 0;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t dict_count = meta_header_->count_;
    const unsigned char *col_data = reinterpret_cast<unsigned char *>(
        const_cast<ObDictMetaHeader *>(meta_header_)) + ctx.col_header_->length_;
    const uint8_t row_ref_size = meta_header_->row_ref_size_;
    int64_t row_id = 0;
    uint64_t ref = 0;
    if (ctx.is_bit_packing()) {
      if (OB_FAIL(batch_get_null_count(row_ids, row_cap, col_data, null_count))) {
      }
    } else {
      for (int64_t i = 0; i < row_cap; ++i) {
        row_id = row_ids[i];
        ref = 0;
        MEMCPY(&ref, col_data + row_id * row_ref_size, row_ref_size);
        if (ref >= dict_count) {
          null_count++;
        }
      }
    }
  }
  return ret;
}

/**
 * Filter push down operator for dict encoding.
 * Now we assume most of the micro blocks are small and most of the dictionaries are small
 * (if too large, might use another encoding), so the cost of traverse a column is less than
 * allocate an array to store the rows hit the filter conditions.
 *
 * But this might be unrealistic and could bring bottleneck to the pushdown.
 */
ObDictDecoderIterator ObDictDecoder::begin(
    const ObColumnDecoderCtx *ctx,
    const bool is_padding_mode, 
    const int64_t meta_length) const
{
  return ObDictDecoderIterator(this, ctx, 0, meta_length, is_padding_mode);
}
ObDictDecoderIterator ObDictDecoder::end(
    const ObColumnDecoderCtx *ctx, 
    const bool is_padding_mode, 
    const int64_t meta_length) const
{
  return ObDictDecoderIterator(this, ctx, meta_header_->count_, meta_length, is_padding_mode);
}

int ObDictDecoder::get_distinct_count(int64_t &distinct_count) const
{
  int ret = OB_SUCCESS;
  distinct_count = meta_header_->count_;
  return ret;
}

int ObDictDecoder::read_distinct(
    const ObColumnDecoderCtx &ctx,
    const char **cell_datas,
    storage::ObGroupByCellBase &group_by_cell) const
{
  int ret = OB_SUCCESS;
  bool has_null = false;
  if (OB_FAIL(batch_read_distinct(ctx, cell_datas, ctx.col_header_->length_, group_by_cell))) {
  } else if (OB_FAIL(check_has_null(ctx, ctx.col_header_->length_, has_null))) {
  } else if (has_null) {
    group_by_cell.add_distinct_null_value();
  }
  return ret;
}

int ObDictDecoder::batch_read_distinct(
    const ObColumnDecoderCtx &ctx,
    const char **cell_datas,
    const int64_t meta_length,
    storage::ObGroupByCellBase &group_by_cell) const
{
  int ret = OB_SUCCESS;
  const int64_t count = meta_header_->count_;
  const char *dict_payload = meta_header_->payload_;
  const common::ObObjType obj_type = ctx.col_header_->get_store_obj_type();
  common::ObDatum *datums = group_by_cell.get_group_by_col_datums_to_fill();
  group_by_cell.set_distinct_cnt(count);
  if (meta_header_->is_fix_length_dict()) {
    const int64_t dict_data_size = meta_header_->data_size_;
    const char *dict_payload = meta_header_->payload_;
    for (int64_t i = 0; i < count; ++i) {
      int64_t data_offset = i * dict_data_size;
      cell_datas[i] = dict_payload + data_offset;
      datums[i].pack_ = static_cast<uint32_t>(dict_data_size);
    }
  } else {
    const ObIntArrayFuncTable &off_array = ObIntArrayFuncTable::instance(meta_header_->index_byte_);
    const char* end_of_col_meta = reinterpret_cast<const char *>(meta_header_) + meta_length;

    cell_datas[0] = var_data_;
    int32_t last_dict_element_offset = off_array.at_(dict_payload, 0);
    datums[0].pack_ = last_dict_element_offset;
    for (int64_t i = 1; i < count - 1; ++i) {
      int32_t curr_dict_element_offset = off_array.at_(dict_payload, i);
      cell_datas[i] = var_data_ + off_array.at_(dict_payload - meta_header_->index_byte_, i);
      datums[i].pack_ = curr_dict_element_offset - last_dict_element_offset;
      last_dict_element_offset = curr_dict_element_offset;
    }
    datums[count - 1].pack_ = end_of_col_meta - (var_data_ + last_dict_element_offset);
    cell_datas[count - 1] = var_data_ + off_array.at_(dict_payload - meta_header_->index_byte_, count - 1);
  }
  if (OB_FAIL(batch_load_data_to_datum(obj_type, cell_datas, count, integer_mask_, datums))) {
  }
  return ret;
}

#define LOAD_DICT_UNPACK_REFS(dict_count, row_ids, row_cap, col_data, ref_buf, ref_size, unpack_type) \
  int64_t row_id = 0; \
  int64_t ref = 0; \
  int64_t bs_len = meta_header_->count_ * ref_size; \
  for (int64_t i = 0; i < row_cap; ++i) { \
    row_id = row_ids[i]; \
    ObBitStream::get<unpack_type>(col_data, row_id * row_ref_size, row_ref_size, bs_len, ref); \
    ref_buf[i] = static_cast<uint32_t>(ref); \
  }

int ObDictDecoder::read_reference(
    const ObColumnDecoderCtx &ctx,
    const int32_t *row_ids,
    const int64_t row_cap,
    storage::ObGroupByCellBase &group_by_cell) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t count = meta_header_->count_;
    const unsigned char *col_data = reinterpret_cast<unsigned char *>(
        const_cast<ObDictMetaHeader *>(meta_header_)) + ctx.col_header_->length_;
    const uint8_t row_ref_size = meta_header_->row_ref_size_;
    uint32_t *ref_buf = group_by_cell.get_refs_buf();
    bool has_null = false;

    // Read ref
    MEMSET(ref_buf, 0, sizeof(uint32_t) * row_cap);
    if (ctx.is_bit_packing()) {
      const uint8_t row_ref_size = meta_header_->row_ref_size_;
      if (row_ref_size < 10) {
        LOAD_DICT_UNPACK_REFS(
            count, row_ids, row_cap, col_data, ref_buf, row_ref_size, ObBitStream::PACKED_LEN_LESS_THAN_10)
      } else if (row_ref_size < 26) {
        LOAD_DICT_UNPACK_REFS(
            count, row_ids, row_cap, col_data, ref_buf, row_ref_size, ObBitStream::PACKED_LEN_LESS_THAN_26)
      } else if (row_ref_size <= 64) {
        LOAD_DICT_UNPACK_REFS(
            count, row_ids, row_cap, col_data, ref_buf, row_ref_size, ObBitStream::DEFAULT)
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    } else {
      for (int64_t i = 0; i < row_cap; ++i) {
        MEMCPY(ref_buf + i, col_data + row_ids[i] * row_ref_size, row_ref_size);
      }
    }
  }
  return ret;
}

#define CHECK_NULL_REF(dict_count, row_cap, col_data, ref_size, unpack_type, has_null) \
  int64_t ref = 0; \
  int64_t bs_len = meta_header_->count_ * ref_size; \
  has_null = false; \
  for (int64_t i = 0; !has_null && i < row_cap; ++i) { \
    ObBitStream::get<unpack_type>(col_data, i * row_ref_size, row_ref_size, bs_len, ref); \
    has_null = ref >= dict_count; \
  }

int ObDictDecoder::check_has_null(const ObColumnDecoderCtx &ctx, const int64_t meta_length, bool &has_null) const
{
  int ret = OB_SUCCESS;
  has_null = false;
  const int64_t count = meta_header_->count_;
  const unsigned char *col_data = reinterpret_cast<unsigned char *>(
      const_cast<ObDictMetaHeader *>(meta_header_)) + meta_length;
  const uint8_t row_ref_size = meta_header_->row_ref_size_;
  if (ctx.is_bit_packing()) {
    const uint8_t row_ref_size = meta_header_->row_ref_size_;
    if (row_ref_size < 10) {
      CHECK_NULL_REF(
          count, ctx.micro_block_header_->row_count_, col_data, row_ref_size, ObBitStream::PACKED_LEN_LESS_THAN_10, has_null);
    } else if (row_ref_size < 26) {
      CHECK_NULL_REF(
          count, ctx.micro_block_header_->row_count_, col_data, row_ref_size, ObBitStream::PACKED_LEN_LESS_THAN_26, has_null);
    } else if (row_ref_size <= 64) {
      CHECK_NULL_REF(
          count, ctx.micro_block_header_->row_count_, col_data, row_ref_size, ObBitStream::DEFAULT, has_null);
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  } else {
    uint32_t ref = 0;
    for (int64_t i = 0; !has_null && i < ctx.micro_block_header_->row_count_; ++i) {
      MEMCPY(&ref, col_data + i * row_ref_size, row_ref_size);
      has_null = ref >= count;
    }
  }
  return ret;
}

} // end namespace blocksstable
} // end namespace oceanbase
