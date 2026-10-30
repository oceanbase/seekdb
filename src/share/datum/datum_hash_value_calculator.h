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

#ifndef OCEANBASE_SHARE_DATUM_DATUM_HASH_VALUE_CALCULATOR_H_
#define OCEANBASE_SHARE_DATUM_DATUM_HASH_VALUE_CALCULATOR_H_

#include "common/datum/ob_datum.h"
#include "common/object/ob_obj_type.h"
#include "lib/hash_func/ob_hash_func.h"
#include <cmath>

namespace oceanbase { namespace common {

// Hash algorithm tags used by Datum hashing. They intentionally live outside
// the Obj implementation so Datum function tables do not depend on it.
struct DatumHashBase
{
  static const bool is_varchar_hash = true;
};
struct DatumDefaultHash : public DatumHashBase
{
  static const bool is_varchar_hash = false;
  static uint64_t hash(const void *data, uint64_t len, uint64_t seed)
  { return murmurhash64A(data, static_cast<int32_t>(len), seed); }
};
struct DatumMurmurHash : public DatumHashBase
{
  static uint64_t hash(const void *data, uint64_t len, uint64_t seed)
  { return murmurhash64A(data, static_cast<int32_t>(len), seed); }
};
struct DatumWyHash : public DatumHashBase
{
  static uint64_t hash(const void *data, uint64_t len, uint64_t seed)
  { return wyhash(data, len, seed); }
};
struct DatumXxHash : public DatumHashBase
{
  static uint64_t hash(const void *data, uint64_t len, uint64_t seed)
  { return XXH64(data, static_cast<size_t>(len), seed); }
};

// Hashes the logical value carried by a datum using the legacy SQL hash
// representation through Datum-native value access.
template <SqlValueType type, typename Hash>
struct DatumHashValueCalculator
{
  static int calc_hash_value(const ObDatum &, const uint64_t, uint64_t &result)
  {
    result = 0;
    return OB_SUCCESS;
  }
};

template <typename Hash>
struct DatumHashValueCalculator<ObNullType, Hash>
{
  static int calc_hash_value(const ObDatum &, const uint64_t seed, uint64_t &result)
  {
    const int type_value = ObNullType;
    result = Hash::hash(&type_value, sizeof(type_value), seed);
    return OB_SUCCESS;
  }
};

#define DEF_DATUM_SCALAR_HASH(TYPE, GETTER, HASH_TYPE)                              \
  template <typename Hash>                                                         \
  struct DatumHashValueCalculator<TYPE, Hash>                                      \
  {                                                                                \
    static int calc_hash_value(const ObDatum &datum, const uint64_t seed,           \
                               uint64_t &result)                                    \
    {                                                                              \
      HASH_TYPE value = static_cast<HASH_TYPE>(datum.GETTER());                     \
      result = Hash::hash(&value, sizeof(value), seed);                             \
      return OB_SUCCESS;                                                           \
    }                                                                              \
  };

DEF_DATUM_SCALAR_HASH(ObTinyIntType, get_tinyint, int64_t);
DEF_DATUM_SCALAR_HASH(ObSmallIntType, get_smallint, int64_t);
DEF_DATUM_SCALAR_HASH(ObMediumIntType, get_mediumint, int64_t);
DEF_DATUM_SCALAR_HASH(ObInt32Type, get_int32, int64_t);
DEF_DATUM_SCALAR_HASH(ObIntType, get_int, int64_t);
DEF_DATUM_SCALAR_HASH(ObUTinyIntType, get_utinyint, uint64_t);
DEF_DATUM_SCALAR_HASH(ObUSmallIntType, get_usmallint, uint64_t);
DEF_DATUM_SCALAR_HASH(ObUMediumIntType, get_umediumint, uint64_t);
DEF_DATUM_SCALAR_HASH(ObUInt32Type, get_uint32, uint64_t);
DEF_DATUM_SCALAR_HASH(ObUInt64Type, get_uint64, uint64_t);
DEF_DATUM_SCALAR_HASH(ObBitType, get_bit, uint64_t);
DEF_DATUM_SCALAR_HASH(ObEnumType, get_enum, uint64_t);
DEF_DATUM_SCALAR_HASH(ObSetType, get_set, uint64_t);
DEF_DATUM_SCALAR_HASH(ObDateTimeType, get_datetime, int64_t);
DEF_DATUM_SCALAR_HASH(ObTimestampType, get_timestamp, int64_t);
DEF_DATUM_SCALAR_HASH(ObDateType, get_date, int64_t);
DEF_DATUM_SCALAR_HASH(ObTimeType, get_time, int64_t);
DEF_DATUM_SCALAR_HASH(ObYearType, get_year, int64_t);
DEF_DATUM_SCALAR_HASH(ObExtendType, get_ext, int64_t);
DEF_DATUM_SCALAR_HASH(ObMySQLDateType, get_date, int64_t);
DEF_DATUM_SCALAR_HASH(ObMySQLDateTimeType, get_datetime, int64_t);

#define DEF_DATUM_REAL_HASH(TYPE, GETTER)                                           \
  template <typename Hash>                                                         \
  struct DatumHashValueCalculator<TYPE, Hash>                                      \
  {                                                                                \
    static int calc_hash_value(const ObDatum &datum, const uint64_t seed,           \
                               uint64_t &result)                                    \
    {                                                                              \
      double value = static_cast<double>(datum.GETTER());                           \
      if (0.0 == value) {                                                          \
        value = 0.0;                                                               \
      } else if (std::isnan(value)) {                                               \
        value = NAN;                                                               \
      }                                                                            \
      result = Hash::hash(&value, sizeof(value), seed);                              \
      return OB_SUCCESS;                                                           \
    }                                                                              \
  };

DEF_DATUM_REAL_HASH(ObFloatType, get_float);
DEF_DATUM_REAL_HASH(ObUFloatType, get_ufloat);
DEF_DATUM_REAL_HASH(ObDoubleType, get_double);
DEF_DATUM_REAL_HASH(ObUDoubleType, get_udouble);
#undef DEF_DATUM_REAL_HASH

template <typename Hash>
struct DatumHashValueCalculator<ObNumberType, Hash>
{
  static int calc_hash_value(const ObDatum &datum, const uint64_t seed, uint64_t &result)
  {
    uint64_t hash = Hash::hash(&datum.get_number_desc().se_, 1, seed);
    result = Hash::hash(datum.get_number_digits(),
                        sizeof(uint32_t) * datum.get_number_desc().len_, hash);
    return OB_SUCCESS;
  }
};
template <typename Hash>
struct DatumHashValueCalculator<ObUNumberType, Hash>
  : DatumHashValueCalculator<ObNumberType, Hash> {};

template <typename Hash>
struct DatumHashValueCalculator<ObUnknownType, Hash>
{
  static int calc_hash_value(const ObDatum &datum, const uint64_t seed, uint64_t &result)
  {
    const int64_t value = datum.get_unknown();
    result = Hash::hash(&value, sizeof(value), seed);
    return OB_SUCCESS;
  }
};
template <typename Hash>
struct DatumHashValueCalculator<ObUserDefinedSQLType, Hash>
{
  static int calc_hash_value(const ObDatum &, const uint64_t, uint64_t &result)
  {
    result = 0;
    return OB_NOT_SUPPORTED;
  }
};
template <typename Hash>
struct DatumHashValueCalculator<ObCollectionSQLType, Hash>
  : DatumHashValueCalculator<ObUserDefinedSQLType, Hash> {};

template <typename Hash>
struct DatumHashValueCalculator<ObDecimalIntType, Hash>
{
  static int calc_hash_value(const ObDatum &datum, const uint64_t seed, uint64_t &result)
  {
    result = Hash::hash(datum.get_decimal_int(), datum.get_int_bytes(), seed);
    return OB_SUCCESS;
  }
};

#undef DEF_DATUM_SCALAR_HASH

} } // namespace oceanbase::common

#endif // OCEANBASE_SHARE_DATUM_DATUM_HASH_VALUE_CALCULATOR_H_
