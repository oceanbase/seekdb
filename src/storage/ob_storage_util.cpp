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

#include "ob_storage_util.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/vector/ob_fixed_length_base.h"

namespace oceanbase
{
using namespace common;
using namespace sql;
namespace storage
{
static const ObString OB_DEFAULT_PADDING_STRING(1, 1, &OB_PADDING_CHAR);

OB_INLINE static const ObString get_padding_str(ObCollationType coll_type)
{
  if (!ObCharset::is_cs_nonascii(coll_type)) {
    return OB_DEFAULT_PADDING_STRING;
  } else {
    return ObCharsetUtils::get_const_str(coll_type, OB_PADDING_CHAR);
  }
}

OB_INLINE static void append_padding_pattern(const ObString &space_pattern,
                                             const int32_t offset,
                                             const int32_t buf_len,
                                             char *&buf,
                                             int32_t &true_len)
{
  true_len = offset;
  if (OB_UNLIKELY((buf_len - offset) < space_pattern.length())) {
  } else if (1 == space_pattern.length()) {
    MEMSET(buf + offset, space_pattern[0], buf_len - offset);
    true_len = buf_len;
  } else {
    for (int32_t i = offset; i <= (buf_len - space_pattern.length()); i += space_pattern.length()) {
      MEMCPY(buf + i, space_pattern.ptr(), space_pattern.length());
      true_len += space_pattern.length();
    }
  }
}

OB_INLINE static int pad_on_local_buf(const ObString &space_pattern,
                                      int32_t pad_whitespace_length,
                                      common::ObIAllocator &padding_alloc,
                                      const char *&ptr,
                                      uint32_t &length)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  const int32_t pad_len = length + pad_whitespace_length * space_pattern.length();
  const int64_t buf_len = pad_len;
  if (OB_ISNULL((buf = (char*) padding_alloc.alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    STORAGE_LOG(WARN, "no memory", K(ret));
  } else {
    int32_t true_len = 0;
    MEMCPY(buf, ptr, length);
    append_padding_pattern(space_pattern, length, buf_len, buf, true_len);
    ptr = buf;
    length = true_len;
  }
  return ret;
}

int pad_column(const ObAccuracy accuracy, common::ObIAllocator &padding_alloc, common::ObObj &cell)
{
  int ret = OB_SUCCESS;
  if (cell.is_fixed_len_char_type()) {
    ObLength length = accuracy.get_length(); // byte or char length
    int32_t cell_strlen = 0; // byte or char length
    const ObString space_pattern = get_padding_str(cell.get_collation_type());
    if (OB_FAIL(cell.get_char_length(accuracy, *(reinterpret_cast<int32_t *>(&cell_strlen))))) {
      STORAGE_LOG(WARN, "Fail to get char length, ", K(ret));
    } else {
      if (cell_strlen < length) {
        uint32_t cell_len = cell.get_val_len();
        const char *ptr = cell.get_string_ptr();
        if (OB_FAIL(pad_on_local_buf(space_pattern, (length - cell_strlen), padding_alloc,
                                     ptr, cell_len))) {
          STORAGE_LOG(WARN, "Fail to pad on local buf, ", K(ret), K(cell), K(length), K(cell_strlen));
        } else {
          // watch out !!! in order to deep copy an ObObj instance whose type is char or varchar,
          // set_collation_type() should be revoked. But here no need to set collation type
          cell.set_string(cell.get_type(), ObString(cell_len, cell_len, ptr));
        }
      }
    }
  }
  return ret;
}

int pad_column(const ObObjMeta &obj_meta, const ObAccuracy accuracy, common::ObIAllocator &padding_alloc, blocksstable::ObStorageDatum &datum)
{
  int ret = OB_SUCCESS;
  if (datum.is_null()) {
    // do nothing.
  } else if (obj_meta.is_fixed_len_char_type()) {
    ObLength length = accuracy.get_length(); // byte or char length
    const common::ObCollationType cs_type = obj_meta.get_collation_type();
    const ObString space_pattern = get_padding_str(cs_type);
    int32_t cur_len = 0; // byte or char length
    bool is_ascii = can_do_ascii_optimize(cs_type) && is_ascii_str(datum.ptr_, datum.pack_);
    if (is_ascii) {
      cur_len = datum.pack_;
    } else {
      cur_len = static_cast<int32_t>(ObCharset::strlen_char(cs_type, datum.ptr_, datum.pack_));
    }
    if (cur_len < length &&
        OB_FAIL(pad_on_local_buf(space_pattern, length - cur_len, padding_alloc, datum.ptr_, datum.pack_))) {
      STORAGE_LOG(WARN, "fail to pad on padding allocator", K(ret), K(length), K(cur_len), K(datum));
    }
  }
  return ret;
}

int pad_column(const common::ObAccuracy accuracy, sql::ObEvalCtx &ctx, sql::ObExpr &expr)
{
  int ret = OB_SUCCESS;
  sql::ObDatum &datum = expr.locate_expr_datum(ctx);
  if (datum.is_null()) {
    // do nothing.
  } else if (expr.obj_meta_.is_fixed_len_char_type()) {
    ObLength length = accuracy.get_length(); // byte or char length
    const common::ObCollationType cs_type = expr.datum_meta_.cs_type_;
    const ObString space_pattern = get_padding_str(cs_type);
    int32_t cur_len = 0; // byte or char length
    bool is_ascii = can_do_ascii_optimize(cs_type) && is_ascii_str(datum.ptr_, datum.pack_);
    if (is_ascii) {
      cur_len = datum.pack_;
    } else {
      cur_len = static_cast<int32_t>(ObCharset::strlen_char(cs_type, datum.ptr_, datum.pack_));
    }
    if (cur_len < length) {
      char *ptr = nullptr;
      const int32_t pad_len = datum.pack_ + (length - cur_len) * space_pattern.length();
      const int64_t buf_len = pad_len;
      if (OB_ISNULL(ptr = expr.get_str_res_mem(ctx, buf_len))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(WARN, "no memory", K(ret));
      } else {
        int32_t true_len = 0;
        MEMMOVE(ptr, datum.ptr_, datum.pack_);
        append_padding_pattern(space_pattern, datum.pack_, buf_len, ptr, true_len);
        datum.ptr_ = ptr;
        datum.pack_ = true_len;
      }
    }
  }
  return ret;
}

int pad_on_datums(const common::ObAccuracy accuracy,
                  const common::ObCollationType cs_type,
                  common::ObIAllocator &padding_alloc,
                  int64_t row_count,
                  common::ObDatum *&datums)
{
  int ret = OB_SUCCESS;
  ObLength length = accuracy.get_length(); // byte or char length
  const ObString space_pattern = get_padding_str(cs_type);
  char *buf = nullptr;
  if (1 == length) {
    int32_t buf_len = space_pattern.length();
    if (OB_ISNULL((buf = (char*) padding_alloc.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "no memory", K(ret));
    } else {
      int32_t true_len = 0;
      append_padding_pattern(space_pattern, 0, buf_len, buf, true_len);
      for (int64_t i = 0; i < row_count; i++) {
        common::ObDatum &datum = datums[i];
        if (datum.is_null()) {
          // do nothing
        } else if (0 == datum.pack_){
          datum.ptr_ = buf;
          datum.pack_ = true_len;
        }
      }
    }
  } else if (can_do_ascii_optimize(cs_type)) {
    int32_t buf_len = length * space_pattern.length() * row_count;
    if (OB_ISNULL(buf = (char*) padding_alloc.alloc(buf_len))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "no memory", K(ret));
    } else {
      char *ptr = buf;
      MEMSET(buf, OB_PADDING_CHAR, buf_len);
      for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
        common::ObDatum &datum = datums[i];
        if (datum.is_null()) {
          // do nothing
        } else {
          if (is_ascii_str(datum.ptr_, datum.pack_)) {
            if (datum.pack_ < length) {
              MEMCPY(ptr, datum.ptr_, datum.pack_);
              datum.ptr_ = ptr;
              datum.pack_ = length;
              ptr = ptr + length;
            }
          } else {
            int32_t cur_len = static_cast<int32_t>(ObCharset::strlen_char(cs_type, datum.ptr_, datum.pack_));
            if (cur_len < length &&
                OB_FAIL(pad_on_local_buf(space_pattern, length - cur_len, padding_alloc, datum.ptr_, datum.pack_))) {
              STORAGE_LOG(WARN, "fail to pad on padding allocator", K(ret), K(length), K(cur_len), K(datum));
            }
          }
        }
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < row_count; i++) {
      common::ObDatum &datum = datums[i];
      if (datum.is_null()) {
        // do nothing
      } else {
        int32_t cur_len = static_cast<int32_t>(ObCharset::strlen_char(cs_type, datum.ptr_, datum.pack_));
        if (cur_len < length &&
            OB_FAIL(pad_on_local_buf(space_pattern, length - cur_len, padding_alloc, datum.ptr_, datum.pack_))) {
          STORAGE_LOG(WARN, "fail to pad on padding allocator", K(ret), K(length), K(cur_len), K(datum));
        }
      }
    }
  }
  return ret;
}

int fill_datums_lob_locator(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const share::schema::ObColumnParam &col_param,
    const int64_t row_cap,
    ObDatum *datums,
    bool reuse_lob_locator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!col_param.get_meta_type().is_lob_storage() ||
                  nullptr == context.lob_locator_helper_)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "Unexpected param", K(ret), K(col_param.get_meta_type()), K(context.lob_locator_helper_));
  } else {
    if (reuse_lob_locator) {
      context.lob_locator_helper_->reuse();
    }
    for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_cap; ++row_idx) {
      ObDatum &datum = datums[row_idx];
      if (!datum.is_null() && !datum.get_lob_data().in_row_) {
        if (OB_FAIL(context.lob_locator_helper_->fill_lob_locator_v2(datum, col_param, iter_param, context))) {
          STORAGE_LOG(WARN, "Failed to fill lob loactor", K(ret), K(row_idx), K(datum), K(context), K(iter_param));
        }
      }
    }
  }
  return ret;
}

