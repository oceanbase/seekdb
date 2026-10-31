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

#include "common/object/ob_type_metadata.h"
#include "common/datum/ob_datum.h"  // ObDatum complete type(do not rely on the preceding include chain)
#include "common/datum/datum.h"
#include "common/datum/sql_type.h"
#include "common/object/ob_obj_type.h"
#include "lib/charset/ob_charset.h"
#include "share/vector/ob_bit_vector.h"
#include <cstddef>
namespace oceanbase { namespace sql {
struct ObSerializeFuncTag;
typedef void (*serializable_function)(ObSerializeFuncTag &);
} }

namespace oceanbase {
namespace common {
class ObExprCtx;
struct ObDatum;
struct ObLobReadOptions;

// Request-scoped services needed when a datum operation has to dereference
// external payload. Pure in-row operations may use a null context; an
// operation that encounters out-row data must reject a missing LOB context.
struct ObDatumAccessContext
{
  explicit ObDatumAccessContext(const ObLobReadOptions &lob_read_options)
    : lob_read_options_(&lob_read_options)
  {}
  const ObLobReadOptions *lob_read_options_;
};

typedef int (*ObDatumCmpFuncType)(const ObDatum &datum1,
                                 const ObDatum &datum2,
                                 int &cmp_ret,
                                 const ObDatumAccessContext *access_ctx);
typedef int (*ObDatumHashFuncType)(const ObDatum &datum,
                                  const uint64_t seed,
                                  uint64_t &res,
                                  const ObDatumAccessContext *access_ctx);

class SqlTypeMeta;
// Row comparison and batch hashing are layer-neutral datum vocabulary. Query
// keeps its established names through aliases in the ObExpr interface.
typedef int (*NullSafeRowCmpFunc) (const SqlTypeMeta &l_meta, const SqlTypeMeta &r_meta,
                                   const void *l_data, const int32_t l_len, const bool l_null,
                                   const void *r_data, const int32_t r_len, const bool r_null,
                                   int &cmp_ret,
                                   const ObDatumAccessContext *access_ctx);
typedef void (*ObBatchDatumHashFunc)(uint64_t *hash_values,
                                     ObDatum *datums,
                                     const bool is_batch_datum,
                                     const sql::ObBitVector &skip,
                                     int64_t size,
                                     const uint64_t *seeds,
                                     const bool is_batch_seed,
                                     const ObDatumAccessContext *access_ctx);

struct ObDatumBasicFuncs
{
  ObDatumHashFuncType default_hash_;
  ObBatchDatumHashFunc default_hash_batch_;
  ObDatumHashFuncType murmur_hash_;
  ObBatchDatumHashFunc murmur_hash_batch_;
  ObDatumHashFuncType xx_hash_;
  ObBatchDatumHashFunc xx_hash_batch_;
  ObDatumHashFuncType wy_hash_;
  ObBatchDatumHashFunc wy_hash_batch_;
  ObDatumCmpFuncType null_first_cmp_;
  ObDatumCmpFuncType null_last_cmp_;
  ObDatumHashFuncType murmur_hash_v2_;
  ObBatchDatumHashFunc murmur_hash_v2_batch_;
};

// A native operation is prepared for a SQL domain, not just for the bits of a
// value. Full types belong to the plan owner and must outlive these callables.
// They are process-local bindings; serialize/rebuild the specification, never
// their borrowed type pointers or function addresses.
class DatumCmpOp
{
public:
  DatumCmpOp() : left_(nullptr), right_(nullptr), function_(nullptr), null_pos_(NULL_FIRST) {}
  DatumCmpOp(std::nullptr_t) : DatumCmpOp() {}
  bool is_valid() const { return left_ != nullptr && right_ != nullptr && function_ != nullptr; }
  explicit operator bool() const { return is_valid(); }
  bool operator==(std::nullptr_t) const { return !is_valid(); }
  bool operator!=(std::nullptr_t) const { return is_valid(); }
  friend bool operator==(std::nullptr_t, const DatumCmpOp &operation) { return !operation.is_valid(); }
  friend bool operator!=(std::nullptr_t, const DatumCmpOp &operation) { return operation.is_valid(); }
  int operator()(const Datum &left, const Datum &right, int &result,
                 const ObDatumAccessContext *access_ctx = nullptr) const;
  const SqlType *left_type() const { return left_; }
  const SqlType *right_type() const { return right_; }
  // For an explicit legacy/fixed-cell boundary that already owns ObDatum
  // descriptors. This is the exact prepared function, including NULL order.
  ObDatumCmpFuncType legacy_function() const { return function_; }
  TO_STRING_KV(KP_(left), KP_(right), "ready", is_valid());
private:
  friend class ObDatumFuncs;
  const SqlType *left_;
  const SqlType *right_;
  ObDatumCmpFuncType function_;
  ObCmpNullPos null_pos_;
};

class DatumHashOp
{
public:
  DatumHashOp() : type_(nullptr), function_(nullptr), state_function_(nullptr) {}
  DatumHashOp(std::nullptr_t) : DatumHashOp() {}
  bool is_valid() const { return type_ != nullptr && function_ != nullptr; }
  explicit operator bool() const { return is_valid(); }
  bool operator==(std::nullptr_t) const { return !is_valid(); }
  bool operator!=(std::nullptr_t) const { return is_valid(); }
  friend bool operator==(std::nullptr_t, const DatumHashOp &operation) { return !operation.is_valid(); }
  friend bool operator!=(std::nullptr_t, const DatumHashOp &operation) { return operation.is_valid(); }
  int operator()(const Datum &value, uint64_t seed, uint64_t &result,
                 const ObDatumAccessContext *access_ctx = nullptr) const;
  // Completed prefix is retained on failure; skipped/unprocessed slots retain
  // their previous contents. Scalar and seed broadcast follow the old API.
  int operator()(uint64_t *results, const Datum *values, bool batch_values,
                 const sql::ObBitVector &skip, int64_t count,
                 const uint64_t *seeds, bool batch_seeds,
                 const ObDatumAccessContext *access_ctx = nullptr) const;
  const SqlType *type() const { return type_; }
  TO_STRING_KV(KP_(type), "ready", is_valid());
private:
  friend class ObDatumFuncs;
  const SqlType *type_;
  ObDatumHashFuncType function_;
  ObDatumHashFuncType state_function_;
};

struct DatumBasicFuncs
{
  DatumHashOp default_hash_;
  DatumHashOp default_hash_batch_;
  DatumHashOp murmur_hash_;
  DatumHashOp murmur_hash_batch_;
  DatumHashOp xx_hash_;
  DatumHashOp xx_hash_batch_;
  DatumHashOp wy_hash_;
  DatumHashOp wy_hash_batch_;
  DatumCmpOp null_first_cmp_;
  DatumCmpOp null_last_cmp_;
  DatumHashOp murmur_hash_v2_;
  DatumHashOp murmur_hash_v2_batch_;

