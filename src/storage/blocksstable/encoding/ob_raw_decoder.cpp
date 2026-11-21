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
#include "ob_raw_decoder.h"
#include "lib/utility/ob_target_specific.h"

namespace oceanbase
{
using namespace common;
namespace blocksstable
{

class ObIRowIndex;
const ObColumnHeader::Type ObRawDecoder::type_;

// define fast batch decode function family
template <typename T, int32_t IS_ZERO_HEADER_LEN, int32_t IS_LAST_VAR_COL>
OB_INLINE static void get_var_col_offset(
    const T *col_idxes, int64_t header_var_col_cnt, const char *&var_data,
    const int64_t header_len, const int64_t row_len, const char *row_data,
    const char *&col_data, int64_t &col_len)
{
  int64_t col_off = 0;
  var_data += sizeof(T) * (header_var_col_cnt - 1);
  if (!IS_ZERO_HEADER_LEN) {
    col_off = col_idxes[header_len - 1];
  }
  if (IS_LAST_VAR_COL) {
    col_len = row_len - col_off - (var_data - row_data);
  } else {
    col_len = col_idxes[header_len] - col_off;
  }
  col_data = var_data + col_off;
}

// ROW_IDX_BYTE : 0 - null
// STORE_CLASS: {0: ObNumberSC, 1: ObStringSC / WideIntSC, 2: ObIntSC/ObUIntSC}
template <int32_t ROW_IDX_BYTE, bool IS_ONLY_VAR_COL,
    bool IS_ZERO_HEADER_LEN, bool IS_LAST_VAR_COL, int32_t STORE_CLASS>
struct RawVarBatchDecodeFunc_T
{
  static void raw_var_batch_decode_func(
      const char *base_data, const char *row_idx_data,
      const int64_t header_off,
      const int64_t header_len,
      const int64_t header_var_col_cnt,
      const int32_t *row_ids, const int64_t row_cap,
      common::ObDatum *datums)
  {
    typedef typename ObEncodingByteLenMap<false, ROW_IDX_BYTE>::Type RowIdxType;

    const RowIdxType *row_idxes = (const RowIdxType *)row_idx_data;
    for (int64_t i = 0; i < row_cap; i++) {
      const int64_t row_id = row_ids[i];
      const int64_t off = row_idxes[row_id];
      const int64_t row_len = row_idxes[row_id + 1] - off;
      const char *row_data = base_data + off;

      const char *col_data = nullptr;
      int64_t col_len = 0;
      if (IS_ONLY_VAR_COL) {
        col_data = row_data + header_off;
        col_len = row_len - header_off;
      } else {
        const char *var_data = row_data + header_off ;
        int8_t col_idx_byte = *var_data;
        var_data += sizeof(int8_t);

        switch (col_idx_byte) {
        case 1: {
          const uint8_t *col_idxes = reinterpret_cast<const uint8_t *>(var_data);
          get_var_col_offset<uint8_t, IS_ZERO_HEADER_LEN, IS_LAST_VAR_COL>(
            col_idxes, header_var_col_cnt, var_data, header_len,
            row_len, row_data, col_data, col_len);
          break;
        }
        case 2: {
          const uint16_t *col_idxes = reinterpret_cast<const uint16_t *>(var_data);
          get_var_col_offset<uint16_t, IS_ZERO_HEADER_LEN, IS_LAST_VAR_COL>(
            col_idxes, header_var_col_cnt, var_data, header_len,
            row_len, row_data, col_data, col_len);
          break;
        }
        case 4: {
          const uint32_t *col_idxes = reinterpret_cast<const uint32_t *>(var_data);
          get_var_col_offset<uint32_t, IS_ZERO_HEADER_LEN, IS_LAST_VAR_COL>(
            col_idxes, header_var_col_cnt, var_data, header_len,
            row_len, row_data, col_data, col_len);
          break;
        }
        case 8: {
          const uint64_t *col_idxes = reinterpret_cast<const uint64_t *>(var_data);
          get_var_col_offset<uint64_t, IS_ZERO_HEADER_LEN, IS_LAST_VAR_COL>(
            col_idxes, header_var_col_cnt, var_data, header_len,
            row_len, row_data, col_data, col_len);
          break;
        }
        default: {
          LOG_ERROR_RET(OB_ERR_UNEXPECTED, "Invalid column index byte", K(col_idx_byte));
        }
        }
      }

      ObDatum &datum = datums[i];
      if (0 == STORE_CLASS) { // number
        ENCODING_ADAPT_MEMCPY(const_cast<char *>(datum.ptr_), col_data, col_len);
      } else {
        datum.ptr_ = col_data;
      }
      datum.pack_ = col_len;
    }
  }
};

// STORE_CLASS: {0: ObNumberSC, 1: ObStringSC / WideIntSC, 2: ObIntSC/ObUIntSC}
template <bool IS_SIGNED_SC, int32_t STORE_LEN_TAG, int32_t DATUM_LEN_TAG, int32_t STORE_CLASS>
struct RawFixBatchDecodeFunc_T
{
  static void raw_fix_batch_decode_func(
      const int64_t col_len,
      const char *base_data,
      const int32_t *row_ids,
      const int64_t row_cap,
      common::ObDatum *datums)
  {
    for (int64_t i = 0; i < row_cap; i++) {
      const int64_t row_id = row_ids[i];
      ObDatum &datum = datums[i];
      if (STORE_CLASS == 1) {
        datum.ptr_ = base_data + col_len * row_id;
      } else if (STORE_CLASS == 0) {
        ENCODING_ADAPT_MEMCPY(const_cast<char *>(datum.ptr_), base_data + row_id * col_len, col_len);
      }
      datum.pack_ = static_cast<int32_t>(col_len);
    }
  }
};

// decoder for UIntSC / IntSC
template <bool IS_SIGNED_SC, int32_t STORE_LEN_TAG, int32_t DATUM_LEN_TAG>
struct RawFixBatchDecodeFunc_T<IS_SIGNED_SC, STORE_LEN_TAG, DATUM_LEN_TAG, 2>
{
  static void raw_fix_batch_decode_func(
      const int64_t col_len,
      const char *base_data,
      const int32_t *row_ids,
      const int64_t row_cap,
      common::ObDatum *datums)
  {
    UNUSED(col_len);
    // TODO: @saitong.zst SIMD on Batch integer decode
    typedef typename ObEncodingTypeInference<IS_SIGNED_SC, STORE_LEN_TAG>::Type StoreType;
    typedef typename ObEncodingTypeInference<IS_SIGNED_SC, DATUM_LEN_TAG>::Type DatumType;
    const StoreType *input = reinterpret_cast<const StoreType *>(base_data);
    for (int64_t i = 0; i < row_cap; i++) {
      const int64_t row_id = row_ids[i];
      *reinterpret_cast<DatumType *>(const_cast<char *>(datums[i].ptr_)) = input[row_id];
      datums[i].pack_ = sizeof(DatumType);
    }
  }
};

// Initialize fast batch decode func family while compiling
static ObMultiDimArray_T<raw_fix_batch_decode_func, 2, 4, 4, 3> raw_fix_batch_decode_funcs;

template <int32_t IS_SIGNED_SC, int32_t STORE_LEN_TAG, int32_t DATUM_LEN_TAG, int32_t STORE_CLASS>
struct RawFixDecoderArrayInit
{
  bool operator()()
  {
    raw_fix_batch_decode_funcs[IS_SIGNED_SC][STORE_LEN_TAG][DATUM_LEN_TAG][STORE_CLASS]
        = &(RawFixBatchDecodeFunc_T<IS_SIGNED_SC, STORE_LEN_TAG, DATUM_LEN_TAG, STORE_CLASS>::raw_fix_batch_decode_func);
    return true;
  }
};

static bool raw_fix_batch_decode_funcs_inited
    = ObNDArrayIniter<RawFixDecoderArrayInit, 2, 4, 4, 3>::apply();


static ObMultiDimArray_T<raw_var_batch_decode_func, 3, 2, 2, 2, 2> var_batch_decode_funcs;

template <int32_t ROW_IDX_BYTE, int32_t IS_ONLY_VAR_COL,
    int32_t IS_ZERO_HEADER_LEN, int32_t IS_LAST_VAR_COL, int32_t STORE_CLASS>
struct RawVarDecoderArrayInit
{
  bool operator()()
  {
    var_batch_decode_funcs[ROW_IDX_BYTE][IS_ONLY_VAR_COL][IS_ZERO_HEADER_LEN][IS_LAST_VAR_COL][STORE_CLASS]
        = &(RawVarBatchDecodeFunc_T<ROW_IDX_BYTE, IS_ONLY_VAR_COL, IS_ZERO_HEADER_LEN,
                                    IS_LAST_VAR_COL, STORE_CLASS>::raw_var_batch_decode_func);
    return true;
  }
};

static bool raw_var_batch_decode_funcs_inited
    = ObNDArrayIniter<RawVarDecoderArrayInit, 3, 2, 2, 2, 2>::apply();

int ObRawDecoder::decode(const ObColumnDecoderCtx &ctx, common::ObDatum &datum, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len) const
{
  int ret = OB_SUCCESS;
  uint64_t val = STORED_NOT_EXT;
  int64_t data_offset = 0;

  const unsigned char *col_data = reinterpret_cast<const unsigned char *>(meta_data_);
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (NULL == data || len < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // read extend value bit
    if (ctx.has_extend_value()) {
      if (ctx.is_fix_length() || ctx.is_bit_packing()) {
        data_offset = ctx.micro_block_header_->row_count_ * ctx.micro_block_header_->extend_value_bit_;
        if (OB_FAIL(ObBitStream::get(col_data, row_id * ctx.micro_block_header_->extend_value_bit_,
            ctx.micro_block_header_->extend_value_bit_, val))) {
        }
      } else {
        if (OB_FAIL(bs.get(ctx.col_header_->extend_value_index_,
                           ctx.micro_block_header_->extend_value_bit_,
                           val))) {
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (STORED_NOT_EXT != val) {
    set_stored_ext_value(datum, static_cast<ObStoredExtValue>(val));
  } else {
    // read bit packing value
    if (ctx.is_bit_packing()) {
      uint32_t datum_len = 0;
      val = 0;
      if (OB_FAIL(get_uint_data_datum_len(
          ObDatum::get_obj_datum_map_type(ctx.obj_meta_.get_type()),
          datum_len))) {
      } else {
        datum.pack_ = datum_len;
        if (OB_FAIL(ObBitStream::get(col_data, data_offset + row_id * ctx.col_header_->length_,
            ctx.col_header_->length_, val))) {
        } else {
          MEMCPY(const_cast<char *>(datum.ptr_), &val, datum_len);
        }
      }
    } else {
      const char *cell_data = NULL;
      int64_t cell_len = 0;
      // get cell data offset and length
      if (ctx.is_fix_length()) {
        data_offset = (data_offset + CHAR_BIT - 1) / CHAR_BIT;
        cell_data = meta_data_ + data_offset + row_id * ctx.col_header_->length_;
        cell_len = ctx.col_header_->length_;
      } else {
        if (OB_FAIL(locate_cell_data(cell_data, cell_len, data, len,
                *ctx.micro_block_header_, *ctx.col_header_, *ctx.col_header_))) {
        }
      }
      if (OB_FAIL(ret)){
      } else if (OB_FAIL(load_data_to_datum(
          ctx.obj_meta_.get_type(),
          cell_data,
          cell_len,
          integer_mask_,
          datum))) {
      }
    }
  }
  return ret;
}

/**
 * Internal call, not check parameters for performance
 * Potential Optimization:
 *  1. Loop ununrolling and parallelization with SIMD
 *  2. Batch operation in ObBitStream
 */
int ObRawDecoder::batch_decode(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if (fast_decode_valid(ctx)) {
    if (OB_FAIL(batch_decode_fast(ctx, row_index, row_ids, row_cap, datums))) {
    }
  } else if (OB_FAIL(batch_decode_general(ctx, row_index, row_ids, cell_datas, row_cap, datums))) {
  }
  return ret;
}

bool ObRawDecoder::fast_decode_valid(const ObColumnDecoderCtx &ctx) const
{
  bool valid = false;
  const ObColumnHeader *col_header = ctx.col_header_;
  const ObMicroBlockHeader *block_header = ctx.micro_block_header_;
  const ObObjTypeStoreClass store_class =
      get_store_class_map()[ob_obj_type_class(ctx.col_header_->get_store_obj_type())];
  if (col_header->is_fix_length()) {
    valid = !col_header->is_bit_packing()
        && !col_header->has_extend_value()
        && (store_class >= ObIntSC && store_class <= ObStringSC)
        && raw_fix_batch_decode_funcs_inited;
    if (valid && (ObIntSC == store_class || ObUIntSC == store_class)) {
      uint32_t store_size = col_header->length_;
      valid = (store_size == 1 || store_size == 2 || store_size == 4 || store_size == 8);
    }
  } else {
    uint8_t row_idx_byte = block_header->row_index_byte_;
    bool row_idx_byte_valid = (1 == row_idx_byte || 2 == row_idx_byte);
    valid = !col_header->is_bit_packing()
        && !col_header->has_extend_value()
        && row_idx_byte_valid
        && (ObNumberSC == store_class || ObDecimalIntSC == store_class || ObStringSC == store_class)
        && raw_var_batch_decode_funcs_inited;
  }
  return valid;
}

int ObRawDecoder::batch_decode_fast(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  // Optimized decode for byte-packing data
  const ObObjType store_type = ctx.col_header_->get_store_obj_type();
  const ObObjTypeStoreClass store_class = get_store_class_map()[ob_obj_type_class(store_type)];
  if (ctx.is_fix_length()) {
    const ObObjDatumMapType map_type = ObDatum::get_obj_datum_map_type(store_type);
    // Only need store_len for UIntSC/IntSC
    uint32_t store_len = ctx.col_header_->length_ > 8 ? 0 : ctx.col_header_->length_;
    raw_fix_batch_decode_func decode_func = raw_fix_batch_decode_funcs
        [ObIntSC == store_class
            && ctx.col_header_->length_ == get_type_size_map()[store_type]]
        [get_value_len_tag_map()[store_len]]
        [get_value_len_tag_map()[get_datum_store_len(map_type)]]
        [get_store_class_tag_map()[store_class]];
    decode_func(ctx.col_header_->length_, meta_data_, row_ids, row_cap, datums);
  } else {
    const ObVarRowIndex *var_row_idx = static_cast<const ObVarRowIndex *>(row_index);
    const char *base_data = var_row_idx->get_data();
    const char *row_idx_data = reinterpret_cast<const ObIntegerArray<uint64_t> *>(
                                    var_row_idx->get_index_data())->get_data();
    raw_var_batch_decode_func decode_func = var_batch_decode_funcs
                  [ctx.micro_block_header_->row_index_byte_]
                  [1 == ctx.micro_block_header_->var_column_count_]
                  [0 == ctx.col_header_->length_]
                  [ctx.col_header_->is_last_var_field()]
                  [get_store_class_tag_map()[store_class]];
    decode_func(
        base_data, row_idx_data, ctx.col_header_->offset_, ctx.col_header_->length_,
        ctx.micro_block_header_->var_column_count_, row_ids, row_cap, datums);
  }
  return ret;
}

// private call, not check parameters
int ObRawDecoder::batch_decode_general(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex* row_index,
    const int32_t *row_ids,
    const char **cell_datas,
    const int64_t row_cap,
    common::ObDatum *datums) const
{
  int ret = OB_SUCCESS;
  const unsigned char *col_data = reinterpret_cast<const unsigned char *>(meta_data_);
  int64_t data_offset =0;

  // Set Extend(null) value if any
  if (ctx.has_extend_value()) {
    if (ctx.is_fix_length() || ctx.is_bit_packing()) {
      data_offset = ctx.micro_block_header_->row_count_
          * ctx.micro_block_header_->extend_value_bit_;
      if (OB_FAIL(set_null_datums_from_fixed_column(
          ctx, row_ids, row_cap, col_data, datums))) {
      }
    } else if (OB_FAIL(set_null_datums_from_var_column(
        ctx, row_index, row_ids, row_cap, datums))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (ctx.is_bit_packing()) {
    int64_t row_id = 0;
    uint32_t datum_len = 0;
    uint64_t value = 0;
    if (OB_FAIL(get_uint_data_datum_len(
        ObDatum::get_obj_datum_map_type(ctx.col_header_->get_store_obj_type()),
        datum_len))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
      if (ctx.has_extend_value() && datums[i].is_null()) {
        // Do nothing
      } else {
        row_id = row_ids[i];
        datums[i].pack_ = datum_len;
        value = 0;
        if (OB_FAIL(ObBitStream::get(
            col_data,
            data_offset + row_id * ctx.col_header_->length_,
            ctx.col_header_->length_,
            value))) {
        } else {
            MEMCPY(const_cast<char *>(datums[i].ptr_), &value, datum_len);
        }
      }
    }
  } else {
    int64_t row_id = 0;
    if (ctx.is_fix_length()) {
      data_offset = (data_offset + CHAR_BIT - 1) / CHAR_BIT;
      for (int64_t i = 0; i < row_cap; ++i) {
        if (ctx.has_extend_value() && datums[i].is_null()) {
        } else {
          row_id = row_ids[i];
          cell_datas[i] = meta_data_ + data_offset + row_id * ctx.col_header_->length_;
          datums[i].pack_ = static_cast<uint32_t>(ctx.col_header_->length_);
        }
      }
    } else {
      if (OB_FAIL(batch_locate_row_data(ctx, row_index, row_ids, row_cap, cell_datas, datums))) {
      } else if (OB_FAIL(batch_locate_cell_data(cell_datas, datums, row_cap,
          *ctx.micro_block_header_, *ctx.col_header_, *ctx.col_header_))) {
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(batch_load_data_to_datum(
        ctx.col_header_->get_store_obj_type(),
        cell_datas,
        row_cap,
        integer_mask_,
        datums))) {
    }
  }
  return ret;
}

/**
 * API for pushdown operator. Objects with store class as ObTextSC is not supported yet.
 * Null object is not greater than, less than or equal to any object.
 */
int ObRawDecoder::get_null_count(
    const ObColumnDecoderCtx &ctx,
    const ObIRowIndex *row_index,
    const int32_t *row_ids,
    const int64_t row_cap,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
  } else if OB_FAIL(ObIColumnDecoder::get_null_count_from_extend_value(
      ctx,
      row_index,
      row_ids,
      row_cap,
      meta_data_,
      null_count)) {
  }
  return ret;
}
template <typename DataType>
struct RawEqualsOp
{
  static uint8_t apply(DataType a, DataType b) { return a == b; }
};

template <typename DataType>
struct RawNotEqualsOp
{
  static uint8_t apply(DataType a, DataType b) { return a != b; }
};

template <typename DataType>
struct RawGreaterOp
{
  static uint8_t apply(DataType a, DataType b) { return a > b; }
};

template <typename DataType>
struct RawLessOp
{
  static uint8_t apply(DataType a, DataType b) { return a < b; }
};

template <typename DataType>
struct RawGreaterOrEqualsOp
{
  static uint8_t apply(DataType a, DataType b) { return a >= b; }
};

template <typename DataType>
struct RawLessOrEqualsOp
{
  static uint8_t apply(DataType a, DataType b) { return a <= b; }
};

template <typename DataType, typename Op>
class RawAggFunctionImpl
{
  public:
  // can use SIMD
  OB_MULTITARGET_FUNCTION_AVX2_SSE42(
  OB_MULTITARGET_FUNCTION_HEADER(static void), raw_min_max_function, OB_MULTITARGET_FUNCTION_BODY((
      const unsigned char* raw_data,
      uint32_t from,
      uint32_t to,
      uint64_t &res)
  {
    const DataType *start_pos = reinterpret_cast<const DataType *>(raw_data);
    const DataType *a_end = start_pos + to;
    const DataType * __restrict a_pos = start_pos + from;
    DataType res_value = *(start_pos + from);
    while (a_pos < a_end) {
      if (Op::apply(*a_pos, res_value)) {
        res_value = *a_pos;
      }
      ++a_pos;
    }
    res = res_value;
  }))

};

template <bool IS_SIGNED, int32_t LEN_TAG>
struct RawAggFunctionProducer
{
  static raw_min_max_function produce_min_max(
    const uint32_t type)
  {
    raw_min_max_function min_max_function = nullptr;
    typedef typename ObEncodingTypeInference<IS_SIGNED, LEN_TAG>::Type DataType;
    const bool is_supported = is_arch_supported(ObTargetArch::AVX2);
    switch (type) {
      case 0: // min
#if OB_USE_MULTITARGET_CODE
        if (is_supported) {
          min_max_function = RawAggFunctionImpl<DataType, RawLessOp<DataType>>::raw_min_max_function_avx2;
        } else {
          min_max_function = RawAggFunctionImpl<DataType, RawLessOp<DataType>>::raw_min_max_function;
        }
#else
        min_max_function = RawAggFunctionImpl<DataType, RawLessOp<DataType>>::raw_min_max_function;
#endif
        break;
      case 1:
#if OB_USE_MULTITARGET_CODE
        if (is_supported) {
          min_max_function = RawAggFunctionImpl<DataType, RawGreaterOp<DataType>>::raw_min_max_function_avx2;
        } else {
          min_max_function = RawAggFunctionImpl<DataType, RawGreaterOp<DataType>>::raw_min_max_function;
        }
#else
        min_max_function = RawAggFunctionImpl<DataType, RawGreaterOp<DataType>>::raw_min_max_function;
#endif
        break;
      default:
        LOG_ERROR_RET(OB_ERR_UNEXPECTED, "Invalid min max type", K(type));
        break;
    }
    return min_max_function;
  }

};

RawAggFunctionFactory::RawAggFunctionFactory()
{
  for (uint32_t k = 0; k < MIN_MAX_CNT; ++k) {
    min_max_functions_array_[0][k] = RawAggFunctionProducer<0, 0>::produce_min_max(k);
    min_max_functions_array_[1][k] = RawAggFunctionProducer<0, 1>::produce_min_max(k);
    min_max_functions_array_[2][k] = RawAggFunctionProducer<0, 2>::produce_min_max(k);
    min_max_functions_array_[3][k] = RawAggFunctionProducer<0, 3>::produce_min_max(k);

  }
}

RawAggFunctionFactory &RawAggFunctionFactory::instance()
{
  static RawAggFunctionFactory ret;
  return ret;
}

raw_min_max_function RawAggFunctionFactory::get_min_max_function(
    const int32_t fix_len_tag,
    const bool is_min)
{
  raw_min_max_function min_max_function = nullptr;
  if (OB_UNLIKELY(fix_len_tag < 0 || fix_len_tag >= FIX_LEN_TAG_CNT)) {
  } else {
    min_max_function = min_max_functions_array_[fix_len_tag][is_min ? 0 : 1];
  }
  return min_max_function;
}

} // end namespace blocksstable
} // end namespace oceanbase
