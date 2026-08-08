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

#ifndef OCEANBASE_SHARE_VECTOR_OB_I_VECTOR_H_
#define OCEANBASE_SHARE_VECTOR_OB_I_VECTOR_H_

#include "common/datum/ob_datum.h"
#include "query/engine/vector/type_traits.h"
#include "query/engine/vector/static_check_utils.h"
#include "common/object/ob_object.h"
#include "share/vector/ob_bit_vector.h"

namespace oceanbase
{
namespace sql {
  struct ObExpr;
  struct EvalBound;
}

namespace common
{
struct ObDatumAccessContext;
#define DEF_VEC_READ_INTERFACES(Derived)                                                           \
public:                                                                                            \
  OB_INLINE bool is_false(const int64_t idx) const                                                 \
  {                                                                                                \
    return !derived_this().is_null(idx) && 0 == get_int(idx);                                      \
  }                                                                                                \
  OB_INLINE bool is_true(const int64_t idx) const                                                  \
  {                                                                                                \
    return !derived_this().is_null(idx) && 0 != get_int(idx);                                      \
  }                                                                                                \
  OB_INLINE int8_t get_int8(const uint64_t idx) const                                              \
  {                                                                                                \
    return get<int8_t>(idx);                                                                       \
  }                                                                                                \
  OB_INLINE int8_t get_tinyint(const int64_t idx) const                                            \
  {                                                                                                \
    return get<int8_t>(idx);                                                                       \
  }                                                                                                \
  OB_INLINE int16_t get_smallint(const int64_t idx) const                                          \
  {                                                                                                \
    return get<int16_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE int32_t get_mediumint(const int64_t idx) const                                         \
  {                                                                                                \
    return get<int32_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE int32_t get_int32(const int64_t idx) const                                             \
  {                                                                                                \
    return get<int32_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE int64_t get_int(const int64_t idx) const                                               \
  {                                                                                                \
    return get<int64_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE uint8_t get_uint8(const int64_t idx) const                                             \
  {                                                                                                \
    return get<uint8_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE uint8_t get_utinyint(const int64_t idx) const                                          \
  {                                                                                                \
    return get<uint8_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE uint16_t get_usmallint(const int64_t idx) const                                        \
  {                                                                                                \
    return get<uint16_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE uint32_t get_umediumint(const int64_t idx) const                                       \
  {                                                                                                \
    return get<uint32_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE uint32_t get_uint32(const int64_t idx) const                                           \
  {                                                                                                \
    return get<uint32_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE uint64_t get_uint64(const int64_t idx) const                                           \
  {                                                                                                \
    return get<uint64_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE uint64_t get_uint(const int64_t idx) const                                             \
  {                                                                                                \
    return get<uint64_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE float get_float(const int64_t idx) const                                               \
  {                                                                                                \
    return get<float>(idx);                                                                        \
  }                                                                                                \
  OB_INLINE double get_double(const int64_t idx) const                                             \
  {                                                                                                \
    return get<double>(idx);                                                                       \
  }                                                                                                \
  OB_INLINE float get_ufloat(const int64_t idx) const                                              \
  {                                                                                                \
    return get<float>(idx);                                                                        \
  }                                                                                                \
  OB_INLINE double get_udouble(const int64_t idx) const                                            \
  {                                                                                                \
    return get<double>(idx);                                                                       \
  }                                                                                                \
  OB_INLINE int64_t get_ext(const int64_t idx) const                                               \
  {                                                                                                \
    return get<int64_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE int64_t get_unknown(const int64_t idx) const                                           \
  {                                                                                                \
    return get<int64_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE uint64_t get_bit(const int64_t idx) const                                              \
  {                                                                                                \
    return get<uint64_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE bool get_bool(const int64_t idx)                                                       \
  {                                                                                                \
    return 0 != get_int(idx);                                                                      \
  }                                                                                                \
  OB_INLINE uint64_t get_enum(const int64_t idx) const                                             \
  {                                                                                                \
    return get<uint64_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE uint64_t get_set(const int64_t idx) const                                              \
  {                                                                                                \
    return get<uint64_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE uint64_t get_enumset(const int64_t idx) const                                          \
  {                                                                                                \
    return get<uint64_t>(idx);                                                                     \
  }                                                                                                \
  OB_INLINE int64_t get_datetime(const int64_t idx) const                                          \
  {                                                                                                \
    return get<int64_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE int64_t get_mysql_datetime(const int64_t idx) const                            \
  {                                                                                                \
    return get<int64_t>(idx);                                                              \
  }                                                                                                \
  OB_INLINE int64_t get_timestamp(const int64_t idx) const                                         \
  {                                                                                                \
    return get<int64_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE int32_t get_date(const int64_t idx) const                                              \
  {                                                                                                \
    return get<int32_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE int32_t get_mysql_date(const int64_t idx) const                                    \
  {                                                                                                \
    return get<int32_t>(idx);                                                                  \
  }                                                                                                \
  OB_INLINE int64_t get_time(const int64_t idx) const                                              \
  {                                                                                                \
    return get<int32_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE uint8_t get_year(const int64_t idx) const                                              \
  {                                                                                                \
    return get<uint8_t>(idx);                                                                      \
  }                                                                                                \
  OB_INLINE const number::ObCompactNumber &get_number(const int64_t idx) const                     \
  {                                                                                                \
    return *(reinterpret_cast<const number::ObCompactNumber *>(derived_this().get_payload(idx)));  \
  }                                                                                                \
  OB_INLINE const ObOTimestampTinyData &get_otimestamp_tiny(const int64_t idx) const               \
  {                                                                                                \
    return *(reinterpret_cast<const ObOTimestampTinyData *>(derived_this().get_payload(idx)));     \
  }                                                                                                \
  OB_INLINE ObString get_string(const int64_t idx) const                                           \
  {                                                                                                \
    const char *str = NULL;                                                                        \
    ObLength len = 0;                                                                              \
    derived_this().get_payload(idx, str, len);                                                     \
    return ObString(len, str);                                                                     \
  }                                                                                                \
  OB_INLINE int get_enumset_inner(const int64_t idx, ObEnumSetInnerValue &inner_value) const       \
  {                                                                                                \
    int64_t pos = 0;                                                                               \
    const char *payload = NULL;                                                                    \
    ObLength len = 0;                                                                              \
    derived_this().get_payload(idx, payload, len);                                                 \
    return inner_value.deserialize(payload, len, pos);                                             \
  }                                                                                                \
  OB_INLINE const ObLobCommon &get_lob_data(const int64_t idx) const                               \
  {                                                                                                \
    return *(reinterpret_cast<const ObLobCommon *>(derived_this().get_payload(idx)));              \
  }                                                                                                \
  OB_INLINE const ObDecimalInt *get_decimal_int(const int64_t idx) const                           \
  {                                                                                                \
    return reinterpret_cast<const ObDecimalInt *>(derived_this().get_payload(idx));                \
  }                                                                                                \
                                                                                                   \
private:                                                                                           \
  const Derived &derived_this() const                                                              \
  {                                                                                                \
    return *static_cast<const Derived *>(this);                                                    \
  }                                                                                                \
  template <typename T>                                                                            \
  OB_INLINE T get(const int64_t idx) const                                                         \
  {                                                                                                \
    static_assert(sizeof(T) <= sizeof(int64_t), "invalid type");                                   \
    return *reinterpret_cast<const T *>(derived_this().get_payload(idx));                          \
  }

#define DEF_VEC_WRITE_INTERFACES(Derived)                                                          \
public:                                                                                            \
  OB_INLINE void set_int(const int64_t idx, const int64_t v)                                       \
  {                                                                                                \
    set<int64_t>(idx, v);                                                                          \
  };                                                                                               \
  OB_INLINE void set_int32(const int64_t idx, const int32_t v)                                     \
  {                                                                                                \
    set<int32_t>(idx, v);                                                                          \
  }                                                                                                \
  OB_INLINE void set_uint(const int64_t idx, const uint64_t v)                                     \
  {                                                                                                \
    set<uint64_t>(idx, v);                                                                         \
  }                                                                                                \
  OB_INLINE void set_uint32(const int64_t idx, const uint32_t v)                                   \
  {                                                                                                \
    set<uint32_t>(idx, v);                                                                         \
  }                                                                                                \
  OB_INLINE void set_bit(const int64_t idx, const uint64_t v)                                      \
  {                                                                                                \
    set<uint64_t>(idx, v);                                                                         \
  }                                                                                                \
  OB_INLINE void set_bool(const int64_t idx, const bool v)                                         \
  {                                                                                                \
    set_int(idx, static_cast<int64_t>(v));                                                         \
  }                                                                                                \
  OB_INLINE void set_true(const int64_t idx)                                                       \
  {                                                                                                \
    set_int(idx, static_cast<int64_t>(true));                                                      \
  }                                                                                                \
  OB_INLINE void set_false(const int64_t idx)                                                      \
  {                                                                                                \
    set_int(idx, static_cast<int64_t>(false));                                                     \
  }                                                                                                \
  OB_INLINE void set_float(const int64_t idx, const float v)                                       \
  {                                                                                                \
    set<float>(idx, v);                                                                            \
  }                                                                                                \
  OB_INLINE void set_double(const int64_t idx, const double v)                                     \
  {                                                                                                \
    set<double>(idx, v);                                                                           \
  }                                                                                                \
  OB_INLINE void set_enum(const int64_t idx, const uint64_t v)                                     \
  {                                                                                                \
    set<uint64_t>(idx, v);                                                                         \
  }                                                                                                \
  OB_INLINE void set_set(const int64_t idx, const uint64_t v)                                      \
  {                                                                                                \
    set<uint64_t>(idx, v);                                                                         \
  }                                                                                                \
  OB_INLINE void set_datetime(const int64_t idx, const int64_t v)                                  \
  {                                                                                                \
    set<int64_t>(idx, v);                                                                          \
  }                                                                                                \
  OB_INLINE void set_mysql_datetime(const int64_t idx, const ObMySQLDateTime v)                    \
  {                                                                                                \
    set<ObMySQLDateTime>(idx, v);                                                                  \
  }                                                                                                \
  OB_INLINE void set_timestamp(const int64_t idx, const int64_t v)                                 \
  {                                                                                                \
    set<int64_t>(idx, v);                                                                          \
  }                                                                                                \
  OB_INLINE void set_time(const int64_t idx, const int64_t v)                                      \
  {                                                                                                \
    set_int(idx, v);                                                                               \
  }                                                                                                \
  OB_INLINE void set_date(const int64_t idx, const int32_t v)                                      \
  {                                                                                                \
    set<int32_t>(idx, v);                                                                          \
  }                                                                                                \
  OB_INLINE void set_mysql_date(const int64_t idx, const ObMySQLDate v)                            \
  {                                                                                                \
    set<ObMySQLDate>(idx, v);                                                                      \
  }                                                                                                \
  OB_INLINE void set_year(const int64_t idx, const int8_t v)                                       \
  {                                                                                                \
    set<int8_t>(idx, v);                                                                           \
  }                                                                                                \
  OB_INLINE void set_interval_nmonth(const int64_t idx, const int64_t v)                           \
  {                                                                                                \
    set<int64_t>(idx, v);                                                                          \
  }                                                                                                \
  OB_INLINE void set_otimestamp_tiny(const int64_t idx, const ObOTimestampTinyData &v)             \
  {                                                                                                \
    derived_this().set_payload(idx, &v, sizeof(v));                                                \
  }                                                                                                \
  OB_INLINE void set_number(const int64_t idx, const number::ObNumber &num)                        \
  {                                                                                                \
    using CptNumber = number::ObCompactNumber;                                                     \
    CptNumber *cnum = reinterpret_cast<CptNumber *>(no_cv(derived_this().get_payload(idx)));       \
    cnum->desc_ = num.d_;                                                                          \
    const ObLength len = num.d_.len_ * sizeof(*num.get_digits());                                  \
    MEMCPY(&cnum->digits_[0], num.get_digits(), len);                                              \
    derived_this().set_payload_shallow(idx, cnum, len + sizeof(ObNumberDesc));                     \
  }                                                                                                \
  OB_INLINE void set_number(const int64_t idx, const number::ObCompactNumber &cnum)                \
  {                                                                                                \
    ObLength len =                                                                                 \
      static_cast<uint32_t>(sizeof(cnum) + cnum.desc_.len_ * sizeof(cnum.digits_[0]));             \
    derived_this().set_payload(idx, &cnum, len);                                                   \
  }                                                                                                \
  OB_INLINE void set_number_shallow(const int64_t idx, const number::ObCompactNumber &cnum)        \
  {                                                                                                \
    ObLength len =                                                                                 \
      static_cast<uint32_t>(sizeof(cnum) + cnum.desc_.len_ * sizeof(cnum.digits_[0]));             \
    derived_this().set_payload_shallow(idx, &cnum, len);                                           \
  }                                                                                                \
  OB_INLINE void set_string(const int64_t idx, const ObString &v)                                  \
  {                                                                                                \
    derived_this().set_payload_shallow(idx, v.ptr(), v.length());                                  \
  }                                                                                                \
  OB_INLINE void set_string(const int64_t idx, const char *ptr, const uint32_t len)                \
  {                                                                                                \
    derived_this().set_payload_shallow(idx, ptr, len);                                             \
  }                                                                                                \
  OB_INLINE void set_enumset_inner(const int64_t idx, const ObString &v)                           \
  {                                                                                                \
    set_string(idx, v);                                                                            \
  }                                                                                                \
  OB_INLINE void set_enumset_inner(const int64_t idx, const char *ptr, const uint32_t len)         \
  {                                                                                                \
    set_string(idx, ptr, len);                                                                     \
  }                                                                                                \
  OB_INLINE void set_lob_data(const int64_t idx, const ObLobCommon &value, int64_t length)         \
  {                                                                                                \
    derived_this().set_payload(idx, &value, static_cast<uint32_t>(length));                        \
  }                                                                                                \
  OB_INLINE void set_decimal_int(const int64_t idx, const ObDecimalInt *decint, int32_t len)       \
  {                                                                                                \
    derived_this().set_payload(idx, decint, static_cast<uint32_t>(len));                           \
  }                                                                                                \
                                                                                                   \
private:                                                                                           \
  template <typename T>                                                                            \
  OB_INLINE __attribute__((always_inline)) T *no_cv(const T *ptr) const                            \
  {                                                                                                \
    return const_cast<T *>(ptr);                                                                   \
  }                                                                                                \
  Derived &derived_this()                                                                          \
  {                                                                                                \
    return *static_cast<Derived *>(this);                                                          \
  }                                                                                                \
  template <typename T>                                                                            \
  OB_INLINE void set(const int64_t idx, const T value)                                             \
  {                                                                                                \
    static_assert(sizeof(T) <= sizeof(int64_t), "invalid type");                                   \
    static_cast<Derived *>(this)->set_payload(idx, &value, sizeof(T));                             \
  }

/*
 * ObIVector
 *   `-- ObVectorBase
 *       |-- ObBitmapNullVectorBase
 *       |   |-- ObFixedLengthBase -- ObFixedLengthFormat<ValueType>
 *       |   |-- ObDiscreteBase ---- ObDiscreteFormat
 *       |   `-- ObContinuousBase -- ObContinuousFormat
 *       `-- ObUniformBase --------- ObUniformFormat<IS_CONST>
 */
class ObIVector
{
public:
  static const int64_t MAX_VECTOR_STRUCT_SIZE = 64;
  virtual VectorFormat get_format() const = 0;

  virtual void get_payload(const int64_t idx,
                           const char *&payload,
                           ObLength &length) const = 0;
  virtual void get_payload(const int64_t idx,
                           bool &is_null,
                           const char *&payload,
                           ObLength &length) const = 0;
  virtual const char *get_payload(const int64_t idx) const = 0;
  virtual ObLength get_length(const int64_t idx) const = 0;

  virtual void set_length(const int64_t idx, const ObLength length) = 0;

  // deep copy payload
  virtual void set_payload(const int64_t idx,
                           const void *payload,
                           const ObLength length) = 0;
  virtual void set_payload_shallow(const int64_t idx,
                                   const void *payload,
                                   const ObLength length) = 0;

  virtual bool has_null() const = 0;
  virtual void set_has_null() = 0;
  virtual void reset_has_null() = 0;
  virtual bool is_null(const int64_t idx) const = 0;
  virtual void set_null(const int64_t idx) = 0;
  virtual void unset_null(const int64_t idx) = 0;
  void set_null(const sql::EvalBound &bound);

  virtual int64_t to_string(char *buf, const int64_t buf_len) const
  {
    UNUSED(buf);
    UNUSED(buf_len);
    return 0;
  }
  DEF_VEC_READ_INTERFACES(ObIVector);
  DEF_VEC_WRITE_INTERFACES(ObIVector);
};

using IVectorPtrs = common::ObIArray<ObIVector *>;

}
}
#endif // OCEANBASE_SHARE_VECTOR_OB_I_VECTOR_H_
