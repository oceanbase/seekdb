/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */
// PartitionHash: wide-integer partition hash(scalar and vectorized batch hash)。
// used only by src/sql partition expressions、dependency sql::ObBitVector(SQL execution engine),
// moved back from oblib common/wide_integer to src(keeps the common::wide namespace, no caller changes)。
#ifndef OB_WIDE_INTEGER_PARTITION_HASH_H_
#define OB_WIDE_INTEGER_PARTITION_HASH_H_

#include "common/wide_integer/ob_wide_integer_helper.h"
#include "sql/engine/ob_bit_vector.h"

namespace oceanbase
{
namespace common
{
namespace wide
{
template<typename Hash, typename Obj>
struct PartitionHash
{
  using HashMethod = Hash;
  static int calculate(const Obj &val, const uint64_t seed, uint64_t &res)
  {
    int ret = OB_SUCCESS;
    constexpr static uint32_t SIGN_BIT_MASK = (1 << 31);
    const uint32_t *data = reinterpret_cast<const uint32_t *>(val.get_decimal_int());
    int32_t last = val.get_int_bytes() / sizeof(uint32_t) - 1;
    // find minimum length of uint32_t values to represent `val`:
    // if data[last] ==  UINT32_MAX && data[last - 1]'s highest bit is 1, last--
    // else if data[last] == 0 && data[last - 1]'s highest bit is 0, last--
    //
    // this way, val can be easily recovered appending 0/UINT32_MAX values
    if (last <= 0) {
      // do nothing
    } else if (data[last] == UINT32_MAX) {
      while (last > 0 && data[last] == UINT32_MAX && (data[last - 1] & SIGN_BIT_MASK)) {
        last--;
      }
    } else if (data[last] == 0) {
      while (last > 0 && data[last] == 0 && ((data[last - 1] & SIGN_BIT_MASK) == 0)) {
        last--;
      }
    }
    res = HashMethod::hash(data, (last + 1) * sizeof(uint32_t), seed);
    return ret;
  }

  static void hash_batch(uint64_t *hash_values, Obj *vals, const bool is_batch_datum,
                         const sql::ObBitVector &skip, const int64_t size,
                         const uint64_t *seeds, const bool is_batch_seed)
  {
    if (is_batch_datum && !is_batch_seed) {
      do_hash_batch(hash_values, VectorIter<const Obj, true>(vals), skip, size,
                    VectorIter<const uint64_t, false>(seeds));
    } else if (is_batch_datum && is_batch_seed) {
      do_hash_batch(hash_values, VectorIter<const Obj, true>(vals), skip, size,
                    VectorIter<const uint64_t, true>(seeds));
    } else if (!is_batch_datum && is_batch_seed) {
      do_hash_batch(hash_values, VectorIter<const Obj, false>(vals), skip, size,
                    VectorIter<const uint64_t, true>(seeds));
    } else {
      do_hash_batch(hash_values, VectorIter<const Obj, false>(vals), skip, size,
                    VectorIter<const uint64_t, false>(seeds));
    }
  }
private:
  template <typename DATUM_VEC, typename SEED_VEC>
  static void do_hash_batch(uint64_t *hash_values, const DATUM_VEC &datum_vec,
                            const sql::ObBitVector &skip, const int64_t size,
                            const SEED_VEC &seed_vec)
  {
    sql::ObBitVector::flip_foreach(
      skip, size, [&](int64_t idx) __attribute__((always_inline)) {
        int ret = OB_SUCCESS;
        ret = calculate(datum_vec[idx], seed_vec[idx], hash_values[idx]);
        return ret;
      });
  }
  template<typename T, bool is_vec>
  struct VectorIter
  {
    explicit VectorIter(T *vec): vec_(vec) {}
    T &operator[](const int64_t idx) const
    {
      return is_vec ? vec_[idx] : vec_[0];
    }
    T *vec_;
  };
};
} // namespace wide
} // namespace common
} // namespace oceanbase
#endif // OB_WIDE_INTEGER_PARTITION_HASH_H_