int check_skip_by_monotonicity(
    sql::ObBlackFilterExecutor &filter,
    blocksstable::ObStorageDatum &min_datum,
    blocksstable::ObStorageDatum &max_datum,
    const sql::ObBitVector &skip_bit,
    const bool has_null,
    ObBitmap *result_bitmap,
    sql::ObBoolMask &bool_mask)
{
  int ret = OB_SUCCESS;
  bool_mask.set_uncertain();
  if (min_datum.is_null() || max_datum.is_null()) {
    // uncertain
  } else {
    const sql::PushdownFilterMonotonicity mono = filter.get_monotonicity();
    bool is_asc = false;
    switch (mono) {
      case sql::PushdownFilterMonotonicity::MON_ASC: {
        is_asc = true;
      }
      case sql::PushdownFilterMonotonicity::MON_DESC: {
        bool filtered = false;
        ObStorageDatum &false_datum = is_asc ? max_datum : min_datum;
        ObStorageDatum &true_datum = is_asc ? min_datum : max_datum;
        if (OB_FAIL(filter.filter(false_datum, skip_bit, filtered))) {
          STORAGE_LOG(WARN, "Failed to compare with false_datum", K(ret), K(false_datum), K(is_asc));
        } else if (filtered) {
          bool_mask.set_always_false();
        } else if (!has_null) {
          if (OB_FAIL(filter.filter(true_datum, skip_bit, filtered))) {
            STORAGE_LOG(WARN, "Failed to compare with true_datum", K(ret), K(true_datum), K(is_asc));
          } else if (!filtered) {
            bool_mask.set_always_true();
          }
        }
        break;
      }
      case sql::PushdownFilterMonotonicity::MON_EQ_ASC: {
        is_asc = true;
      }
      case sql::PushdownFilterMonotonicity::MON_EQ_DESC: {
        bool min_cmp_res = false;
        bool max_cmp_res = false;
        if (OB_FAIL(filter.judge_greater_or_less(min_datum, skip_bit, is_asc, min_cmp_res))) {
          STORAGE_LOG(WARN, "Failed to judge min_datum", K(ret), K(min_datum));
        } else if (min_cmp_res) {
          bool_mask.set_always_false();
        } else if (OB_FAIL(filter.judge_greater_or_less(max_datum, skip_bit, !is_asc, max_cmp_res))) {
          STORAGE_LOG(WARN, "Failed to judge max_datum", K(ret), K(max_datum));
        } else if (max_cmp_res) {
          bool_mask.set_always_false();
        } else if (!has_null) {
          if (OB_FAIL(filter.filter(min_datum, skip_bit, min_cmp_res))) {
            STORAGE_LOG(WARN, "Failed to compare with min_datum", K(ret), K(min_datum));
          } else if (min_cmp_res) {
            // min datum is filtered
          } else if (OB_FAIL(filter.filter(max_datum, skip_bit, max_cmp_res))) {
            STORAGE_LOG(WARN, "Failed to compare with max_datum", K(ret), K(max_datum));
          } else if (!max_cmp_res) {
            // min datum and max datum are both not filtered
            bool_mask.set_always_true();
          }
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "Unexpected monotonicity", K(ret), K(mono));
      }
    }
  }
  if (OB_SUCC(ret) && nullptr != result_bitmap){
    if (bool_mask.is_always_false()) {
      result_bitmap->reuse(false);
    } else if (bool_mask.is_always_true()) {
      result_bitmap->reuse(true);
    }
  }
  return ret;
}