  // Exact scalar/batch companion selected when these native operations were
  // prepared. The table has static lifetime and is process-local; never send
  // this pointer over the wire. Explicit legacy/fixed-cell boundaries only.
  const ObDatumBasicFuncs *legacy_functions() const { return legacy_functions_; }

private:
  friend class ObDatumFuncs;
  const ObDatumBasicFuncs *legacy_functions_ = nullptr;
};

class ObDatumFuncs {
public:
  // The planner has already established a common comparison/hash domain.
  // These prepare the established algorithms, including Decimal width/scale,
  // rather than inventing a new cross-type SQL coercion or hash convention.
  static int prepare_cmp(const SqlType &left, const SqlType &right,
                         ObCmpNullPos null_pos, ObCollationType collation, DatumCmpOp &output);
  static int prepare_basic(const SqlType &type, DatumBasicFuncs &output);
  // Bind an already selected legacy semantic operation at a typed boundary.
  // Its NULL/collation/rounding convention is preserved, never inferred from
  // the function address. Optional state hashing must use the same algorithm.
  static int bind_legacy_cmp(const SqlType &left, const SqlType &right,
                            ObDatumCmpFuncType function, DatumCmpOp &output);
  static int bind_legacy_hash(const SqlType &type, ObDatumHashFuncType function,
                             DatumHashOp &output, ObDatumHashFuncType special_state_function = nullptr);
  static ObDatumCmpFuncType get_nullsafe_cmp_func(const SqlValueType type1,
                                                  const SqlValueType type2,
                                                  const ObCmpNullPos null_pos,
                                                  const ObCollationType cs_type,
                                                  const ObScale max_scale,
                                                  const bool has_lob_header,
                                                  const ObPrecision prec1 = PRECISION_UNKNOWN_YET,
                                                  const ObPrecision prec2 = PRECISION_UNKNOWN_YET);

  static bool is_string_type(const SqlValueType type);
  static bool is_json(const SqlValueType type);
  static bool is_geometry(const SqlValueType type);
  static bool is_collection(const SqlValueType type);
  static bool is_varying_len_char_type(const SqlValueType type, const ObCollationType cs_type) {
    return (type == ObVarcharType && cs_type != CS_TYPE_BINARY);
  }
  static bool is_null_aware_hash_type(const SqlValueType type);
  static ObScale max_scale(const ObScale s1, const ObScale s2)
  {
    ObScale max_scale = SCALE_UNKNOWN_YET;
    if (s1 != SCALE_UNKNOWN_YET && s2 != SCALE_UNKNOWN_YET) {
      max_scale = MAX(s1, s2);
    }
    return max_scale;
  }
  static ObDatumBasicFuncs* get_basic_func(const SqlValueType type,
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
    NullSafeRowCmpFunc row_cmp_func_;
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
