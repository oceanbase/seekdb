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

#include "common/datum/datum_parameter_codec.h"
#include <type_traits>
#include "lib/container/ob_se_array.h"

#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "The native wire codec must not depend on the legacy value definition"
#endif

namespace oceanbase
{
namespace common
{
namespace
{
constexpr int MAX_ARRAY_DEPTH = 64;
// A deferred array can fail after allocating several nested values. Track
// payload pointers separately so rollback reaches even unpublished leaves;
// successful payloads remain in the caller's allocator, never in this object.
class DeferredArrayDecodeAllocator : public ObIAllocator
{
public:
  explicit DeferredArrayDecodeAllocator(ObIAllocator &owner) : owner_(owner), committed_(false) {}
  ~DeferredArrayDecodeAllocator()
  {
    if (!committed_) {
      for (int64_t i = 0; i < allocations_.count(); ++i) { owner_.free(allocations_.at(i)); }
    }
  }
  void *alloc(int64_t size) override
  {
    void *memory = owner_.alloc(size);
    if (memory != nullptr && allocations_.push_back(memory) != OB_SUCCESS) {
      owner_.free(memory);
      memory = nullptr;
    }
    return memory;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *memory) override
  {
    for (int64_t i = 0; memory != nullptr && i < allocations_.count(); ++i) {
      if (allocations_.at(i) == memory) {
        allocations_.remove(i);
        owner_.free(memory);
        break;
      }
    }
  }
  void commit() { committed_ = true; }
private:
  ObIAllocator &owner_;
  bool committed_;
  ObSEArray<void *, 16> allocations_;
};

uint32_t datum_length_limit()
{
  ObDatumDesc descriptor;
  descriptor.pack_ = UINT32_MAX;
  return descriptor.len_;
}


struct SizeArchive
{
  static constexpr bool reading = false;
  int ret_ = OB_SUCCESS;
  int64_t pos_ = 0;
  void add(int64_t length)
  {
    if (ret_ == OB_SUCCESS) {
      int64_t sum = 0;
      if (length < 0 || __builtin_add_overflow(pos_, length, &sum)) { ret_ = OB_SIZE_OVERFLOW; }
      else { pos_ = sum; }
    }
  }
  template <typename T> void field(const T &value)
  { if (ret_ == OB_SUCCESS) { add(serialization::encoded_length(value)); } }
  void raw(const void *bytes, int64_t length)
  {
    if (length > 0 && bytes == nullptr) { ret_ = OB_INVALID_ARGUMENT; }
    add(length);
  }
  void number(const number::ObNumber::Desc &desc, const uint32_t *)
  { add(serialization::encode_length_number_type(desc)); }
  void require(int64_t) {}
};

struct WriteArchive
{
  static constexpr bool reading = false;
  char *buf_;
  int64_t length_;
  int64_t pos_;
  int ret_ = OB_SUCCESS;
  WriteArchive(char *buf, int64_t length, int64_t pos) : buf_(buf), length_(length), pos_(pos) {}
  void require(int64_t length)
  {
    if (ret_ == OB_SUCCESS && (length < 0 || length > length_ - pos_)) {
      ret_ = OB_BUF_NOT_ENOUGH;
    }
  }
  template <typename T> void field(const T &value)
  { if (ret_ == OB_SUCCESS) { ret_ = serialization::encode(buf_, length_, pos_, value); } }
  void raw(const void *bytes, int64_t length)
  {
    require(length);
    if (ret_ == OB_SUCCESS && length > 0) {
      if (bytes == nullptr) { ret_ = OB_INVALID_ARGUMENT; }
      else { MEMCPY(buf_ + pos_, bytes, length); pos_ += length; }
    }
  }
  void number(const number::ObNumber::Desc &desc, const uint32_t *digits)
  {
    if (ret_ == OB_SUCCESS) {
      ret_ = serialization::encode_number_type(buf_, length_, pos_, desc, digits);
    }
  }
};

struct ReadArchive
{
  static constexpr bool reading = true;
  const char *buf_;
  int64_t length_;
  int64_t pos_;
  ObIAllocator &allocator_;
  int ret_ = OB_SUCCESS;
  bool allow_omitted_fields_;
  bool allow_missing_string_terminator_;
  ReadArchive(const char *buf, int64_t length, int64_t pos, ObIAllocator &allocator,
              bool allow_omitted_fields = false, bool allow_missing_string_terminator = false)
      : buf_(buf), length_(length), pos_(pos), allocator_(allocator),
        allow_omitted_fields_(allow_omitted_fields),
        allow_missing_string_terminator_(allow_missing_string_terminator) {}
  void require(int64_t length)
  {
    if (ret_ == OB_SUCCESS && (length < 0 || length > length_ - pos_)) {
      ret_ = OB_DESERIALIZE_ERROR;
    }
  }
  void field(ObString &value)
  {
    if (allow_omitted_fields_ && pos_ >= length_) { return; }
    if (allow_omitted_fields_ && ret_ == OB_SUCCESS) {
      // Reuse the existing bounded string parser's error/omission contract.
      // Its historical decoder may advance past a missing terminator; schema
      // decoding rejected that later, so reject it here before copying bytes.
      ret_ = serialization::decode(buf_, length_, pos_, value);
      if (ret_ == OB_SUCCESS && pos_ > length_ && !allow_missing_string_terminator_) { ret_ = OB_INVALID_ARGUMENT; }
      if (ret_ == OB_SUCCESS && (value.length() < 0 || value.length() > datum_length_limit())) {
        ret_ = OB_DESERIALIZE_ERROR;
      }
      return;
    }
    if (ret_ == OB_SUCCESS) {
      int64_t length = 0;
      ret_ = serialization::decode_vi64(buf_, length_, pos_, &length);
      // Check the native length limit before narrowing to ObString and require
      // the legacy terminator byte as well as the payload, without addition overflow.
      if (ret_ == OB_SUCCESS &&
          (length < 0 || length > datum_length_limit() || length >= length_ - pos_)) {
        ret_ = OB_DESERIALIZE_ERROR;
      }
      if (ret_ == OB_SUCCESS) { value.assign_ptr(buf_ + pos_, length); pos_ += length + 1; }
    }
  }
  template <typename T> void field(T &value)
  {
    if (allow_omitted_fields_ && pos_ >= length_) { return; }
    require(1);
    if (ret_ == OB_SUCCESS) {
      const int64_t before = pos_;
      ret_ = serialization::decode(buf_, length_, pos_, value);
      if (ret_ == OB_SUCCESS && (pos_ <= before || pos_ > length_)) { ret_ = OB_DESERIALIZE_ERROR; }
    }
  }
  void allocate(ObDatum &value, int64_t length)
  {
    if (ret_ == OB_SUCCESS) {
      if (length < 0 || length > datum_length_limit()) { ret_ = OB_DESERIALIZE_ERROR; }
      else if ((value.ptr_ = static_cast<char *>(allocator_.alloc(std::max<int64_t>(length, 1)))) == nullptr) {
        ret_ = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
  }
  void copy(ObDatum &value, const char *bytes, int64_t length)
  {
    allocate(value, length);
    if (ret_ == OB_SUCCESS) {
      if (length > 0) { MEMCPY(const_cast<char *>(value.ptr_), bytes, length); }
      value.set_string(value.ptr_, length);
    }
  }
  void number(number::ObNumber::Desc &desc, const uint32_t *&digits)
  {
    uint32_t *borrowed = nullptr;
    if (ret_ == OB_SUCCESS) {
      ret_ = serialization::decode_number_type(buf_, length_, pos_, desc, borrowed);
      desc.reserved_ = 0;
      digits = borrowed;
    }
  }
};

template <typename Archive>
using ParameterView = std::conditional_t<Archive::reading, DatumParameter, const DatumParameter>;
template <typename Archive> void parameter(ParameterView<Archive> &, Archive &, int);
void read_schema_native(Datum &, SqlTypeMeta &, ReadArchive &, int depth);

template <typename Archive>
void array_body(const DatumParameterArray &array, Archive &archive, int depth)
{
  if (depth > MAX_ARRAY_DEPTH) { archive.ret_ = OB_SIZE_OVERFLOW; }
  if (array.count_ < 0 || (array.count_ > 0 && array.data_ == nullptr)) { archive.ret_ = OB_INVALID_ARGUMENT; }
  archive.field(array.element_);
  archive.field(array.count_);
  for (int64_t i = 0; archive.ret_ == OB_SUCCESS && i < array.count_; ++i) {
    parameter(array.data_[i], archive, depth + 1);
  }
}

void read_array_body(ReadArchive &archive, DatumParameterArray *&output, int depth)
{
  if (depth > MAX_ARRAY_DEPTH) { archive.ret_ = OB_SIZE_OVERFLOW; }
  ObDataType element;
  int64_t count = 0;
  archive.field(element); archive.field(count);
  if (archive.ret_ == OB_SUCCESS && (count < 0 || count > (archive.length_ - archive.pos_) / 6)) {
    archive.ret_ = OB_DESERIALIZE_ERROR;
  }
  DatumParameterArray *array = nullptr;
  if (archive.ret_ == OB_SUCCESS && (array = DatumParameterArray::alloc(archive.allocator_, count)) == nullptr) {
    archive.ret_ = OB_ALLOCATE_MEMORY_FAILED;
  }
  if (archive.ret_ == OB_SUCCESS) { array->element_ = element; }
  for (int64_t i = 0; archive.ret_ == OB_SUCCESS && i < count; ++i) {
    parameter(array->data_[i], archive, depth + 1);
  }
  if (archive.ret_ == OB_SUCCESS) { output = array; }
}

template <typename Archive, typename Wire, auto Getter, auto Setter>
void scalar(ObDatum &datum, Archive &archive)
{
  Wire value{};
  using Native = decltype((datum.*Getter)());
  if constexpr (!Archive::reading) {
    if (datum.ptr_ == nullptr || datum.len_ != sizeof(Native)) { archive.ret_ = OB_INVALID_ARGUMENT; }
    else { value = static_cast<Wire>((datum.*Getter)()); }
  }
  archive.field(value);
  if constexpr (Archive::reading) {
    archive.allocate(datum, sizeof(Native));
    if (archive.ret_ == OB_SUCCESS) { (datum.*Setter)(value); }
  }
}

template <typename Archive>
void value(ObDatum &datum, SqlTypeMeta &meta, Archive &archive, int depth,
           bool include_metadata = true)
{
  if (archive.ret_ != OB_SUCCESS) { return; }
  if (depth > MAX_ARRAY_DEPTH) { archive.ret_ = OB_SIZE_OVERFLOW; return; }
  if constexpr (!Archive::reading) {
    if (meta.is_invalid()) { archive.ret_ = OB_ERR_UNEXPECTED; return; }
    if (datum.is_null() && !meta.is_null()) { meta.set_null(); }
  }
  // Protocol readers stay strict. The schema reader explicitly retains the
  // old OB_UNIS_DECODE omission rule for shorter version-1 bodies.
  if (include_metadata) {
    if constexpr (Archive::reading) {
      if (!archive.allow_omitted_fields_) { archive.require(4); }
    } else {
      archive.require(4);
    }
    archive.field(meta);
  }
  if (archive.ret_ != OB_SUCCESS) { return; }
  if (meta.is_invalid()) { archive.ret_ = OB_ERR_UNEXPECTED; return; }
  switch (meta.get_type()) {
    case ObNullType: datum.set_null(); break;
    case ObTinyIntType: scalar<Archive, int8_t, &ObDatum::get_int, &ObDatum::set_int>(datum, archive); break;
    case ObSmallIntType: scalar<Archive, int16_t, &ObDatum::get_int, &ObDatum::set_int>(datum, archive); break;
    case ObMediumIntType: scalar<Archive, int32_t, &ObDatum::get_int, &ObDatum::set_int>(datum, archive); break;
    case ObInt32Type: scalar<Archive, int32_t, &ObDatum::get_int, &ObDatum::set_int>(datum, archive); break;
    case ObIntType: scalar<Archive, int64_t, &ObDatum::get_int, &ObDatum::set_int>(datum, archive); break;
    case ObUTinyIntType: scalar<Archive, uint8_t, &ObDatum::get_uint, &ObDatum::set_uint>(datum, archive); break;
    case ObUSmallIntType: scalar<Archive, uint16_t, &ObDatum::get_uint, &ObDatum::set_uint>(datum, archive); break;
    case ObUMediumIntType: scalar<Archive, uint32_t, &ObDatum::get_uint, &ObDatum::set_uint>(datum, archive); break;
    case ObUInt32Type: scalar<Archive, uint32_t, &ObDatum::get_uint, &ObDatum::set_uint>(datum, archive); break;
    case ObUInt64Type: scalar<Archive, uint64_t, &ObDatum::get_uint, &ObDatum::set_uint>(datum, archive); break;
    case ObFloatType: scalar<Archive, float, &ObDatum::get_float, &ObDatum::set_float>(datum, archive); break;
    case ObUFloatType: scalar<Archive, float, &ObDatum::get_float, &ObDatum::set_float>(datum, archive); break;
    case ObDoubleType: scalar<Archive, double, &ObDatum::get_double, &ObDatum::set_double>(datum, archive); break;
    case ObUDoubleType: scalar<Archive, double, &ObDatum::get_double, &ObDatum::set_double>(datum, archive); break;
    case ObDateTimeType: scalar<Archive, int64_t, &ObDatum::get_datetime, &ObDatum::set_datetime>(datum, archive); break;
    case ObTimestampType: scalar<Archive, int64_t, &ObDatum::get_timestamp, &ObDatum::set_timestamp>(datum, archive); break;
    case ObDateType: scalar<Archive, int32_t, &ObDatum::get_date, &ObDatum::set_date>(datum, archive); break;
    case ObTimeType: scalar<Archive, int64_t, &ObDatum::get_time, &ObDatum::set_time>(datum, archive); break;
    case ObYearType: scalar<Archive, uint8_t, &ObDatum::get_year, &ObDatum::set_year>(datum, archive); break;
    case ObBitType: scalar<Archive, uint64_t, &ObDatum::get_bit, &ObDatum::set_bit>(datum, archive); break;
    case ObEnumType: scalar<Archive, uint64_t, &ObDatum::get_enum, &ObDatum::set_enum>(datum, archive); break;
    case ObSetType: scalar<Archive, uint64_t, &ObDatum::get_set, &ObDatum::set_set>(datum, archive); break;
    case ObUnknownType: scalar<Archive, int64_t, &ObDatum::get_unknown, &ObDatum::set_int>(datum, archive); break;
    case ObMySQLDateType: scalar<Archive, int32_t, &ObDatum::get_date, &ObDatum::set_date>(datum, archive); break;
    case ObMySQLDateTimeType: scalar<Archive, int64_t, &ObDatum::get_datetime, &ObDatum::set_datetime>(datum, archive); break;
    case ObNumberType:
    case ObUNumberType: {
      number::ObNumber::Desc desc{};
      const uint32_t *digits = nullptr;
      if constexpr (!Archive::reading) {
        if (datum.ptr_ == nullptr || datum.len_ < sizeof(desc)) { archive.ret_ = OB_INVALID_ARGUMENT; }
        else {
          desc = datum.get_number_desc();
          if (datum.len_ != sizeof(desc) + sizeof(uint32_t) * desc.len_) { archive.ret_ = OB_INVALID_ARGUMENT; }
          else { digits = datum.get_number_digits(); }
          desc.reserved_ = 0;
          desc.flag_ = 0;
        }
      }
      if (archive.ret_ == OB_SUCCESS) { archive.number(desc, digits); }
      if constexpr (Archive::reading) {
        const int64_t length = sizeof(desc) + sizeof(uint32_t) * desc.len_;
        archive.allocate(datum, length);
        if (archive.ret_ == OB_SUCCESS) {
          char *bytes = const_cast<char *>(datum.ptr_);
          MEMCPY(bytes, &desc, sizeof(desc));
          if (desc.len_ > 0) { MEMCPY(bytes + sizeof(desc), digits, sizeof(uint32_t) * desc.len_); }
          datum.set_string(bytes, length);
        }
      }
      break;
    }
    case ObVarcharType:
    case ObCharType:
    case ObHexStringType:
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType:
    case ObEnumInnerType:
    case ObSetInnerType:
    case ObJsonType:
    case ObGeometryType:
    case ObUserDefinedSQLType:
    case ObCollectionSQLType: {
      if (meta.get_type() == ObUserDefinedSQLType || meta.is_collection_sql_type()) {
        uint16_t subschema_id = meta.get_subschema_id();
        uint8_t flags = meta.get_udt_flags();
        archive.field(subschema_id);
        archive.field(flags);
        if constexpr (Archive::reading) { meta.set_subschema_id(subschema_id); meta.set_udt_flags(flags); }
      }
      ObString bytes;
      if constexpr (!Archive::reading) {
        if (datum.len_ > INT32_MAX || (datum.len_ > 0 && datum.ptr_ == nullptr)) { archive.ret_ = OB_INVALID_ARGUMENT; }
        else { bytes = datum.get_string(); }
      }
      archive.field(bytes);
      if constexpr (Archive::reading) {
        archive.copy(datum, bytes.ptr(), bytes.length());
        if (meta.is_outrow()) { datum.set_outrow(); }
        else if (meta.has_lob_header()) { datum.set_has_lob_header(); }
      }
      break;
    }
    case ObDecimalIntType: {
      int32_t length = 0;
      ObScale scale = meta.get_scale();
      if constexpr (!Archive::reading) {
        if (datum.len_ > sizeof(int512_t)) { archive.ret_ = OB_INVALID_ARGUMENT; }
        else { length = datum.len_; }
      }
      archive.field(length); archive.field(scale);
      if constexpr (Archive::reading) {
        if (length < 0 || length > sizeof(int512_t)) { archive.ret_ = OB_DESERIALIZE_ERROR; }
        archive.require(length);
        if (archive.ret_ == OB_SUCCESS) {
          archive.copy(datum, archive.buf_ + archive.pos_, length);
          archive.pos_ += length;
          meta.set_decimal_int(scale);
        }
      } else { archive.raw(datum.ptr_, length); }
      break;
    }
    case ObExtendType: {
      if (meta.is_ext_sql_array()) {
        if constexpr (Archive::reading) {
          int64_t zero = 0, length = 0;
          archive.field(zero); archive.field(length);
          if (archive.ret_ == OB_SUCCESS && zero != 0) { archive.ret_ = OB_NOT_SUPPORTED; }
          archive.require(length);
          if (archive.ret_ == OB_SUCCESS) {
            const int64_t end = archive.pos_ + length;
            const int64_t parent_end = archive.length_;
            archive.length_ = end;
            DatumParameterArray *array = nullptr;
            read_array_body(archive, array, depth);
            if (archive.ret_ == OB_SUCCESS && archive.pos_ != end) { archive.ret_ = OB_DESERIALIZE_ERROR; }
            archive.length_ = parent_end;
            archive.allocate(datum, sizeof(int64_t));
            if (archive.ret_ == OB_SUCCESS) {
              array->source_value_size_ = static_cast<int32_t>(length);
              datum.set_int(reinterpret_cast<int64_t>(array));
            }
          }
        } else {
          if (datum.ptr_ == nullptr || datum.len_ != sizeof(int64_t)) { archive.ret_ = OB_INVALID_ARGUMENT; break; }
          const auto *array = reinterpret_cast<const DatumParameterArray *>(datum.get_int());
          if (array == nullptr || array->count_ < 0 || (array->count_ > 0 && array->data_ == nullptr)) {
            archive.ret_ = OB_INVALID_ARGUMENT; break;
          }
          SizeArchive size;
          array_body(*array, size, depth);
          if (size.ret_ != OB_SUCCESS) { archive.ret_ = size.ret_; break; }
          const int64_t zero = 0;
          archive.field(zero); archive.field(size.pos_);
          if constexpr (std::is_same_v<Archive, SizeArchive>) { archive.add(size.pos_); }
          else { array_body(*array, archive, depth); }
        }
      } else {
        int64_t extended = 0;
        if constexpr (!Archive::reading) {
          if (datum.ptr_ == nullptr || !datum.has_ext_value()) { archive.ret_ = OB_INVALID_ARGUMENT; }
          else {
            extended = datum.get_ext();
            const bool sentinel = (extended >= ObActionFlag::OP_MIN_OBJ && extended <= ObActionFlag::OP_LOCK_ROW)
                || extended == DATUM_MIN_EXT_VALUE || extended == DATUM_MAX_EXT_VALUE;
            if (meta.is_pl_extend_type() && !sentinel) { archive.ret_ = OB_NOT_SUPPORTED; }
          }
        }
        archive.field(extended);
        if constexpr (Archive::reading) {
          const bool sentinel = (extended >= ObActionFlag::OP_MIN_OBJ && extended <= ObActionFlag::OP_LOCK_ROW)
              || extended == DATUM_MIN_EXT_VALUE || extended == DATUM_MAX_EXT_VALUE || extended == 0;
          if (archive.ret_ == OB_SUCCESS && !sentinel) { archive.ret_ = OB_NOT_SUPPORTED; }
          archive.allocate(datum, DATUM_EXTEND_PAYLOAD_SIZE);
          if (archive.ret_ == OB_SUCCESS) {
            datum.set_extend(extended, meta.get_extend_type(), 0, meta.get_cs_level(), meta.get_cs_type());
          }
        }
      }
      break;
    }
    default: archive.ret_ = OB_NOT_SUPPORTED; break;
  }
}

template <typename Archive>
void parameter(ParameterView<Archive> &parameter_value, Archive &archive, int depth)
{
  Datum datum(parameter_value.datum_);
  SqlTypeMeta meta(parameter_value.get_type_meta());
  if constexpr (Archive::reading) {
    read_schema_native(datum, meta, archive, depth);
  } else {
    alignas(8) char scratch[64];
    ObDatum legacy;
    archive.ret_ = meta.is_invalid() ? OB_ERR_UNEXPECTED
        : DatumParameterCodec::export_native_value(datum, meta, legacy, scratch, sizeof(scratch));
    if (archive.ret_ == OB_SUCCESS) { value(legacy, meta, archive, depth); }
  }
  ObAccuracy accuracy(parameter_value.get_accuracy());
  uint32_t flags = parameter_value.get_result_flag();
  archive.field(accuracy); archive.field(flags);
  if constexpr (Archive::reading) {
    if (archive.ret_ == OB_SUCCESS) {
      parameter_value.set_datum(datum);
      parameter_value.set_type_meta(meta);
      parameter_value.set_accuracy(accuracy);
      parameter_value.set_result_flags(flags);
    }
  }
}

// Only the historical schema value domain chooses this representation. It is
// not a complete SQL type and must never be used to prepare semantic operators.
enum class SchemaScalar { Reference, Signed, Unsigned, Float, Double };
SchemaScalar schema_scalar(const SqlTypeMeta &meta)
{
  switch (meta.get_type()) {
    case ObTinyIntType: case ObSmallIntType: case ObMediumIntType:
    case ObInt32Type: case ObIntType: case ObDateTimeType: case ObTimestampType:
    case ObDateType: case ObTimeType: case ObMySQLDateType: case ObMySQLDateTimeType:
      return SchemaScalar::Signed;
    case ObUTinyIntType: case ObUSmallIntType: case ObUMediumIntType:
    case ObUInt32Type: case ObUInt64Type: case ObYearType: case ObBitType:
    case ObEnumType: case ObSetType:
      return SchemaScalar::Unsigned;
    case ObFloatType: case ObUFloatType: return SchemaScalar::Float;
    case ObDoubleType: case ObUDoubleType: return SchemaScalar::Double;
    default: return SchemaScalar::Reference;
  }
}

template <typename T> T schema_load(const void *bytes)
{
  T result;
  MEMCPY(&result, bytes, sizeof(result));
  return result;
}

// Generic wrappers preserve their historical reference byte domain. Unlike
// schema clone, this does not reinterpret a Decimal coefficient using precision
// or restrict a wire-accepted span to arithmetic's supported coefficient widths.
// Only scalar loads and address/extended access require an exact safe shape.
bool valid_generic_legacy(const ObDatum &datum, const SqlTypeMeta &meta)
{
  if (meta.get_type() < ObNullType || meta.get_type() >= ObMaxType) { return false; }
  if (datum.is_null() || meta.is_null()) { return true; }
  if (datum.len_ > 0 && datum.ptr_ == nullptr) { return false; }
  switch (schema_scalar(meta)) {
    case SchemaScalar::Signed:
      return datum.ptr_ != nullptr && datum.len_ == (meta.get_type() == ObDateType
          || meta.get_type() == ObMySQLDateType ? sizeof(int32_t) : sizeof(int64_t));
    case SchemaScalar::Unsigned:
      return datum.ptr_ != nullptr && datum.len_ == (meta.get_type() == ObYearType
          ? sizeof(uint8_t) : sizeof(uint64_t));
    case SchemaScalar::Float: return datum.ptr_ != nullptr && datum.len_ == sizeof(float);
    case SchemaScalar::Double: return datum.ptr_ != nullptr && datum.len_ == sizeof(double);
    case SchemaScalar::Reference:
      if (meta.is_unknown() || meta.is_ext_sql_array()) {
        return datum.ptr_ != nullptr && datum.len_ == sizeof(int64_t);
      }
      return !meta.is_ext() || datum.has_ext_value();
  }
  return false;
}

// Match the former schema clone's physical validation. Owning these bytes does
// not interpret string collation or recover a missing declaration/enum labels.
bool valid_schema_legacy(const ObDatum &datum, const SqlTypeMeta &meta)
{
  const SqlValueType type = meta.get_type();
  if (type < ObNullType || type >= ObMaxType) { return false; }
  if (datum.is_null() || meta.is_null()) { return true; }
  if (datum.ptr_ == nullptr && datum.len_ > 0) { return false; }
  if (meta.is_ext_sql_array()) { return datum.len_ == sizeof(int64_t); }
  switch (ObDatum::get_sql_value_datum_map_type(type)) {
    case OBJ_DATUM_8BYTE_DATA: return datum.len_ == sizeof(int64_t);
    case OBJ_DATUM_4BYTE_DATA: return datum.len_ == sizeof(int32_t);
    case OBJ_DATUM_1BYTE_DATA: return datum.len_ == sizeof(uint8_t);
    case OBJ_DATUM_EXTEND: return datum.ptr_ != nullptr && datum.has_ext_value();
    case OBJ_DATUM_NUMBER: {
      if (datum.len_ < sizeof(number::ObNumber::Desc)) { return false; }
      const auto desc = schema_load<number::ObNumber::Desc>(datum.ptr_);
      return datum.len_ == sizeof(desc) + sizeof(uint32_t) * desc.len_;
    }
    case OBJ_DATUM_DECIMALINT:
      return datum.len_ == 0 || datum.len_ == 4 || datum.len_ == 8
          || datum.len_ == 16 || datum.len_ == 32 || datum.len_ == 64;
    default: return true;
  }
}

int borrow_schema_native(const ObDatum &source, const SqlTypeMeta &meta, Datum &output)
{
  int ret = OB_SUCCESS;
  Datum candidate;
  if (source.is_null()) {
    candidate.set_null();
  } else {
    switch (schema_scalar(meta)) {
      case SchemaScalar::Signed:
        candidate.set_int(meta.get_type() == ObDateType || meta.get_type() == ObMySQLDateType
            ? schema_load<int32_t>(source.ptr_) : schema_load<int64_t>(source.ptr_));
        break;
      case SchemaScalar::Unsigned:
        candidate.set_uint(meta.get_type() == ObYearType
            ? schema_load<uint8_t>(source.ptr_) : schema_load<uint64_t>(source.ptr_));
        break;
      case SchemaScalar::Float: candidate.set_float(schema_load<float>(source.ptr_)); break;
      case SchemaScalar::Double: candidate.set_double(schema_load<double>(source.ptr_)); break;
      case SchemaScalar::Reference: {
        const uint16_t flags = source.is_outrow() ? Datum::OutRow
            : source.has_lob_header() ? Datum::HasLobHeader
            : source.is_ext() ? Datum::LegacyExtended : Datum::NoReferenceFlags;
        // A bare state can discard the wrapper only when all its remaining
        // bytes can be recovered from the historical metadata. NOW and resource
        // wrappers retain their actual bytes and explicit Reference layout.
        if (meta.is_ext() && !meta.is_ext_sql_array() && meta.get_extend_type() == 0
            && source.ptr_ != nullptr && source.has_ext_value()
            && source.get_extend_type() == 0 && source.get_extend_size() == 0
            && source.get_extend_collation_level() == meta.get_cs_level()
            && source.get_extend_collation() == meta.get_cs_type()
            && (source.is_min() || source.is_max() || source.is_nop())) {
          if (source.is_min()) { candidate.set_min(); }
          else if (source.is_max()) { candidate.set_max(); }
          else { candidate.set_nop(); }
        } else {
          ret = candidate.set_reference(source.ptr_, source.len_, flags);
        }
        break;
      }
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}

template <typename T>
int export_schema_scalar(T value, ObDatum &output, void *scratch, int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (capacity < static_cast<int64_t>(sizeof(T))) { ret = OB_BUF_NOT_ENOUGH; }
  else if (scratch == nullptr) { ret = OB_INVALID_ARGUMENT; }
  else {
    MEMCPY(scratch, &value, sizeof(T));
    output.ptr_ = static_cast<const char *>(scratch);
    output.pack_ = sizeof(T);
  }
  return ret;
}

template <typename Wire, auto Setter>
void read_schema_scalar(Datum &datum, ReadArchive &archive)
{
  Wire scalar{};
  archive.field(scalar);
  if (archive.ret_ == OB_SUCCESS) { (datum.*Setter)(scalar); }
}

void read_schema_native(Datum &datum, SqlTypeMeta &meta, ReadArchive &archive, int depth)
{
  if (archive.ret_ != OB_SUCCESS) { return; }
  if (depth > MAX_ARRAY_DEPTH) { archive.ret_ = OB_SIZE_OVERFLOW; return; }
  if (!archive.allow_omitted_fields_) { archive.require(4); }
  archive.field(meta);
  if (archive.ret_ != OB_SUCCESS) { return; }
  if (meta.is_invalid()) { archive.ret_ = OB_ERR_UNEXPECTED; return; }
  // Decode directly into the active inline member. In particular, no old 8B
  // allocation precedes the native integer. All reference bodies use the same
  // existing codec (including omission rules and recursive SQL-array values).
  switch (meta.get_type()) {
    case ObNullType: datum.set_null(); break;
    case ObTinyIntType: read_schema_scalar<int8_t, &Datum::set_int>(datum, archive); break;
    case ObSmallIntType: read_schema_scalar<int16_t, &Datum::set_int>(datum, archive); break;
    case ObMediumIntType: case ObInt32Type:
      read_schema_scalar<int32_t, &Datum::set_int>(datum, archive); break;
    case ObIntType: case ObDateTimeType: case ObTimestampType: case ObTimeType: case ObMySQLDateTimeType:
      read_schema_scalar<int64_t, &Datum::set_int>(datum, archive); break;
    case ObDateType: case ObMySQLDateType:
      read_schema_scalar<int32_t, &Datum::set_int>(datum, archive); break;
    case ObUTinyIntType: case ObYearType:
      read_schema_scalar<uint8_t, &Datum::set_uint>(datum, archive); break;
    case ObUSmallIntType: read_schema_scalar<uint16_t, &Datum::set_uint>(datum, archive); break;
    case ObUMediumIntType: case ObUInt32Type:
      read_schema_scalar<uint32_t, &Datum::set_uint>(datum, archive); break;
    case ObUInt64Type: case ObBitType: case ObEnumType: case ObSetType:
      read_schema_scalar<uint64_t, &Datum::set_uint>(datum, archive); break;
    case ObFloatType: case ObUFloatType:
      read_schema_scalar<float, &Datum::set_float>(datum, archive); break;
    case ObDoubleType: case ObUDoubleType:
      read_schema_scalar<double, &Datum::set_double>(datum, archive); break;
    default: {
      ObDatum owned;
      owned.set_null();
      value(owned, meta, archive, depth, false /* include_metadata */);
      if (archive.ret_ == OB_SUCCESS) { archive.ret_ = borrow_schema_native(owned, meta, datum); }
      // Normalizing an Extend wrapper to a bare MIN/MAX/NOP state leaves no
      // reference in the native value. An explicit-free owner must not lose
      // that temporary codec allocation on a successful decode.
      if (archive.ret_ == OB_SUCCESS && !datum.is_present() && owned.ptr_ != nullptr) {
        archive.allocator_.free(const_cast<char *>(owned.ptr_));
      }
      break;
    }
  }
}

bool valid_buffer(const void *buf, int64_t length, int64_t pos)
{ return buf != nullptr && pos >= 0 && length >= pos; }

} // anonymous namespace

template <>
int DatumParamStore::serialize(char *buf, int64_t length, int64_t &pos) const
{
  return DatumParameterCodec::encode_store(*this, buf, length, pos);
}

template <>
int DatumParamStore::deserialize(const char *buf, int64_t length, int64_t &pos)
{
  ObWrapperAllocator allocator(get_block_allocator());
  return DatumParameterCodec::decode_store(buf, length, pos, allocator, *this);
}

template <>
int64_t DatumParamStore::get_serialize_size() const
{
  int64_t size = 0;
  const int ret = DatumParameterCodec::store_size(*this, size);
  if (ret != OB_SUCCESS) {
    COMMON_LOG(WARN, "native parameter store has no supported wire size", K(ret));
  }
  return size;
}

int DatumParameterArray::serialize(char *buf, int64_t length, int64_t &pos) const
{
  return DatumParameterCodec::encode_array(*this, buf, length, pos);
}

int64_t DatumParameterArray::get_serialize_size() const
{
  int64_t size = 0;
  const int ret = DatumParameterCodec::array_size(*this, size);
  if (ret != OB_SUCCESS) { COMMON_LOG(WARN, "native parameter array has no supported wire size", K(ret)); }
  return size;
}

int DatumParameterCodec::array_size(const DatumParameterArray &array, int64_t &size)
{
  SizeArchive archive;
  array_body(array, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { size = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::encode_array(const DatumParameterArray &array, char *buf, int64_t length, int64_t &pos)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  WriteArchive archive(buf, length, pos);
  array_body(array, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::decode_array(const char *buf, int64_t length, int64_t &pos,
                                     ObIAllocator &allocator, DatumParameterArray *&array)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  ReadArchive archive(buf, length, pos, allocator);
  DatumParameterArray *decoded = nullptr;
  read_array_body(archive, decoded, 0);
  if (archive.ret_ == OB_SUCCESS) { array = decoded; pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::decode_deferred_array(const char *buf, int64_t length,
                                               ObIAllocator &allocator, ObDatum &output)
{
  if (buf == nullptr || length <= 0 || length > INT32_MAX) { return OB_INVALID_ARGUMENT; }
  DeferredArrayDecodeAllocator transaction(allocator);
  ReadArchive archive(buf, length, 0, transaction);
  DatumParameterArray *array = nullptr;
  read_array_body(archive, array, 0);
  if (archive.ret_ == OB_SUCCESS && archive.pos_ != length) { archive.ret_ = OB_DESERIALIZE_ERROR; }
  ObDatum owned;
  archive.allocate(owned, sizeof(int64_t));
  if (archive.ret_ == OB_SUCCESS) {
    array->source_value_size_ = static_cast<int32_t>(length);
    owned.set_int(reinterpret_cast<int64_t>(array));
    output = owned;
    transaction.commit();
  }
  return archive.ret_;
}

int DatumParameterCodec::store_size(const DatumParamStore &store, int64_t &size)
{
  SizeArchive archive;
  archive.field(store.count());
  for (int64_t i = 0; archive.ret_ == OB_SUCCESS && i < store.count(); ++i) {
    parameter(store.at(i), archive, 0);
  }
  if (archive.ret_ == OB_SUCCESS) { size = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::encode_store(const DatumParamStore &store, char *buf,
                                      int64_t length, int64_t &pos)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  WriteArchive archive(buf, length, pos);
  archive.field(store.count());
  for (int64_t i = 0; archive.ret_ == OB_SUCCESS && i < store.count(); ++i) {
    parameter(store.at(i), archive, 0);
  }
  if (archive.ret_ == OB_SUCCESS) { pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::decode_store(const char *buf, int64_t length, int64_t &pos,
                                      ObIAllocator &allocator, DatumParamStore &store)
{
  store.reuse();
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  ReadArchive archive(buf, length, pos, allocator);
  int64_t count = 0;
  archive.field(count);
  if (archive.ret_ == OB_SUCCESS && (count < 0 || count > (length - archive.pos_) / 6)) {
    archive.ret_ = OB_DESERIALIZE_ERROR;
  }
  if (archive.ret_ == OB_SUCCESS) { archive.ret_ = store.prepare_allocate(count); }
  for (int64_t i = 0; archive.ret_ == OB_SUCCESS && i < count; ++i) {
    parameter(store.at(i), archive, 0);
  }
  if (archive.ret_ == OB_SUCCESS) { pos = archive.pos_; }
  else { store.reuse(); }
  return archive.ret_;
}

int DatumParameterCodec::value_size(const ObDatum &datum, const SqlTypeMeta &meta, int64_t &size)
{
  ObDatum view(datum);
  SqlTypeMeta type(meta);
  SizeArchive archive;
  value(view, type, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { size = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::encode_value(const ObDatum &datum, const SqlTypeMeta &meta,
                                      char *buf, int64_t length, int64_t &pos)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  ObDatum view(datum);
  SqlTypeMeta type(meta);
  WriteArchive archive(buf, length, pos);
  value(view, type, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::decode_value(const char *buf, int64_t length, int64_t &pos,
                                      ObIAllocator &allocator, ObDatum &datum, SqlTypeMeta &meta)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  ObDatum output;
  output.set_null();
  SqlTypeMeta type;
  ReadArchive archive(buf, length, pos, allocator);
  value(output, type, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { datum = output; meta = type; pos = archive.pos_; }
  return archive.ret_;
}

static int decode_metadata_value(const char *buf, int64_t length, int64_t &pos,
                                      ObIAllocator &allocator, ObDatum &datum, SqlTypeMeta &meta,
                                      bool allow_missing_string_terminator, bool reuse_metadata)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  ObDatum output;
  output.set_null();
  // UNIS members deserialize into an existing value: metadata fields absent
  // from a short body keep their previous values. Payload decoding still
  // starts from a fresh owned Datum, matching the old zeroed value union.
  // Schema decoding keeps its existing fresh-metadata behavior.
  SqlTypeMeta type = reuse_metadata ? meta : SqlTypeMeta();
  ReadArchive archive(buf, length, pos, allocator, true, allow_missing_string_terminator);
  value(output, type, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { datum = output; meta = type; pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::decode_schema_value(const char *buf, int64_t length, int64_t &pos,
    ObIAllocator &allocator, ObDatum &datum, SqlTypeMeta &meta)
{ return decode_metadata_value(buf, length, pos, allocator, datum, meta, false, false); }
int DatumParameterCodec::decode_unis_value(const char *buf, int64_t length, int64_t &pos,
    ObIAllocator &allocator, ObDatum &datum, SqlTypeMeta &meta)
{ return decode_metadata_value(buf, length, pos, allocator, datum, meta, true, true); }

int DatumParameterCodec::export_native_value(const Datum &source, const SqlTypeMeta &meta,
    ObDatum &output, void *scratch, int64_t capacity)
{
  int ret = OB_SUCCESS;
  ObDatum candidate;
  if (meta.get_type() < ObNullType || meta.get_type() >= ObMaxType || capacity < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (source.is_null()) {
    candidate.set_null();
  } else if (!source.is_present()) {
    if (!meta.is_ext() || meta.is_ext_sql_array() || meta.get_extend_type() != 0
        || !(source.is_min() || source.is_max() || source.is_nop())) {
      ret = OB_INVALID_ARGUMENT;
    } else if (capacity < DATUM_EXTEND_PAYLOAD_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
    } else if (scratch == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      candidate.ptr_ = static_cast<const char *>(scratch);
      candidate.set_extend(source.get_ext(), 0, 0, meta.get_cs_level(), meta.get_cs_type());
    }
  } else if (source.is_inline_extended()) {
    if (!meta.is_ext()) { ret = OB_INVALID_ARGUMENT; }
    else if (capacity < source.inline_extended_payload_size()) { ret = OB_BUF_NOT_ENOUGH; }
    else if (OB_FAIL(source.write_inline_extended_payload(scratch, source.inline_extended_payload_size()))) {
    } else {
      candidate.ptr_ = static_cast<const char *>(scratch);
      candidate.set_ext(source.inline_extended_payload_size());
    }
  } else if (schema_scalar(meta) != SchemaScalar::Reference) {
    if (source.length() != 0 || source.reference_flags() != Datum::NoReferenceFlags) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      switch (schema_scalar(meta)) {
        case SchemaScalar::Signed:
          ret = meta.get_type() == ObDateType || meta.get_type() == ObMySQLDateType
              ? export_schema_scalar(static_cast<int32_t>(source.get_int()), candidate, scratch, capacity)
              : export_schema_scalar(source.get_int(), candidate, scratch, capacity);
          break;
        case SchemaScalar::Unsigned:
          ret = meta.get_type() == ObYearType
              ? export_schema_scalar(static_cast<uint8_t>(source.get_uint()), candidate, scratch, capacity)
              : export_schema_scalar(source.get_uint(), candidate, scratch, capacity);
          break;
        case SchemaScalar::Float:
          ret = export_schema_scalar(source.get_float(), candidate, scratch, capacity); break;
        case SchemaScalar::Double:
          ret = export_schema_scalar(source.get_double(), candidate, scratch, capacity); break;
        default: ret = OB_ERR_UNEXPECTED; break;
      }
    }
  } else if (source.length() > datum_length_limit()
      || (source.length() > 0 && source.get_reference() == nullptr)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    candidate.ptr_ = static_cast<const char *>(source.get_reference());
    candidate.pack_ = source.length();
    switch (source.reference_flags()) {
      case Datum::NoReferenceFlags: break;
      case Datum::OutRow: candidate.set_outrow(); break;
      case Datum::HasLobHeader: candidate.set_has_lob_header(); break;
      case Datum::LegacyExtended: candidate.set_ext(source.length()); break;
      default: ret = OB_INVALID_ARGUMENT; break;
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}

int DatumParameterCodec::export_schema_legacy_value(const Datum &source, const SqlTypeMeta &meta,
    ObDatum &output, void *scratch, int64_t capacity)
{
  return export_native_value(source, meta, output, scratch, capacity);
}

namespace
{
void release_native_array(DatumParameterArray *array, ObIAllocator &allocator)
{
  if (array != nullptr) {
    for (int64_t i = 0; i < array->count_; ++i) {
      DatumParameterCodec::release_owned_schema_native_value(
          array->data_[i].datum_, array->data_[i].get_type_meta(), allocator);
      array->data_[i].~DatumParameter();
    }
    array->~DatumParameterArray();
    allocator.free(array);
  }
}

int clone_schema_graph(const Datum &source, const SqlTypeMeta &meta,
                       ObIAllocator &allocator, Datum &output, int depth)
{
  int ret = OB_SUCCESS;
  alignas(8) char scratch[64];
  ObDatum view;
  Datum candidate;
  if (depth > MAX_ARRAY_DEPTH) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(DatumParameterCodec::export_native_value(source, meta, view, scratch, sizeof(scratch)))) {
  } else if (!(depth == 0 ? valid_schema_legacy(view, meta) : valid_generic_legacy(view, meta))) {
    // The outer schema domain validates its own shape. Historical array leaves
    // used Parameter's physical span clone, not schema precision/width checks.
    ret = OB_INVALID_ARGUMENT;
  } else if (source.is_null() || !source.is_present() || schema_scalar(meta) != SchemaScalar::Reference) {
    candidate = source;
  } else if (meta.is_ext_sql_array()) {
    const auto *array = reinterpret_cast<const DatumParameterArray *>(view.get_int());
    DatumParameterArray *owned = nullptr;
    if (array == nullptr || array->count_ < 0 || (array->count_ > 0 && array->data_ == nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    } else if ((owned = DatumParameterArray::alloc(allocator, array->count_)) == nullptr) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      owned->element_ = array->element_;
      owned->source_value_size_ = array->source_value_size_;
      // This explicit-free physical clone deliberately leaves runtime leaf
      // Types unbound. Typed retaining uses clone_native_parameter_value with
      // its separate bulk-owner contract; no Type tree leaks into this codec.
      for (int64_t i = 0; ret == OB_SUCCESS && i < array->count_; ++i) {
        Datum leaf;
        const DatumParameter &input = array->data_[i];
        if (OB_FAIL(clone_schema_graph(input.datum_, input.get_type_meta(), allocator, leaf, depth + 1))) {
        } else {
          owned->data_[i] = input;
          owned->data_[i].set_datum(leaf);
        }
      }
      if (ret == OB_SUCCESS) {
        void *bytes = allocator.alloc(sizeof(int64_t));
        if (bytes == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          const int64_t address = reinterpret_cast<int64_t>(owned);
          MEMCPY(bytes, &address, sizeof(address));
          ret = candidate.set_reference(bytes, sizeof(address), source.reference_flags());
          if (ret != OB_SUCCESS) { allocator.free(bytes); }
        }
      }
      if (ret != OB_SUCCESS) { release_native_array(owned, allocator); }
    }
  } else {
    ret = DatumParameterCodec::copy_native_value_span(source, meta, allocator, candidate);
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}
} // anonymous namespace

int DatumParameterCodec::borrow_native_value(const ObDatum &source, const SqlTypeMeta &meta,
                                             Datum &output)
{
  return valid_generic_legacy(source, meta) ? borrow_schema_native(source, meta, output)
                                           : OB_INVALID_ARGUMENT;
}

int DatumParameterCodec::native_span_size(const Datum &source, const SqlTypeMeta &meta,
                                         int64_t &output)
{
  alignas(8) char scratch[64];
  ObDatum view;
  int ret = export_native_value(source, meta, view, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS && !valid_generic_legacy(view, meta)) { ret = OB_INVALID_ARGUMENT; }
  if (ret == OB_SUCCESS) {
    output = source.is_present() && !source.is_inline_extended() && schema_scalar(meta) == SchemaScalar::Reference
        ? source.length() : 0;
  }
  return ret;
}

int DatumParameterCodec::copy_native_span(const Datum &source, const SqlTypeMeta &meta,
    char *buffer, int64_t length, int64_t &pos, Datum &output)
{
  if (length < 0 || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
  int64_t bytes = 0;
  int ret = native_span_size(source, meta, bytes);
  Datum candidate = source;
  if (ret == OB_SUCCESS && bytes > length - pos) { ret = OB_BUF_NOT_ENOUGH; }
  if (ret == OB_SUCCESS && source.is_present() && !source.is_inline_extended()
      && schema_scalar(meta) == SchemaScalar::Reference) {
    if (bytes > 0 && buffer == nullptr) { ret = OB_INVALID_ARGUMENT; }
    else {
      char *destination = buffer == nullptr ? nullptr : buffer + pos;
      if (bytes > 0) { MEMMOVE(destination, source.get_reference(), bytes); }
      ret = candidate.set_reference(destination, source.length(), source.reference_flags());
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; pos += bytes; }
  return ret;
}

int DatumParameterCodec::copy_native_value_span(const Datum &source, const SqlTypeMeta &meta,
                                               ObIAllocator &allocator, Datum &output)
{
  alignas(8) char scratch[64];
  ObDatum view;
  Datum candidate;
  int ret = export_native_value(source, meta, view, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS && !valid_generic_legacy(view, meta)) { ret = OB_INVALID_ARGUMENT; }
  if (ret == OB_SUCCESS) {
    if (!source.is_present() || source.is_inline_extended() || schema_scalar(meta) != SchemaScalar::Reference) {
      candidate = source;
    } else if (meta.is_pl_extend_type() && source.has_ext_value()) {
      // A native PL copy owns no carrier allocation, including when its input
      // is an explicitly borrowed old ABI descriptor. Preserve all payload
      // metadata and the legacy envelope; the resource itself remains borrowed.
      candidate.set_inline_extended(source.get_ext(), source.get_extend_type(), source.get_extend_size(),
          source.get_extend_collation_level(), source.get_extend_collation(),
          source.length() == DATUM_LEGACY_EXTEND_SIZE);
    } else {
      void *bytes = allocator.alloc(std::max<int64_t>(source.length(), 1));
      if (bytes == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        if (source.length() > 0) { MEMCPY(bytes, source.get_reference(), source.length()); }
        ret = candidate.set_reference(bytes, source.length(), source.reference_flags());
        if (ret != OB_SUCCESS) { allocator.free(bytes); }
      }
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}

int DatumParameterCodec::import_native_value(const ObDatum &source, const SqlTypeMeta &meta,
                                             ObIAllocator &allocator, Datum &output)
{
  Datum borrowed;
  int ret = borrow_native_value(source, meta, borrowed);
  if (ret == OB_SUCCESS) { ret = copy_native_value_span(borrowed, meta, allocator, output); }
  return ret;
}

int DatumParameterCodec::copy_legacy_value(const Datum &source, const SqlTypeMeta &meta,
                                          ObIAllocator &allocator, ObDatum &output)
{
  alignas(8) char scratch[64];
  ObDatum view;
  int ret = export_native_value(source, meta, view, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS && !valid_generic_legacy(view, meta)) { ret = OB_INVALID_ARGUMENT; }
  if (ret == OB_SUCCESS && !view.is_null()) {
    char *bytes = static_cast<char *>(allocator.alloc(std::max<int64_t>(view.len_, 1)));
    if (bytes == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      if (view.len_ > 0) { MEMCPY(bytes, view.ptr_, view.len_); }
      view.ptr_ = bytes;
    }
  }
  if (ret == OB_SUCCESS) { output = view; }
  return ret;
}

int DatumParameterCodec::native_value_size(const Datum &source, const SqlTypeMeta &meta, int64_t &size)
{
  if (meta.is_invalid()) { return OB_ERR_UNEXPECTED; }
  alignas(8) char scratch[64];
  ObDatum view;
  const int ret = export_native_value(source, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? value_size(view, meta, size) : ret;
}

int DatumParameterCodec::encode_native_value(const Datum &source, const SqlTypeMeta &meta,
                                             char *buf, int64_t length, int64_t &pos)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  if (meta.is_invalid()) { return OB_ERR_UNEXPECTED; }
  alignas(8) char scratch[64];
  ObDatum view;
  const int ret = export_native_value(source, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? encode_value(view, meta, buf, length, pos) : ret;
}

int DatumParameterCodec::decode_native_value(const char *buf, int64_t length, int64_t &pos,
    ObIAllocator &allocator, Datum &output, SqlTypeMeta &meta)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  Datum candidate;
  SqlTypeMeta type;
  ReadArchive archive(buf, length, pos, allocator);
  read_schema_native(candidate, type, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { output = candidate; meta = type; pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::clone_schema_native_value(const Datum &source, const SqlTypeMeta &meta,
    ObIAllocator &allocator, Datum &output)
{
  return clone_schema_graph(source, meta, allocator, output, 0);
}

int DatumParameterCodec::borrow_schema_native_value(const ObDatum &source, const SqlTypeMeta &meta,
    Datum &output)
{
  return valid_schema_legacy(source, meta) ? borrow_schema_native(source, meta, output)
                                           : OB_INVALID_ARGUMENT;
}

int DatumParameterCodec::import_schema_native_value(const ObDatum &source, const SqlTypeMeta &meta,
    ObIAllocator &allocator, Datum &output)
{
  int ret = OB_SUCCESS;
  Datum borrowed;
  if (!valid_schema_legacy(source, meta)) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(borrow_schema_native(source, meta, borrowed))) {
  } else { ret = clone_schema_native_value(borrowed, meta, allocator, output); }
  return ret;
}

void DatumParameterCodec::release_owned_schema_native_value(Datum &value, const SqlTypeMeta &meta,
    ObIAllocator &allocator)
{
  if (value.is_present() && !value.is_inline_extended() && schema_scalar(meta) == SchemaScalar::Reference) {
    void *bytes = const_cast<void *>(value.get_reference());
    if (bytes != nullptr) {
      if (meta.is_ext_sql_array()) {
        int64_t address = 0;
        MEMCPY(&address, bytes, sizeof(address));
        release_native_array(reinterpret_cast<DatumParameterArray *>(address), allocator);
      }
      // Even a zero-length reference owns its allocation. PL and LOB wrappers
      // keep their existing byte ownership; their external pointees are not ours.
      allocator.free(bytes);
    }
  }
  value.reset();
}

int DatumParameterCodec::schema_native_value_size(const Datum &source, const SqlTypeMeta &meta,
    int64_t &size)
{
  alignas(8) char scratch[64];
  ObDatum view;
  const int ret = export_schema_legacy_value(source, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? value_size(view, meta, size) : ret;
}

int DatumParameterCodec::encode_schema_native_value(const Datum &source, const SqlTypeMeta &meta,
    char *buf, int64_t length, int64_t &pos)
{
  alignas(8) char scratch[64];
  ObDatum view;
  const int ret = export_schema_legacy_value(source, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? encode_value(view, meta, buf, length, pos) : ret;
}

int DatumParameterCodec::decode_schema_native_value(const char *buf, int64_t length, int64_t &pos,
    ObIAllocator &allocator, Datum &output, SqlTypeMeta &meta)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  Datum candidate;
  SqlTypeMeta type;
  ReadArchive archive(buf, length, pos, allocator, true /* allow_omitted_fields */);
  read_schema_native(candidate, type, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { output = candidate; meta = type; pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::decode_unis_native_value(const char *buf, int64_t length, int64_t &pos,
    ObIAllocator &allocator, Datum &output, SqlTypeMeta &meta)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  Datum candidate;
  SqlTypeMeta type = meta;
  ReadArchive archive(buf, length, pos, allocator, true /* allow_omitted_fields */,
                      true /* allow_missing_string_terminator */);
  read_schema_native(candidate, type, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { output = candidate; meta = type; pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::borrow_raw_constant_value(const ObDatum &source, const SqlTypeMeta &meta,
                                                   Datum &output)
{
  int ret = OB_SUCCESS;
  Datum candidate;
  if (!valid_schema_legacy(source, meta)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (meta.is_unknown() && !source.is_null()) {
    candidate.set_int(schema_load<int64_t>(source.ptr_));
  } else {
    ret = borrow_schema_native(source, meta, candidate);
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}

int DatumParameterCodec::export_raw_constant_value(const Datum &source, const SqlTypeMeta &meta,
                                                   ObDatum &output, void *scratch, int64_t capacity)
{
  int ret = OB_SUCCESS;
  ObDatum candidate;
  if (meta.is_unknown() && !source.is_null()) {
    if (!source.is_present() || source.length() != 0
        || source.reference_flags() != Datum::NoReferenceFlags || capacity < 0) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ret = export_schema_scalar(source.get_int(), candidate, scratch, capacity);
    }
  } else {
    ret = export_schema_legacy_value(source, meta, candidate, scratch, capacity);
  }
  if (ret == OB_SUCCESS && !valid_schema_legacy(candidate, meta)) {
    ret = OB_INVALID_ARGUMENT;
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}

int DatumParameterCodec::clone_raw_constant_value(const Datum &source, const SqlTypeMeta &meta,
                                                  ObIAllocator &allocator, Datum &output)
{
  int ret = OB_SUCCESS;
  if (meta.is_unknown()) {
    alignas(8) char scratch[sizeof(int64_t)];
    ObDatum view;
    if (OB_FAIL(export_raw_constant_value(source, meta, view, scratch, sizeof(scratch)))) {
    } else { output = source; }
  } else {
    ret = clone_schema_native_value(source, meta, allocator, output);
  }
  return ret;
}

int DatumParameterCodec::parameter_size(const DatumParameter &source, int64_t &size)
{
  SizeArchive archive;
  parameter(source, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { size = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::encode_parameter(const DatumParameter &source, char *buf,
                                          int64_t length, int64_t &pos)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  WriteArchive archive(buf, length, pos);
  parameter(source, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { pos = archive.pos_; }
  return archive.ret_;
}

int DatumParameterCodec::decode_parameter(const char *buf, int64_t length, int64_t &pos,
                                          ObIAllocator &allocator, DatumParameter &output)
{
  if (!valid_buffer(buf, length, pos)) { return OB_INVALID_ARGUMENT; }
  DatumParameter candidate;
  ReadArchive archive(buf, length, pos, allocator);
  parameter(candidate, archive, 0);
  if (archive.ret_ == OB_SUCCESS) { output = candidate; pos = archive.pos_; }
  return archive.ret_;
}

int DatumValue::assign_native_borrowed(const Datum &source, const SqlTypeMeta &meta)
{
  alignas(8) char scratch[64];
  ObDatum view;
  int ret = DatumParameterCodec::export_native_value(source, meta, view, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS && !valid_generic_legacy(view, meta)) { ret = OB_INVALID_ARGUMENT; }
  if (ret == OB_SUCCESS) { datum_ = source; meta_ = meta; }
  return ret;
}

int DatumValue::assign_native_owned(const Datum &source, const SqlTypeMeta &meta, ObIAllocator &owner)
{
  Datum copy;
  const int ret = DatumParameterCodec::copy_native_value_span(source, meta, owner, copy);
  if (ret == OB_SUCCESS) { datum_ = copy; meta_ = meta; }
  return ret;
}

int DatumValue::assign_legacy_borrowed(const ObDatum &source, const SqlTypeMeta &meta)
{
  Datum borrowed;
  const int ret = DatumParameterCodec::borrow_native_value(source, meta, borrowed);
  if (ret == OB_SUCCESS) { datum_ = borrowed; meta_ = meta; }
  return ret;
}

int DatumValue::assign_legacy_owned(const ObDatum &source, const SqlTypeMeta &meta, ObIAllocator &owner)
{
  Datum copy;
  const int ret = DatumParameterCodec::import_native_value(source, meta, owner, copy);
  if (ret == OB_SUCCESS) { datum_ = copy; meta_ = meta; }
  return ret;
}

int DatumValue::export_legacy(ObDatum &output, void *scratch, int64_t capacity) const
{ return DatumParameterCodec::export_native_value(datum_, meta_, output, scratch, capacity); }

int DatumValue::copy_legacy(ObDatum &output, ObIAllocator &owner) const
{ return DatumParameterCodec::copy_legacy_value(datum_, meta_, owner, output); }

int DatumValue::deep_copy(const DatumValue &source, ObIAllocator &owner)
{ return assign_native_owned(source.datum_, source.meta_, owner); }

int DatumValue::assign_reference_bytes(const void *bytes, int64_t length, uint16_t flags,
                                      const SqlTypeMeta &meta, ObIAllocator &owner)
{
  if (length < 0 || length > datum_length_limit() || (length > 0 && bytes == nullptr)) {
    return OB_INVALID_ARGUMENT;
  }
  void *copy = owner.alloc(std::max<int64_t>(length, 1));
  if (copy == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  if (length > 0) { MEMCPY(copy, bytes, length); }
  Datum candidate;
  const int ret = candidate.set_reference(copy, length, flags);
  if (ret != OB_SUCCESS) { owner.free(copy); }
  else { datum_ = candidate; meta_ = meta; }
  return ret;
}

int DatumValue::set_extend(int64_t value, uint8_t subtype, ObIAllocator &owner, int32_t size)
{
  SqlTypeMeta meta = meta_; meta.set_ext(); meta.set_extend_type(subtype);
  if (!meta.is_ext_sql_array()) {
    Datum candidate;
    if (subtype == 0 && size == 0 && value == DATUM_MIN_EXT_VALUE) { candidate.set_min(); }
    else if (subtype == 0 && size == 0 && value == DATUM_MAX_EXT_VALUE) { candidate.set_max(); }
    else if (subtype == 0 && size == 0 && value == ObActionFlag::OP_NOP) { candidate.set_nop(); }
    else { candidate.set_inline_extended(value, subtype, size); }
    datum_ = candidate;
    meta_ = meta;
    return OB_SUCCESS;
  }
  alignas(8) char bytes[DATUM_EXTEND_PAYLOAD_SIZE];
  ObDatum source;
  if (meta.is_ext_sql_array()) {
    MEMCPY(bytes, &value, sizeof(value));
    source.set_string(bytes, sizeof(value));
  } else {
    source.ptr_ = bytes;
    source.set_extend(value, subtype, size);
  }
  return assign_legacy_owned(source, meta, owner);
}

int64_t DatumValue::get_data_length() const
{
  alignas(8) char scratch[64];
  ObDatum view;
  const int ret = export_legacy(view, scratch, sizeof(scratch));
  OB_ASSERT(ret == OB_SUCCESS);
  return view.len_;
}

int DatumValue::to_row_datum(ObDatum &output,
                            char (&legacy_payload)[DATUM_EXTEND_PAYLOAD_SIZE]) const
{
  int ret = OB_SUCCESS;
  ObDatum candidate;
  // A native clone may retain the historical 16-byte envelope. The row ABI
  // below normalizes Extend to 15 bytes, so first read that envelope in local
  // storage rather than requiring the caller's 15-byte result buffer to hold it.
  char extended_scratch[DATUM_LEGACY_EXTEND_SIZE];
  if (!meta_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (is_null() || meta_.is_null()) {
    candidate.set_null();
  } else if (OB_FAIL(export_legacy(candidate,
      datum_.is_inline_extended() ? extended_scratch : legacy_payload,
      datum_.is_inline_extended() ? sizeof(extended_scratch) : sizeof(legacy_payload)))) {
  } else if (is_ext()) {
    if ((is_ext_sql_array() && (candidate.ptr_ == nullptr || candidate.len_ != sizeof(int64_t)))
        || (!is_ext_sql_array() && !candidate.has_ext_value())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      candidate.ptr_ = legacy_payload;
      candidate.set_extend(get_ext(), meta_.get_extend_type(), get_val_len(),
                           meta_.get_cs_level(), meta_.get_cs_type());
    }
  } else {
    const SqlValueDatumMapType mapping = ObDatum::get_sql_value_datum_map_type(get_type());
    const bool fixed = mapping == OBJ_DATUM_8BYTE_DATA
        || mapping == OBJ_DATUM_4BYTE_DATA || mapping == OBJ_DATUM_1BYTE_DATA;
    if ((candidate.len_ > 0 && candidate.ptr_ == nullptr)
        || (fixed && candidate.len_ != ObDatum::get_reserved_size(mapping))
        || (mapping == OBJ_DATUM_NUMBER && (candidate.ptr_ == nullptr
            || candidate.len_ < sizeof(number::ObNumber::Desc)
            || candidate.len_ != sizeof(number::ObNumber::Desc)
                + sizeof(uint32_t) * candidate.get_number_desc().len_))) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      candidate.pack_ = candidate.len_;
      if (mapping == OBJ_DATUM_STRING && meta_.is_outrow()) { candidate.set_outrow(); }
    }
  }
  if (ret == OB_SUCCESS) { output = candidate; }
  return ret;
}

int DatumParameter::assign_native_borrowed(const Datum &source, const SqlTypeMeta &meta)
{
  DatumValue candidate;
  const int ret = candidate.assign_native_borrowed(source, meta);
  if (ret == OB_SUCCESS) { assign_value(candidate); }
  return ret;
}

int DatumParameter::assign_native_owned(const Datum &source, const SqlTypeMeta &meta, ObIAllocator &owner)
{
  Datum copy;
  const int ret = DatumParameterCodec::copy_native_value_span(source, meta, owner, copy);
  if (ret == OB_SUCCESS) { datum_ = copy; metadata_.type_meta_ = meta; }
  return ret;
}

int DatumParameter::assign_legacy_borrowed(const ObDatum &source, const SqlTypeMeta &meta)
{
  Datum borrowed;
  const int ret = DatumParameterCodec::borrow_native_value(source, meta, borrowed);
  if (ret == OB_SUCCESS) { datum_ = borrowed; metadata_.type_meta_ = meta; }
  return ret;
}

int DatumParameter::assign_legacy_owned(const ObDatum &source, const SqlTypeMeta &meta, ObIAllocator &owner)
{
  Datum copy;
  const int ret = DatumParameterCodec::import_native_value(source, meta, owner, copy);
  if (ret == OB_SUCCESS) { datum_ = copy; metadata_.type_meta_ = meta; }
  return ret;
}

int DatumParameter::export_legacy(ObDatum &output, void *scratch, int64_t capacity) const
{ return DatumParameterCodec::export_native_value(datum_, metadata_.type_meta_, output, scratch, capacity); }

int DatumParameter::copy_legacy(ObDatum &output, ObIAllocator &owner) const
{ return DatumParameterCodec::copy_legacy_value(datum_, metadata_.type_meta_, owner, output); }

int DatumParameter::deep_copy(const DatumParameter &source, ObIAllocator &owner)
{
  Datum copy;
  const int ret = DatumParameterCodec::copy_native_value_span(source.datum_, source.get_type_meta(), owner, copy);
  if (ret == OB_SUCCESS) { *this = source; datum_ = copy; }
  return ret;
}

int32_t datum_array_source_value_size(const Datum &address)
{
  int32_t size = 0;
  if (address.is_present() && address.get_reference() != nullptr && address.length() == sizeof(int64_t)) {
    const auto *array = reinterpret_cast<const DatumParameterArray *>(address.get_unknown());
    if (array != nullptr) { size = array->source_value_size_; }
  }
  return size;
}

int DatumParameter::get_string(ObString &out) const
{
  int ret = OB_OBJ_TYPE_ERROR;
  if (!is_null() && (get_type_meta().is_string_or_lob_locator_type() || is_lob_storage())) {
    if (!is_lob_storage() || datum_.length() == 0) {
      out = datum_.get_string();
    } else {
      ObLobLocatorV2 locator(datum_.get_string(), has_lob_header());
      if (locator.is_valid()) {
        // Retain the existing checked string getter's return contract: callers
        // handle out-row locators through the request LOB service separately.
        locator.get_inrow_data(out);
      }
    }
    ret = OB_SUCCESS;
  }
  return ret;
}

int DatumParameter::serialize(char *buf, int64_t length, int64_t &pos) const
{
  return DatumParameterCodec::encode_parameter(*this, buf, length, pos);
}
int DatumParameter::deserialize(const char *buf, int64_t length, int64_t &pos, ObIAllocator &allocator)
{
  return DatumParameterCodec::decode_parameter(buf, length, pos, allocator, *this);
}
int64_t DatumParameter::get_serialize_size() const
{
  int64_t size = 0;
  return OB_SUCCESS == DatumParameterCodec::parameter_size(*this, size) ? size : 0;
}

int DatumParameter::get_real_param_count(int64_t &count) const
{
  int ret = OB_SUCCESS;
  count = 1;
  if (is_ext()) {
    const auto *array = reinterpret_cast<const DatumParameterArray *>(get_ext());
    if (array == nullptr || array->count_ < 0) { ret = OB_ERR_UNEXPECTED; }
    else { count = array->count_; }
  }
  return ret;
}
bool DatumParameter::is_outrow_lob() const
{
  bool result = false;
  if (get_type_meta().is_lob_storage() && datum_.length() > 0 && has_lob_header()) {
    ObLobLocatorV2 locator(datum_.get_string(), true);
    if (locator.is_valid()) { result = !locator.has_inrow_data(); }
  }
  return result;
}

} // namespace common
} // namespace oceanbase
