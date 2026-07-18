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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_BASIC_FUNCS_H_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_BASIC_FUNCS_H_

#include "share/datum/ob_datum_funcs.h"
#include "common/object/ob_obj_compare.h"

namespace oceanbase
{
namespace sql
{

typedef int (*ObExprHashFuncType)(const common::ObDatum &datum, const uint64_t seed, uint64_t &res);

// batch datum hash functions, %seeds, %hash_values may be the same.
using ObBatchDatumHashFunc = common::ObBatchDatumHashFunc;  // moved down to share/datum

typedef int (*ObExprCmpFuncType)(const common::ObDatum &datum1, const common::ObDatum &datum2, int& cmp_ret);
using NullSafeRowCmpFunc = common::NullSafeRowCmpFunc;  // moved down to share/datum
typedef int (*RowCmpFunc) (const ObObjMeta &l_meta, const ObObjMeta &r_meta,
                           const void *l_data, const int32_t l_len,
                           const void *r_data, const int32_t r_len,
                           int &cmp_ret);
struct ObExprBasicFuncs
{
  // Default hash method:
  //    murmur for non string tyeps
  //    mysql string hash for string types
  // Try not to use it unless you need to be compatible with ObObj::hash()/ObObj::varchar_hash(),
  // use murmur_hash_ instead.
  ObExprHashFuncType default_hash_;
  ObBatchDatumHashFunc default_hash_batch_;
  // For murmur/xx/wy functions, the specified hash method is used for all types.
  ObExprHashFuncType murmur_hash_;
  ObBatchDatumHashFunc murmur_hash_batch_;
  ObExprHashFuncType xx_hash_;
  ObBatchDatumHashFunc xx_hash_batch_;
  ObExprHashFuncType wy_hash_;
  ObBatchDatumHashFunc wy_hash_batch_;

  ObExprCmpFuncType null_first_cmp_;
  ObExprCmpFuncType null_last_cmp_;

  /* murmur_hash_v2_ is more efficient than murmur_hash_
     If there is no problem of compatibility, use hash_v2_ is a better choice

     For example, if we calc hash of NUMBER,

      murmur_hash_ calcs like that :
          if (datum.is_null()) {
            const int null_type = ObNullType;
            v = murmurhash64A(&null_type, sizeof(null_type), seed);
          } else {
            uint64_t tmp_v = murmurhash64A(datum.get_number_desc().se_, 1, seed);
            v = murmurhash64A(datum.get_number_digits(), static_cast<uint64_t>(sizeof(uint32_t)* datum.get_number_desc().len_), tmp_v);
          }

      murmur_hash_v2_ calc like that :
          v =  murmurhash64A(datum.ptr_, datum.len_, seed);
  */
  ObExprHashFuncType murmur_hash_v2_;
  ObBatchDatumHashFunc murmur_hash_v2_batch_;

  // null first/last cmp funcs for vector engine 2.0
  NullSafeRowCmpFunc row_null_first_cmp_;
  NullSafeRowCmpFunc row_null_last_cmp_;
  RowCmpFunc row_cmp_;
};

}  // namespace sql
}  // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_BASIC_FUNCS_H_ */
