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

#define USING_LOG_PREFIX STORAGE

#include "storage/ddl/ob_ddl_encode_sortkey_utils.h"
#include "storage/ob_order_perserving_encoder.h"

namespace oceanbase
{
using namespace blocksstable;
namespace storage
{
namespace
{
int compare_encoded_sortkey(const ObDatum &l, const ObDatum &r)
{
  const int64_t min_len = MIN(l.len_, r.len_);
  int cmp_ret = MEMCMP(l.ptr_, r.ptr_, min_len);
  if (0 == cmp_ret) {
    cmp_ret = (l.len_ < r.len_) ? -1 : ((l.len_ > r.len_) ? 1 : 0);
  }
  return cmp_ret;
}

int radix_sort_fixed_key_rows(
    common::ObFixedArray<ObFtsSegmentSortRow *, ObIAllocator> &row_ptrs,
    common::ObIAllocator &allocator,
    const int64_t key_len)
{
  int ret = OB_SUCCESS;
  static constexpr int64_t BUCKET_CNT = 256;
  const int64_t row_cnt = row_ptrs.count();
  if (row_cnt <= 1) {
  } else {
    ObFtsSegmentSortRow **src = &row_ptrs.at(0);
    ObFtsSegmentSortRow **tmp = static_cast<ObFtsSegmentSortRow **>(
        allocator.alloc(sizeof(ObFtsSegmentSortRow *) * row_cnt));
    if (OB_ISNULL(tmp)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc radix sort temp buffer", K(ret), K(row_cnt), K(key_len));
    } else {
      ObFtsSegmentSortRow **dst = tmp;
      for (int64_t offset = key_len - 1; OB_SUCC(ret) && offset >= 0; --offset) {
        int64_t counts[BUCKET_CNT] = {0};
        int64_t positions[BUCKET_CNT] = {0};
        for (int64_t i = 0; i < row_cnt; ++i) {
          const unsigned char byte_val = static_cast<unsigned char>(src[i]->encode_datum_.ptr_[offset]);
          ++counts[byte_val];
        }
        for (int64_t i = 1; i < BUCKET_CNT; ++i) {
          positions[i] = positions[i - 1] + counts[i - 1];
        }
        for (int64_t i = 0; i < row_cnt; ++i) {
          const unsigned char byte_val = static_cast<unsigned char>(src[i]->encode_datum_.ptr_[offset]);
          dst[positions[byte_val]++] = src[i];
        }
        std::swap(src, dst);
      }
      if (OB_SUCC(ret) && src != &row_ptrs.at(0)) {
        MEMCPY(&row_ptrs.at(0), src, sizeof(ObFtsSegmentSortRow *) * row_cnt);
      }
      allocator.free(tmp);
    }
  }
  return ret;
}
} // namespace

int ObDDLEncodeSortkeyUtils::fill_encode_sortkey_column_item(const bool is_oracle_mode, ObColumnSchemaItem &item)
{
  int ret = OB_SUCCESS;
  item.is_valid_ = true;
  item.col_type_.set_varchar();
  item.col_type_.set_collation_type(CS_TYPE_BINARY);
  item.col_type_.set_collation_level(CS_LEVEL_COERCIBLE);
  UNUSED(is_oracle_mode);
  item.col_accuracy_.set_full_length(OB_MAX_ROW_LENGTH, LS_BYTE);
  item.is_nullable_ = true;
  // other fields should not be used
  return ret;
}

int ObDDLEncodeSortkeyUtils::prepare_encode_param(
    const ObIArray<ObObjMeta> &column_descs,
    const bool is_oracle_mode,
    ObIArray<share::ObEncParam> &params)
{
  int ret = OB_SUCCESS;
  const int64_t sortkey_cnt = column_descs.count();
  bool is_null_first = !is_oracle_mode; // see default_asc_direction
  bool is_asc = true;
  if (OB_FAIL(params.prepare_allocate(sortkey_cnt))) {
    LOG_WARN("failed to prepare allocate", K(ret), K(sortkey_cnt));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < sortkey_cnt; i++) {
      share::ObEncParam &param = params.at(i);
      param.type_ = column_descs.at(i).get_type();
      param.cs_type_ = column_descs.at(i).get_collation_type();
      param.is_var_len_ = false; // is_var_len_ is unused
      param.is_memcmp_ = is_oracle_mode;
      param.is_nullable_ = true;
#if OB_USE_MULTITARGET_CODE
      int tmp_ret = OB_SUCCESS;
      tmp_ret = OB_E(EventTable::EN_DISABLE_ENCODESORTKEY_OPT) OB_SUCCESS;
      if (OB_SUCCESS != tmp_ret) {
        param.is_simdopt_ = false;
      }
#else
      param.is_simdopt_ = false;
#endif
      param.is_null_first_ = is_null_first;
      param.is_asc_ = is_asc;
    }
  }
  return ret;
}

// ith sortkey's param is params[i], ith sortkey's datum is datums[sortkey_idxs[i]]
int ObDDLEncodeSortkeyUtils::encode_row(
    const ObDatum *datums,
    const int64_t datum_cnt,
    const ObIArray<int64_t> &sortkey_idxs,
    const int64_t init_encode_row_buf_len,
    ObIArray<share::ObEncParam> &params,
    ObIAllocator &allocator,
    ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  int64_t max_len = init_encode_row_buf_len;
  if (OB_UNLIKELY(sortkey_idxs.count() != params.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(sortkey_idxs), K(params));
  } else {
    while (true) {
      ret = OB_SUCCESS;
      int64_t data_len = 0;
      unsigned char *buf = reinterpret_cast<unsigned char *>(allocator.alloc(max_len));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("invalid argument", K(ret), K(max_len));
      }
      bool has_invalid_uni = false;
      for (int64_t i = 0; OB_SUCC(ret) && i < sortkey_idxs.count(); i++) {
        int64_t col_idx = sortkey_idxs.at(i);
        if (OB_UNLIKELY(col_idx >= datum_cnt)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid col idx", K(ret), K(col_idx), K(datum_cnt));
        }
        if (OB_SUCC(ret)) {
          int64_t tmp_data_len = 0;
          ObDatum datum = datums[col_idx];
          params.at(i).is_valid_uni_ = true;
          if (OB_FAIL(share::ObSortkeyConditioner::process_key_conditioning(
                       datum, buf + data_len, max_len - data_len,
                       tmp_data_len, params.at(i)))) {
            if (ret != OB_BUF_NOT_ENOUGH) {
              LOG_WARN("failed to encode", K(ret), K(data_len), K(max_len), K(i), K(datum), K(params.at(i)));
            }
          } else {
            if (!params.at(i).is_valid_uni_) {
              has_invalid_uni = true;
            }
            data_len += tmp_data_len;
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (has_invalid_uni) {
          res_datum.set_null(); // sort impl should fallback to original sortkey
        } else {
          res_datum.set_string(ObString(data_len, reinterpret_cast<char *>(buf)));
        }
      } else if (ret == OB_BUF_NOT_ENOUGH) {
        max_len = max_len * 2;
        continue;
      }
      break;
    }
  }
  return ret;
}

// ith sortkey's column desc is column_descs[i], ith sortkey's vector is batch_rows.vectors_[sortkey_idxs[i]]
int ObDDLEncodeSortkeyUtils::encode_batch(
    const ObBatchDatumRows &batch_rows,
    const ObIArray<int64_t> &sortkey_idxs,
    const ObIArray<ObObjMeta> &column_descs,
    const bool is_oracle_mode,
    ObIAllocator &allocator,
    ObDirectLoadBatchRows &encode_sortkeys)
{
  int ret = OB_SUCCESS;
  const int64_t row_cnt = batch_rows.row_count_;
  const int64_t column_cnt = batch_rows.get_column_count();
  ObSEArray<share::ObEncParam, 4> params;
  ObSEArray<ObColumnSchemaItem, 1> encode_column_items;
  ObDirectLoadRowFlag encode_row_flag;
  ObFixedArray<ObDatum, ObIAllocator> encode_datums(&allocator);
  ObFixedArray<ObDatum, ObIAllocator> tmp_datums(&allocator);
  if (OB_UNLIKELY(column_descs.count() != sortkey_idxs.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpected column desc count", K(ret), K(column_descs), K(sortkey_idxs), K(column_cnt));
  } else if (OB_FAIL(prepare_encode_param(column_descs, is_oracle_mode, params))) {
    LOG_WARN("failed to prepare encode param", K(ret), K(sortkey_idxs), K(column_descs), K(is_oracle_mode));
  } else if (OB_UNLIKELY(params.count() != sortkey_idxs.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid params", K(ret), K(params), K(sortkey_idxs));
  } else if (OB_FAIL(encode_column_items.prepare_allocate(1))) {
    LOG_WARN("failed to prepare allocate", K(ret), K(column_cnt));
  } else if (OB_FAIL(fill_encode_sortkey_column_item(is_oracle_mode, encode_column_items.at(0)))) {
    LOG_WARN("failed to fill colum item", K(ret));
  } else if (OB_FAIL(encode_sortkeys.init(encode_column_items, row_cnt, encode_row_flag))) {
    LOG_WARN("failed to encode row flag", K(ret), K(encode_column_items), K(row_cnt), K(encode_row_flag));
  } else if (OB_FAIL(encode_datums.prepare_allocate(row_cnt))) {
    LOG_WARN("failed to init datums", K(ret), K(row_cnt));
  } else if (OB_FAIL(tmp_datums.prepare_allocate(column_cnt))) {
    LOG_WARN("failed to init datums", K(ret), K(column_cnt));
  }

  for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_cnt; row_idx++) {
    const int64_t max_len = 32;
    ObDatum &res_datum = encode_datums.at(row_idx);
    for (int64_t i = 0; OB_SUCC(ret) && i < sortkey_idxs.count(); i++) {
      int64_t col_idx = sortkey_idxs.at(i);
      const char *pay_load = nullptr;
      bool is_null = false;
      ObLength length = 0;
      if (OB_UNLIKELY(col_idx >= column_cnt)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid col idx", K(ret), K(col_idx), K(column_cnt));
      } else if (OB_ISNULL(batch_rows.vectors_.at(col_idx))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("vec should not be null", KR(ret), K(col_idx));
      } else {
        batch_rows.vectors_.at(col_idx)->get_payload(row_idx, is_null, pay_load, length);
        if (is_null) {
          tmp_datums.at(col_idx).set_null();
        } else {
          tmp_datums.at(col_idx) = ObDatum(pay_load, length, is_null);
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(encode_row(&tmp_datums.at(0), column_cnt, sortkey_idxs, max_len, params, allocator, res_datum))) {
      LOG_WARN("failed to encode", K(ret), K(max_len));
    }
  }

  if (OB_SUCC(ret)) {
    ObSEArray<ObDatum *, 1> datum_ptrs;
    for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_cnt; ++row_idx) {
      datum_ptrs.reset();
      if (OB_FAIL(datum_ptrs.push_back(&encode_datums.at(row_idx)))) {
        LOG_WARN("failed to push back datum ptr", K(ret), K(row_idx));
      } else if (OB_FAIL(encode_sortkeys.append_row(datum_ptrs))) {
        LOG_WARN("failed to append encode sortkey row", K(ret), K(row_idx));
      }
    }
  }
  return ret;
}

bool ObFtsSegmentSortRow::is_null(const int64_t col_idx) const
{
  OB_ASSERT(col_idx == 0);
  return encode_datum_.is_null();
}

const char *ObFtsSegmentSortRow::get_cell_payload(const sql::RowMeta &row_meta, const int64_t col_idx) const
{
  UNUSED(row_meta);
  OB_ASSERT(col_idx == 0);
  return encode_datum_.ptr_;
}

uint32_t ObFtsSegmentSortRow::get_length(const sql::RowMeta &row_meta, const int64_t col_idx) const
{
  UNUSED(row_meta);
  OB_ASSERT(col_idx == 0);
  return encode_datum_.len_;
}

bool ObFtsSegmentSortCompare::compare(const ObFtsSegmentSortRow *l, const ObFtsSegmentSortRow *r) const
{
  int cmp_ret = 0;
  if (OB_SUCCESS == ret_) {
    if (encode_sortkey_
        && OB_NOT_NULL(l)
        && OB_NOT_NULL(r)
        && !l->encode_datum_.is_null()
        && !r->encode_datum_.is_null()) {
      cmp_ret = compare_encoded_sortkey(l->encode_datum_, r->encode_datum_);
    } else if (OB_SUCCESS != (ret_ = cmp_func_(l->token_pair_->first.get_word(), r->token_pair_->first.get_word(), cmp_ret))) {
      cmp_ret = 0;
    }
  }
  return cmp_ret < 0;
}

ObFtsSegmentSort::ObFtsSegmentSort()
  : is_inited_(false),
    word_meta_(),
    can_encode_sortkey_(false),
    can_fixed_key_sort_(false),
    fixed_key_len_(0),
    mem_context_(nullptr),
    params_(),
    rows_(),
    row_ptrs_()
{
}

int ObFtsSegmentSort::init(
    const ObObjMeta &word_meta,
    const bool is_oracle_mode,
    const int64_t row_count,
    lib::MemoryContext *mem_context)
{
  int ret = OB_SUCCESS;
  mem_context_ = mem_context;
  word_meta_ = word_meta;
  rows_.set_allocator(&mem_context->ref_context()->get_malloc_allocator());
  row_ptrs_.set_allocator(&mem_context->ref_context()->get_malloc_allocator());
  can_encode_sortkey_ = false;
  can_fixed_key_sort_ = false;
  fixed_key_len_ = 0;
  if (ObOrderPerservingEncoder::can_encode_sortkey(word_meta.get_type(), word_meta.get_collation_type())) {
    ObSEArray<ObObjMeta, 1> column_descs;
    can_encode_sortkey_ = true;
    can_fixed_key_sort_ = true;
    if (OB_FAIL(column_descs.push_back(word_meta))) {
      LOG_WARN("failed to push back", K(ret));
    } else if (OB_FAIL(ObDDLEncodeSortkeyUtils::prepare_encode_param(column_descs, is_oracle_mode, params_))) {
      LOG_WARN("failed to prepare encode params", K(ret), K(column_descs), K(is_oracle_mode));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rows_.reserve(row_count))) {
    LOG_WARN("failed to reserve", K(ret), K(row_count));
  } else if (OB_FAIL(row_ptrs_.reserve(row_count))) {
    LOG_WARN("failed to reserve", K(ret), K(row_count));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObFtsSegmentSort::add_row(const hash::HashMapPair<ObFTWord, int64_t> &token_pair)
{
  int ret = OB_SUCCESS;
  ObFtsSegmentSortRow row;
  row.token_pair_ = &token_pair;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (can_encode_sortkey_) {
    ObSEArray<int64_t, 1> sortkey_idxs;
    if (OB_FAIL(sortkey_idxs.push_back(0))) {
      LOG_WARN("failed to push back", K(ret));
    } else if (OB_FAIL(ObDDLEncodeSortkeyUtils::encode_row(&token_pair.first.get_word(), 1, sortkey_idxs,
            INIT_ENCODE_ROW_BUF_LEN, params_, mem_context_->ref_context()->get_arena_allocator(), row.encode_datum_))) {
      LOG_WARN("failed to encode row", K(ret));
    } else if (row.encode_datum_.is_null()) {
      can_encode_sortkey_ = false;
      can_fixed_key_sort_ = false;
    } else if (row.encode_datum_.len_ < MIN_FIXED_ENCODE_KEY_LEN || row.encode_datum_.len_ > MAX_FIXED_ENCODE_KEY_LEN) {
      can_fixed_key_sort_ = false;
    } else if (0 == fixed_key_len_) {
      fixed_key_len_ = row.encode_datum_.len_;
    } else if (fixed_key_len_ != row.encode_datum_.len_) {
      can_fixed_key_sort_ = false;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rows_.push_back(row))) {
    LOG_WARN("failed to push back", K(ret));
  }
  return ret;
}

int ObFtsSegmentSort::sort_fixed_key_inmem_data()
{
  int ret = OB_SUCCESS;
  if (rows_.count() > 1) {
    ret = radix_sort_fixed_key_rows(
        row_ptrs_,
        mem_context_->ref_context()->get_malloc_allocator(),
        fixed_key_len_);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to radix sort fixed key rows", K(ret), K(fixed_key_len_), K(row_ptrs_.count()));
    }
  }
  return ret;
}

int ObFtsSegmentSort::sort_inmem_data()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    row_ptrs_.reuse();
    for (int64_t i = 0; OB_SUCC(ret) && i < rows_.count(); i++) {
      if (OB_FAIL(row_ptrs_.push_back(&rows_.at(i)))) {
        LOG_WARN("failed to push back", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (can_fixed_key_sort_ && fixed_key_len_ >= MIN_FIXED_ENCODE_KEY_LEN && fixed_key_len_ <= MAX_FIXED_ENCODE_KEY_LEN) {
      if (OB_FAIL(sort_fixed_key_inmem_data())) {
        LOG_WARN("failed to sort fixed-key rows", K(ret), K(fixed_key_len_), K(rows_.count()));
      }
    } else {
      ObDatumCmpFuncType cmp_func = get_datum_cmp_func(word_meta_, word_meta_);
      ObFtsSegmentSortCompare cmp(ret, cmp_func);
      cmp.encode_sortkey_ = can_encode_sortkey_;
      lib::ob_sort(row_ptrs_.begin(), row_ptrs_.end(), cmp);
      if (OB_FAIL(ret)) {
        LOG_WARN("failed to compare", K(ret));
      }
    }
  }
  return ret;
}

} // end namespace storage
} // end namespace oceanbase
