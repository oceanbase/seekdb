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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_UTIL_
#define OCEANBASE_STORAGE_OB_STORAGE_UTIL_

#include "data_plane/encoding/ob_ascii_util.h"
#include "lib/allocator/ob_allocator.h"
#include "share/datum/ob_datum_funcs.h"
#include "share/datum/ob_datum_compare.h"
#include "query/engine/expr/ob_expr.h"
#include "common/ob_common_types.h"
#include "storage/ob_obj_buf_array.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObColumnParam;
}
}
namespace common
{
class ObBitmap;
class ObIVector;
struct ObVersionRange;
}
namespace sql
{
struct ObBoolMask;
class ObBlackFilterExecutor;
class ObDynamicFilterExecutor;
}
namespace blocksstable
{
struct ObStorageDatum;
}
namespace storage
{
struct ObTableIterParam;
struct ObTableAccessContext;

int pad_column(const ObObjMeta &obj_meta,
               const ObAccuracy accuracy,
               common::ObIAllocator &padding_alloc,
               blocksstable::ObStorageDatum &datum);

int pad_column(const ObAccuracy accuracy,
               common::ObIAllocator &padding_alloc,
               common::ObObj &cell);

int pad_column(const common::ObAccuracy accuracy,
               sql::ObEvalCtx &ctx,
               sql::ObExpr &expr);

int pad_on_datums(const common::ObAccuracy accuracy,
                  const common::ObCollationType cs_type,
                  common::ObIAllocator &padding_alloc,
                  int64_t row_count,
                  common::ObDatum *&datums);

int fill_datums_lob_locator(const ObTableIterParam &iter_param,
                            const ObTableAccessContext &context,
                            const share::schema::ObColumnParam &col_param,
                            const int64_t row_cap,
                            ObDatum *datums,
                            bool reuse_lob_locator = true);

int check_skip_by_monotonicity(sql::ObBlackFilterExecutor &filter,
                               blocksstable::ObStorageDatum &min_datum,
                               blocksstable::ObStorageDatum &max_datum,
                               const sql::ObBitVector &skip_bit,
                               const bool has_null,
                               ObBitmap *result_bitmap,
                               sql::ObBoolMask &bool_mask);

inline static common::ObDatumHashFuncType get_datum_hash_func(
    const common::ObObjMeta &obj_meta)
{
  common::ObDatumHashFuncType hash_func = nullptr;
  common::ObPrecision precision = common::PRECISION_UNKNOWN_YET;
  if (obj_meta.is_decimal_int()) {
    precision = obj_meta.get_stored_precision();
  }
  common::ObDatumBasicFuncs *basic_funcs = common::ObDatumFuncs::get_basic_func(
      obj_meta.get_type(),
      obj_meta.get_collation_type(),
      obj_meta.get_scale(),
      false,
      precision);
  if (nullptr != basic_funcs) {
    hash_func = basic_funcs->murmur_hash_v2_;
  }
  return hash_func;
}

enum class ObFilterInCmpType {
  MERGE_SEARCH,
  BINARY_SEARCH_DICT,
  BINARY_SEARCH,
  HASH_SEARCH,
};

inline ObFilterInCmpType get_filter_in_cmp_type(
  const int64_t row_count, 
  const int64_t param_count,
  const bool is_sorted_dict)
{
  // BINARY_HASH_THRESHOLD: means the threshold to choose BINARY_SEARCH or HASH_SEARCH
  // When the dictionary is unordered, the only variable available for iteration is param_count.
  // Testing has shown that when the data size is small, the overhead of binary search is 
  // lower than the overhead of computing hashes.
  // Therefore, this threshold is temporarily set to a small value(8).
  static constexpr int64_t BINARY_HASH_THRESHOLD = 8;

  // HASH_BUCKETS: means the number of buckets(slots) in hashset.
  // This value is related to the performance of the hashset.
  const int64_t HASH_BUCKETS = hash::cal_next_prime(param_count * 2);

  ObFilterInCmpType cmp_type = ObFilterInCmpType::HASH_SEARCH;
  if (is_sorted_dict) {
    if (row_count > 3 * param_count) {
      // row_count >> param_count
      if (row_count > HASH_BUCKETS * 4) {
        cmp_type = ObFilterInCmpType::BINARY_SEARCH_DICT;
      } else {
        cmp_type = ObFilterInCmpType::MERGE_SEARCH;
      }
    } else if (row_count * 3 >= param_count) {
      // row_count ~~ param_count
      if (row_count > HASH_BUCKETS) {
        cmp_type = ObFilterInCmpType::MERGE_SEARCH;
      } else {
        cmp_type = ObFilterInCmpType::HASH_SEARCH;
      }
    } else {
      // row_count << param_count
      cmp_type = ObFilterInCmpType::HASH_SEARCH;
    }
  } else {
    // Unordered dict
    if (param_count <= BINARY_HASH_THRESHOLD) {
      cmp_type = ObFilterInCmpType::BINARY_SEARCH;
    } else {
      cmp_type = ObFilterInCmpType::HASH_SEARCH;
    }
  }
  return cmp_type;
}

inline int reverse_trans_version_val(common::ObDatum &datum)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(datum.is_nop() || datum.is_null() || datum.get_int() > 0)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected datum value", K(ret), K(datum));
  } else {
    datum.set_int(-datum.get_int());
  }
  return ret;
}
int reverse_trans_version_val(common::ObDatum *datums, const int64_t count);
int reverse_trans_version_val(common::ObIVector *vector, const int64_t count);

}
}

#endif // OCEANBASE_STORAGE_OB_STORAGE_UTIL_
