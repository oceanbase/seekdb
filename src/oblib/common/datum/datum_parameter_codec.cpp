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
void value(ObDatum &datum, SqlTypeMeta &meta, Archive &archive, int depth)
{
  if (archive.ret_ != OB_SUCCESS) { return; }
  if (depth > MAX_ARRAY_DEPTH) { archive.ret_ = OB_SIZE_OVERFLOW; return; }
  if constexpr (!Archive::reading) {
    if (meta.is_invalid()) { archive.ret_ = OB_ERR_UNEXPECTED; return; }
    if (datum.is_null() && !meta.is_null()) { meta.set_null(); }
  }
  // Protocol readers stay strict. The schema reader explicitly retains the
  // old OB_UNIS_DECODE omission rule for shorter version-1 bodies.
  if constexpr (Archive::reading) {
    if (!archive.allow_omitted_fields_) { archive.require(4); }
  } else {
    archive.require(4);
  }
  archive.field(meta);
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
  ObDatum datum(parameter_value.datum_);
  SqlTypeMeta meta(parameter_value.get_type_meta());
  value(datum, meta, archive, depth);
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

int32_t datum_array_source_value_size(const ObDatum &address)
{
  int32_t size = 0;
  if (!address.is_null() && address.ptr_ != nullptr && address.len_ == sizeof(int64_t)) {
    const auto *array = reinterpret_cast<const DatumParameterArray *>(address.get_int());
    if (array != nullptr) { size = array->source_value_size_; }
  }
  return size;
}

int DatumParameter::get_string(ObString &out) const
{
  int ret = OB_OBJ_TYPE_ERROR;
  if (!is_null() && (type_meta_.is_string_or_lob_locator_type() || is_lob_storage())) {
    if (!is_lob_storage() || datum_.len_ == 0) {
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
  if (type_meta_.is_lob_storage() && datum_.len_ > 0 && has_lob_header()) {
    ObLobLocatorV2 locator(datum_.get_string(), true);
    if (locator.is_valid()) { result = !locator.has_inrow_data(); }
  }
  return result;
}

} // namespace common
} // namespace oceanbase
