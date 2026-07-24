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

#ifndef OCEANBASE_OB_DATUM_FUNCS_H_
#define OCEANBASE_OB_DATUM_FUNCS_H_

#include "common/object/ob_obj_compare.h"
#include "common/datum/ob_datum.h"  // ObDatum complete type(do not rely on the preceding include chain)
#include "common/object/ob_obj_type.h"
#include "lib/charset/ob_charset.h"
#include "sql/engine/ob_bit_vector.h"  // conf marker base_bitvec L1
namespace oceanbase { namespace sql {
struct ObExprBasicFuncs;
struct ObSerializeFuncTag;
typedef void (*serializable_function)(ObSerializeFuncTag &);
} }

namespace oceanbase {
namespace common {
class ObExprCtx;
struct ObDatum;

typedef int (*ObDatumCmpFuncType)(const ObDatum &datum1, const ObDatum &datum2, int &cmp_ret);
typedef int (*ObDatumHashFuncType)(const ObDatum &datum, const uint64_t seed, uint64_t &res);

typedef void (*ObBatchDatumHashFunc)(uint64_t *hash_values,
                                     ObDatum *datums,
                                     const bool is_batch_datum,
                                     const sql::ObBitVector &skip,
                                     int64_t size,
                                     const uint64_t *seeds,
                                     const bool is_batch_seed);

class ObDatumFuncs {
public:
  static ObDatumCmpFuncType get_nullsafe_cmp_func(const ObObjType type1,
                                                  const ObObjType type2,
                                                  const ObCmpNullPos null_pos,
                                                  const ObCollationType cs_type,
                                                  const ObScale max_scale,
                                                  const bool has_lob_header,
                                                  const ObPrecision prec1 = PRECISION_UNKNOWN_YET,
                                                  const ObPrecision prec2 = PRECISION_UNKNOWN_YET);

  static bool is_string_type(const ObObjType type);
  static bool is_json(const ObObjType type);
  static bool is_geometry(const ObObjType type);
  static bool is_collection(const ObObjType type);
  static bool is_varying_len_char_type(const ObObjType type, const ObCollationType cs_type) {
    return (type == ObVarcharType && cs_type != CS_TYPE_BINARY);
  }
  static bool is_null_aware_hash_type(const ObObjType type);
  static ObScale max_scale(const ObScale s1, const ObScale s2)
  {
    ObScale max_scale = SCALE_UNKNOWN_YET;
    if (s1 != SCALE_UNKNOWN_YET && s2 != SCALE_UNKNOWN_YET) {
      max_scale = MAX(s1, s2);
    }
    return max_scale;
  }
  static sql::ObExprBasicFuncs* get_basic_func(const ObObjType type,
                                               const ObCollationType cs_type,
                                               const ObScale scale = SCALE_UNKNOWN_YET,
                                               const bool is_lob_locator = true,
                                               const ObPrecision prec = PRECISION_UNKNOWN_YET);
};

struct ObCmpFunc
{
  OB_UNIS_VERSION(1);
public:
  ObCmpFunc() : cmp_func_(NULL) {}
  union {
    common::ObDatumCmpFuncType cmp_func_;
    sql::serializable_function ser_cmp_func_;
  };
  TO_STRING_KV(KP_(cmp_func));
};

struct ObHashFunc
{
  OB_UNIS_VERSION(1);
public:
  ObHashFunc() : hash_func_(NULL), batch_hash_func_(NULL) {}
  union {
    common::ObDatumHashFuncType hash_func_;
    sql::serializable_function ser_hash_func_;
  };
  union {
    ObBatchDatumHashFunc batch_hash_func_;
    sql::serializable_function ser_batch_hash_func_;
  };
  TO_STRING_KV(K_(hash_func), K_(batch_hash_func));
};

typedef common::ObFixedArray<ObCmpFunc, common::ObIAllocator> ObCmpFuncs;
typedef common::ObFixedArray<ObHashFunc, common::ObIAllocator> ObHashFuncs;


} // end namespace common
} // end namespace oceanbase
#endif // OCEANBASE_OB_DATUM_FUNCS_H_