int reverse_trans_version_val(common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == datums || count < 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument", K(ret), KP(datums), K(count));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      common::ObDatum &datum = datums[i];
      if (OB_UNLIKELY(datum.is_nop() || datum.is_null() || datum.get_int() > 0)) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "Unexpected datum value", K(ret), K(datum));
      } else {
        datum.set_int(-datum.get_int());
      }
    }
  }
  return ret;
}

int reverse_trans_version_val(ObIVector *vector, const int64_t count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == vector || count < 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument", K(ret), KP(vector), K(count));
  } else if (OB_UNLIKELY(vector->get_format() != VectorFormat::VEC_FIXED)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "Unexpected vector format for trans version col", K(ret), K(vector->get_format()));
  } else {
    ObFixedLengthBase *fixed_length_base = static_cast<ObFixedLengthBase *>(vector);
    if (OB_UNLIKELY(fixed_length_base->has_null() || fixed_length_base->get_length() != sizeof(int64_t))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "Unexpected vector", K(ret), K(fixed_length_base->has_null()), K(fixed_length_base->get_length()));
    } else {
      int64_t *ver_ptr = reinterpret_cast<int64_t *>(fixed_length_base->get_data());
      for (int64_t i = 0; i < count; ++i) {
        ver_ptr[i] = -ver_ptr[i];
      }
    }
  }
  return ret;
}

int decimal_or_number_to_int64(const ObDatum &datum,
                               const ObDatumMeta &datum_meta,
                               int64_t &res)
{
  int ret = OB_SUCCESS;
  ObObjType ob_type = datum_meta.get_type();
  if (ObNumberType == ob_type) {
    const number::ObNumber nmb(datum.get_number());
    if (OB_FAIL(nmb.extract_valid_int64_with_trunc(res))) {
      STORAGE_LOG(WARN, "failed to cast number to int64", K(ret));
    }
  } else if (ObDecimalIntType == ob_type) {
    int32_t int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(datum_meta.precision_);
    bool is_valid;
    if (OB_FAIL(wide::check_range_valid_int64(datum.get_decimal_int(), int_bytes, is_valid, res))) {
      STORAGE_LOG(WARN, "failed to check decimal int", K(int_bytes), K(ret));
    } else if (!is_valid) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "decimal int is not valid int64", K(ret));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected type", K(ob_type), K(ret));
  }
  return ret;
}

}
}
