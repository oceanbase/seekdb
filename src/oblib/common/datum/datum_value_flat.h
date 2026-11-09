/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/datum_parameter_codec.h"
namespace oceanbase { namespace common {
// Flat, explicit-buffer ownership shared by ordinary rows and chunk stores.
// Layout is deterministic: rounded-eight-byte spans; arrays allocate their
// descriptor/parameters, recursively clone children, then store the address.
class NativeValueFlat final
{
public:
  static int64_t span(int64_t bytes)
  { return bytes < 0 || bytes > INT64_MAX - 7 ? INT64_MAX : (bytes + 7) & ~int64_t(7); }
  static bool add_size(int64_t &total, int64_t bytes)
  {
    if (bytes < 0 || bytes > INT64_MAX - total) { total = INT64_MAX; return false; }
    total += bytes; return true;
  }
  // Physical metadata selects the union member even while a reference pointer
  // is an encoded offset. Never inspect reference bytes to guess the layout.
  static bool uses_reference(const DatumValue &value)
  {
    if (!value.datum_.is_present() || value.datum_.is_inline_extended()) { return false; }
    switch (value.get_type()) {
      case ObTinyIntType: case ObSmallIntType: case ObMediumIntType:
      case ObInt32Type: case ObIntType: case ObDateTimeType: case ObTimestampType:
      case ObDateType: case ObTimeType: case ObMySQLDateType: case ObMySQLDateTimeType:
      case ObUTinyIntType: case ObUSmallIntType: case ObUMediumIntType:
      case ObUInt32Type: case ObUInt64Type: case ObYearType: case ObBitType:
      case ObEnumType: case ObSetType: case ObFloatType: case ObUFloatType:
      case ObDoubleType: case ObUDoubleType: return false;
      default: return true;
    }
  }
  static int64_t size(const DatumValue &value, int depth = 0)
  {
    if (depth > 64) { return INT64_MAX; }
    int64_t payload = 0;
    if (DatumParameterCodec::native_span_size(value.datum_, value.meta_, payload) != OB_SUCCESS) {
      return INT64_MAX;
    }
    if (!uses_reference(value) || (value.meta_.is_pl_extend_type() && value.datum_.has_ext_value())) {
      // clone_parameter canonicalizes PL carriers, including explicit old
      // descriptors, into the value. The resource itself remains borrowed.
      return 0;
    }
    int64_t bytes = 0;
    if (value.is_ext_sql_array()) {
      const auto *array = reinterpret_cast<const DatumParameterArray *>(value.get_ext());
      if (array == nullptr || array->count_ < 0
          || array->count_ > (INT64_MAX - sizeof(DatumParameterArray)) / sizeof(DatumParameter)
          || (array->count_ > 0 && array->data_ == nullptr)) { return INT64_MAX; }
      bytes = span(sizeof(DatumParameterArray) + sizeof(DatumParameter) * array->count_);
      for (int64_t i = 0; bytes != INT64_MAX && i < array->count_; ++i) {
        add_size(bytes, size(array->data_[i].value_view(), depth + 1));
      }
      add_size(bytes, span(sizeof(int64_t)));
    } else { bytes = span(MAX(int64_t(1), payload)); }
    return bytes;
  }
  class BufferOwner final : public ObIAllocator
  {
  public:
    BufferOwner(char *buffer, int64_t length, int64_t &pos) : buffer_(buffer), length_(length), pos_(pos) {}
    void *alloc(int64_t bytes) override
    {
      const int64_t count = span(bytes);
      if (buffer_ == nullptr || pos_ < 0 || pos_ > length_ || count > length_ - pos_) { return nullptr; }
      char *value = buffer_ + pos_; pos_ += count;
      if (count > bytes) { MEMSET(value + bytes, 0, count - bytes); }
      return value;
    }
    void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
    void free(void *) override {}
  private:
    char *buffer_; int64_t length_; int64_t &pos_;
  };
  static int take_span(char *buffer, int64_t length, int64_t &pos, int64_t bytes, char *&output)
  {
    const int64_t count = span(bytes);
    if (buffer == nullptr || pos < 0 || pos > length || count > length - pos) { return OB_SIZE_OVERFLOW; }
    output = buffer + pos; pos += count; return OB_SUCCESS;
  }
  static int copy(const DatumValue &source, DatumValue &output, char *buffer,
                   int64_t length, int64_t &pos)
  {
    if (buffer == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
    const int64_t bytes = size(source);
    if (bytes == INT64_MAX || bytes > length - pos) { return OB_SIZE_OVERFLOW; }
    int64_t next = pos; BufferOwner owner(buffer, length, next); DatumParameter input, copy;
    input.assign_value(source);
    const int ret = DatumParameterArray::clone_parameter(input, copy, owner);
    if (ret == OB_SUCCESS) { output = copy.value_view(); pos = next; }
    return ret;
  }
  // Caller may first validate with publish=false and then publish a complete
  // row. Traversal never follows old pointers, even in nested array headers.
  static int relocate(DatumValue &value, char *buffer, int64_t length, int64_t &pos,
                       bool publish = true)
  {
    int64_t next = pos;
    const int ret = walk(value, buffer, length, next, publish, nullptr, 0);
    if (ret == OB_SUCCESS) { pos = next; }
    return ret;
  }
  // Rewrites references and nested array addresses as offset+1 from base.
  // This is the new native memory-image ABI, not the historical value codec.
  // Restore uses the same traversal, not pointer arithmetic on stale offsets.
  static int make_offsets(DatumValue &value, char *buffer, int64_t length, int64_t &pos,
                           const char *base = nullptr, bool publish = true)
  {
    int64_t next = pos;
    const int ret = walk(value, buffer, length, next, publish, base == nullptr ? buffer : base, 0);
    if (ret == OB_SUCCESS) { pos = next; }
    return ret;
  }
  // RowStore/Chunk v2 physical record. Historical generic value wire is
  // unchanged. Preserve state, physical meta/flags/width and complete array
  // attributes, without transmitting descriptor/payload/type pointers.
  static int row_value_size(const DatumValue &value, int64_t &bytes)
  { return row_size(value, bytes, 0); }
  static int encode_row_value(const DatumValue &value, char *buf, int64_t length, int64_t &pos)
  {
    int64_t bytes = 0;
    int ret = row_size(value, bytes, 0);
    if (ret == OB_SUCCESS && (buf == nullptr || pos < 0 || pos > length)) { ret = OB_INVALID_ARGUMENT; }
    if (ret == OB_SUCCESS && bytes > length - pos) { ret = OB_BUF_NOT_ENOUGH; }
    int64_t next = pos;
    if (ret == OB_SUCCESS) { ret = row_encode(value, buf, length, next, 0); }
    if (ret == OB_SUCCESS) { pos = next; }
    return ret;
  }
  static int decode_row_value(const char *buf, int64_t length, int64_t &pos,
                              ObIAllocator &owner, DatumValue &value)
  {
    if (buf == nullptr || pos < 0 || pos > length) { return OB_INVALID_ARGUMENT; }
    int64_t next = pos, budget = (length - pos) / 11 + 1;
    DatumValue candidate;
    const int ret = row_decode(buf, length, next, owner, candidate, 0, budget);
    if (ret == OB_SUCCESS) { value = candidate; pos = next; }
    return ret;
  }
  // Compatibility reader for the accepted 1d6 Linux x86-64 image only.
  // Offsets are relative to image, never native process addresses. This is not
  // the unmodified-master legacy value image, despite both old envelopes using v1.
  static int read_legacy_1d6_value(const char *image, int64_t image_length,
      int64_t descriptor_offset, ObIAllocator &owner, DatumValue &output)
  {
    static_assert(sizeof(ObDatum) == 12 && sizeof(SqlTypeMeta) == 4
        && sizeof(ObAccuracy) == 8, "Accepted legacy image field widths");
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && __SIZEOF_POINTER__ == 8
    if (image == nullptr || image_length < 0) { return OB_INVALID_ARGUMENT; }
    int64_t budget = image_length / 16 + 1;
    return read_legacy_value(image, image_length, descriptor_offset, owner, output, 0, budget);
#else
    UNUSED(image); UNUSED(image_length); UNUSED(descriptor_offset); UNUSED(owner); UNUSED(output);
    return OB_NOT_SUPPORTED;
#endif
  }
private:
  static int read_row_vi64(const char *buf, int64_t length, int64_t &pos, int64_t &output)
  {
    uint64_t bits = 0;
    int64_t next = pos;
    for (uint32_t i = 0; i < 10; ++i) {
      if (next < 0 || next >= length) { return OB_DESERIALIZE_ERROR; }
      const uint8_t byte = static_cast<uint8_t>(buf[next++]);
      if (i == 9 && (byte & 0xfe) != 0) { return OB_DESERIALIZE_ERROR; }
      bits |= uint64_t(byte & 0x7f) << (7 * i);
      if ((byte & 0x80) == 0) { MEMCPY(&output, &bits, sizeof(output)); pos = next; return OB_SUCCESS; }
    }
    return OB_DESERIALIZE_ERROR;
  }
  static bool row_unsigned(SqlValueType kind)
  { return (kind >= ObUTinyIntType && kind <= ObUInt64Type) || kind == ObYearType
      || kind == ObBitType || kind == ObEnumType || kind == ObSetType; }
  static int row_shape(const DatumValue &value)
  {
    int64_t ignored = 0;
    int ret = DatumParameterCodec::native_span_size(value.datum_, value.meta_, ignored);
    if (ret == OB_SUCCESS && !value.datum_.is_present()
        && (value.datum_.length() != 0 || value.datum_.reference_flags() != 0)) { ret = OB_INVALID_DATA; }
    if (ret == OB_SUCCESS && value.datum_.is_present() && value.meta_.is_pl_extend_type()) {
      if (!value.datum_.has_ext_value()) { ret = OB_NOT_SUPPORTED; }
      else {
        const int64_t extended = value.datum_.get_ext();
        const bool marker = (extended >= ObActionFlag::OP_MIN_OBJ && extended <= ObActionFlag::OP_LOCK_ROW)
            || extended == DATUM_MIN_EXT_VALUE || extended == DATUM_MAX_EXT_VALUE || extended == 0;
        if (!marker) { ret = OB_NOT_SUPPORTED; }
      }
    }
    return ret;
  }
  static int row_size(const DatumValue &value, int64_t &output, int depth)
  {
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    if (value.datum_.is_inline_extended()) {
      char scratch[16];
      DatumValue wire = value;
      int ret = value.datum_.write_inline_extended_payload(scratch, sizeof(scratch));
      if (ret == OB_SUCCESS) {
        ret = wire.datum_.set_reference(scratch, value.datum_.inline_extended_payload_size(), Datum::LegacyExtended);
      }
      return ret == OB_SUCCESS ? row_size(wire, output, depth) : ret;
    }
    int ret = row_shape(value);
    int64_t bytes = 11;
    if (ret == OB_SUCCESS && value.datum_.is_present()) {
      if (!uses_reference(value)) { add_size(bytes, 8); }
      else if (!value.is_ext_sql_array()) { add_size(bytes, value.datum_.length()); }
      else {
        const auto *array = reinterpret_cast<const DatumParameterArray *>(value.get_ext());
        if (array == nullptr || array->count_ < 0 || array->source_value_size_ < 0
            || (array->count_ > 0 && array->data_ == nullptr)) { ret = OB_INVALID_ARGUMENT; }
        else {
          add_size(bytes, 22); // element physical metadata 18, source size 4
          add_size(bytes, serialization::encoded_length_vi64(array->count_));
          for (int64_t i = 0; ret == OB_SUCCESS && bytes != INT64_MAX && i < array->count_; ++i) {
            int64_t child = 0;
            ret = row_size(array->data_[i].value_view(), child, depth + 1);
            add_size(bytes, 30); add_size(bytes, child);
          }
        }
      }
    }
    if (ret == OB_SUCCESS && bytes == INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
    if (ret == OB_SUCCESS) { output = bytes; }
    return ret;
  }
  static int row_encode(const DatumValue &value, char *buf, int64_t length, int64_t &pos, int depth)
  {
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    if (value.datum_.is_inline_extended()) {
      char scratch[16];
      DatumValue wire = value;
      int ret = value.datum_.write_inline_extended_payload(scratch, sizeof(scratch));
      if (ret == OB_SUCCESS) {
        ret = wire.datum_.set_reference(scratch, value.datum_.inline_extended_payload_size(), Datum::LegacyExtended);
      }
      return ret == OB_SUCCESS ? row_encode(wire, buf, length, pos, depth) : ret;
    }
    int ret = OB_SUCCESS;
    if (OB_FAIL(serialization::encode(buf, length, pos, value.meta_))) {}
    else if (OB_FAIL(serialization::encode_i8(buf, length, pos, static_cast<int8_t>(value.datum_.state())))) {}
    else if (OB_FAIL(serialization::encode_i16(buf, length, pos, value.datum_.reference_flags()))) {}
    else if (OB_FAIL(serialization::encode_i32(buf, length, pos, value.datum_.length()))) {}
    else if (!value.datum_.is_present()) {}
    else if (!uses_reference(value)) {
      uint64_t bits = 0;
      if (value.get_type() == ObFloatType || value.get_type() == ObUFloatType) {
        const float number = value.datum_.get_float(); uint32_t word = 0;
        MEMCPY(&word, &number, sizeof(word)); bits = word;
      } else if (value.get_type() == ObDoubleType || value.get_type() == ObUDoubleType) {
        const double number = value.datum_.get_double(); MEMCPY(&bits, &number, sizeof(bits));
      } else if (row_unsigned(value.get_type())) { bits = value.datum_.get_uint(); }
      else { const int64_t number = value.datum_.get_int(); MEMCPY(&bits, &number, sizeof(bits)); }
      int64_t word = 0; MEMCPY(&word, &bits, sizeof(word));
      ret = serialization::encode_i64(buf, length, pos, word);
    } else if (value.is_ext_sql_array()) {
      const auto &array = *reinterpret_cast<const DatumParameterArray *>(value.get_ext());
      if (OB_FAIL(encode_row_element(array.element_, buf, length, pos))) {}
      else if (OB_FAIL(serialization::encode_i32(buf, length, pos, array.source_value_size_))) {}
      else if (OB_FAIL(serialization::encode_vi64(buf, length, pos, array.count_))) {}
      for (int64_t i = 0; OB_SUCC(ret) && i < array.count_; ++i) {
        if (OB_FAIL(encode_row_attributes(array.data_[i].get_metadata(), buf, length, pos))) {}
        else { ret = row_encode(array.data_[i].value_view(), buf, length, pos, depth + 1); }
      }
    } else if (value.datum_.length() > length - pos) { ret = OB_BUF_NOT_ENOUGH; }
    else {
      if (value.datum_.length() > 0) { MEMCPY(buf + pos, value.datum_.get_reference(), value.datum_.length()); }
      pos += value.datum_.length();
    }
    return ret;
  }
  static int row_decode(const char *buf, int64_t length, int64_t &pos, ObIAllocator &owner,
                         DatumValue &output, int depth, int64_t &budget)
  {
    if (depth > 64 || budget-- <= 0) { return OB_SIZE_OVERFLOW; }
    if (length - pos < 11) { return OB_DESERIALIZE_ERROR; }
    int ret = OB_SUCCESS;
    DatumValue candidate;
    int8_t state = 0; int16_t flags = 0; int32_t span = 0;
    if (OB_FAIL(serialization::decode(buf, length, pos, candidate.meta_))) {}
    else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &state))) {}
    else if (OB_FAIL(serialization::decode_i16(buf, length, pos, &flags))) {}
    else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &span))) {}
    else if (span < 0 || flags < 0 || state < 0 || state > static_cast<int8_t>(Datum::State::Max)) { ret = OB_DESERIALIZE_ERROR; }
    else if (state != static_cast<int8_t>(Datum::State::Present)) {
      if (span != 0 || flags != 0) { ret = OB_DESERIALIZE_ERROR; }
      else {
        switch (static_cast<Datum::State>(state)) {
          case Datum::State::Null: candidate.datum_.set_null(); break;
          case Datum::State::Nop: candidate.datum_.set_nop(); break;
          case Datum::State::Min: candidate.datum_.set_min(); break;
          case Datum::State::Max: candidate.datum_.set_max(); break;
          default: ret = OB_DESERIALIZE_ERROR; break;
        }
      }
    } else {
      // Only metadata decides representation. This initial present value has
      // no pointer; it is never exported before the selected branch fills it.
      candidate.datum_.set_int(0);
      if (!uses_reference(candidate)) {
        int64_t word = 0;
        if (span != 0 || flags != 0 || length - pos < 8) { ret = OB_DESERIALIZE_ERROR; }
        else if (OB_FAIL(serialization::decode_i64(buf, length, pos, &word))) {}
        else if (candidate.get_type() == ObFloatType || candidate.get_type() == ObUFloatType) {
          uint64_t bits = 0; MEMCPY(&bits, &word, sizeof(bits));
          if (bits > UINT32_MAX) { ret = OB_DESERIALIZE_ERROR; }
          else { const uint32_t small = static_cast<uint32_t>(bits); float number = 0; MEMCPY(&number, &small, sizeof(number)); candidate.datum_.set_float(number); }
        } else if (candidate.get_type() == ObDoubleType || candidate.get_type() == ObUDoubleType) {
          double number = 0; MEMCPY(&number, &word, sizeof(number)); candidate.datum_.set_double(number);
        } else if (row_unsigned(candidate.get_type())) {
          uint64_t bits = 0; MEMCPY(&bits, &word, sizeof(bits)); candidate.datum_.set_uint(bits);
        } else { candidate.datum_.set_int(word); }
      } else if (candidate.meta_.is_ext_sql_array()) {
        ObDataType element;
        int32_t source_size = 0; int64_t count = 0;
        DatumParameterArray *array = nullptr;
        if (span != 8 || flags != 0 || length - pos < 23) { ret = OB_DESERIALIZE_ERROR; }
        else if (OB_FAIL(decode_row_element(buf, length, pos, element))) {}
        else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &source_size))) {}
        else if (OB_FAIL(read_row_vi64(buf, length, pos, count))) {}
        else if (source_size < 0 || count < 0 || count > budget || count > (length - pos) / 41) { ret = OB_DESERIALIZE_ERROR; }
        else if ((array = DatumParameterArray::alloc(owner, count)) == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          array->element_ = element; array->source_value_size_ = source_size;
          for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
            DatumParameterMetadata attributes; DatumValue child;
            if (OB_FAIL(decode_row_attributes(buf, length, pos, attributes))) {}
            else if (OB_FAIL(row_decode(buf, length, pos, owner, child, depth + 1, budget))) {}
            else if (MEMCMP(&attributes.type_meta_, &child.meta_, sizeof(SqlTypeMeta)) != 0) {
              DatumParameterCodec::release_owned_schema_native_value(child.datum_, child.meta_, owner);
              ret = OB_DESERIALIZE_ERROR;
            } else { array->data_[i].set_metadata(attributes); array->data_[i].set_datum(child.datum_); }
          }
          if (OB_SUCC(ret)) {
            const int64_t address = reinterpret_cast<int64_t>(array); ObDatum legacy;
            legacy.ptr_ = reinterpret_cast<const char *>(&address); legacy.pack_ = 8;
            ret = candidate.assign_legacy_owned(legacy, candidate.meta_, owner);
          }
          if (OB_FAIL(ret)) { DatumParameterArray::release_array(array, owner); }
        }
      } else if (span > length - pos) { ret = OB_DESERIALIZE_ERROR; }
      else {
        void *memory = owner.alloc(MAX(int64_t(1), int64_t(span)));
        if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else {
          if (span > 0) { MEMCPY(memory, buf + pos, span); }
          ret = candidate.datum_.set_reference(memory, span, static_cast<uint16_t>(flags));
          if (OB_SUCC(ret)) { pos += span; }
          else { owner.free(memory); }
        }
      }
    }
    if (OB_SUCC(ret)) {
      ret = row_shape(candidate);
      if (OB_FAIL(ret)) { DatumParameterCodec::release_owned_schema_native_value(candidate.datum_, candidate.meta_, owner); }
    }
    if (OB_SUCC(ret)) { output = candidate; }
    return ret;
  }
  static int encode_row_attributes(const DatumParameterMetadata &attributes,
                               char *buf, int64_t length, int64_t &pos)
  {
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    const uint64_t bits = attributes.accuracy_.get_accuracy();
    MEMCPY(&accuracy, &bits, sizeof(accuracy));
    if (length - pos < 30) { return OB_BUF_NOT_ENOUGH; }
    if (OB_FAIL(serialization::encode(buf, length, pos, attributes.type_meta_))) {
    } else if (OB_FAIL(serialization::encode_i64(buf, length, pos, accuracy))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, attributes.res_flags_))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, attributes.raw_text_pos_))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, attributes.raw_text_len_))) {
    } else if (OB_FAIL(serialization::encode(buf, length, pos, attributes.param_meta_))) {
    } else if (OB_FAIL(serialization::encode_i8(buf, length, pos, attributes.flag_.flag_))) {
    } else { ret = serialization::encode_i8(buf, length, pos, attributes.flag_.extend_flag_); }
    return ret;
  }

  static int decode_row_attributes(const char *buf, int64_t length, int64_t &pos,
                               DatumParameterMetadata &attributes)
  {
    if (length - pos < 30) { return OB_DESERIALIZE_ERROR; }
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    int32_t flags = 0;
    int8_t flag = 0, extended_flag = 0;
    if (OB_FAIL(serialization::decode(buf, length, pos, attributes.type_meta_))) {
    } else if (OB_FAIL(serialization::decode_i64(buf, length, pos, &accuracy))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &flags))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &attributes.raw_text_pos_))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &attributes.raw_text_len_))) {
    } else if (OB_FAIL(serialization::decode(buf, length, pos, attributes.param_meta_))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &flag))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &extended_flag))) {
    } else {
      attributes.accuracy_.set_accuracy(static_cast<uint64_t>(accuracy));
      attributes.res_flags_ = static_cast<uint32_t>(flags);
      attributes.flag_.flag_ = static_cast<uint8_t>(flag);
      attributes.flag_.extend_flag_ = static_cast<uint8_t>(extended_flag);
    }
    return ret;
  }

  // Array element_ is historical physical metadata, not a substitute for any
  // leaf's FullType. V2 preserves all of its actual fields explicitly. V1
  // continues to use the established ObDataType codec in the physical port.
  static constexpr int64_t ARRAY_ELEMENT_METADATA_SIZE = 18;
  static int encode_row_element(const ObDataType &element,
                                      char *buf, int64_t length, int64_t &pos)
  {
    if (length - pos < ARRAY_ELEMENT_METADATA_SIZE) { return OB_BUF_NOT_ENOUGH; }
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    const uint64_t bits = element.accuracy_.get_accuracy();
    MEMCPY(&accuracy, &bits, sizeof(accuracy));
    if (OB_FAIL(serialization::encode(buf, length, pos, element.meta_))) {
    } else if (OB_FAIL(serialization::encode_i64(buf, length, pos, accuracy))) {
    } else if (OB_FAIL(serialization::encode_i32(buf, length, pos, static_cast<int32_t>(element.charset_)))) {
    } else if (OB_FAIL(serialization::encode_i8(buf, length, pos, element.is_binary_collation_ ? 1 : 0))) {
    } else { ret = serialization::encode_i8(buf, length, pos, element.is_zero_fill_ ? 1 : 0); }
    return ret;
  }

  static int decode_row_element(const char *buf, int64_t length, int64_t &pos,
                                      ObDataType &element)
  {
    if (length - pos < ARRAY_ELEMENT_METADATA_SIZE) { return OB_DESERIALIZE_ERROR; }
    int ret = OB_SUCCESS;
    int64_t accuracy = 0;
    int32_t charset = 0;
    int8_t binary = 0, zero_fill = 0;
    if (OB_FAIL(serialization::decode(buf, length, pos, element.meta_))) {
    } else if (OB_FAIL(serialization::decode_i64(buf, length, pos, &accuracy))) {
    } else if (OB_FAIL(serialization::decode_i32(buf, length, pos, &charset))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &binary))) {
    } else if (OB_FAIL(serialization::decode_i8(buf, length, pos, &zero_fill))) {
    } else if ((binary != 0 && binary != 1) || (zero_fill != 0 && zero_fill != 1)) {
      ret = OB_DESERIALIZE_ERROR;
    } else {
      element.accuracy_.set_accuracy(static_cast<uint64_t>(accuracy));
      element.charset_ = static_cast<ObCharsetType>(charset);
      element.is_binary_collation_ = binary != 0;
      element.is_zero_fill_ = zero_fill != 0;
    }
    return ret;
  }

  static bool legacy_range(int64_t length, uint64_t offset, uint64_t bytes)
  { return length >= 0 && offset <= static_cast<uint64_t>(length)
      && bytes <= static_cast<uint64_t>(length) - offset; }
  template <typename T>
  static void legacy_field(const char *memory, T &value)
  { MEMCPY(&value, memory, sizeof(value)); }
  static int read_legacy_value(const char *image, int64_t length, int64_t descriptor,
      ObIAllocator &owner, DatumValue &output, int depth, int64_t &budget)
  {
    int ret = OB_SUCCESS;
    if (depth > 64 || budget-- <= 0) { return OB_SIZE_OVERFLOW; }
    if (descriptor < 0 || !legacy_range(length, descriptor, 16)) { return OB_DESERIALIZE_ERROR; }
    uint64_t payload_offset = 0;
    ObDatum source;
    SqlTypeMeta meta;
    legacy_field(image + descriptor, payload_offset);
    legacy_field(image + descriptor + 8, source.pack_);
    legacy_field(image + descriptor + 12, meta);
    DatumValue candidate;
    if (source.is_null()) {
      source.ptr_ = nullptr;
      ret = candidate.assign_legacy_borrowed(source, meta);
    } else if (!legacy_range(length, payload_offset, source.len_)) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (meta.is_ext_sql_array()) {
      uint64_t array_offset = 0;
      if (source.len_ != 8 || source.flag_ != ObDatumDesc::NONE) { return OB_INVALID_DATA; }
      legacy_field(image + payload_offset, array_offset);
      if (!legacy_range(length, array_offset, 48)) { return OB_DESERIALIZE_ERROR; }
      const char *header = image + array_offset;
      uint64_t data_offset = 0;
      int64_t count = 0;
      int32_t source_size = 0;
      legacy_field(header, data_offset);
      legacy_field(header + 8, count);
      legacy_field(header + 40, source_size);
      if (count < 0 || source_size < 0 || count > budget
          || count > INT64_MAX / 64
          || data_offset != array_offset + 48
          || !legacy_range(length, data_offset, static_cast<uint64_t>(count) * 64)) {
        return OB_DESERIALIZE_ERROR;
      }
      // The old header contains physical ObDataType, not a runtime SqlType.
      // Read its fields individually; no old vptr/pointer/padding is copied.
      ObDataType element;
      int32_t charset = 0;
      uint8_t binary = 0, zero_fill = 0;
      legacy_field(header + 16, element.meta_);
      legacy_field(header + 24, element.accuracy_);
      legacy_field(header + 32, charset);
      legacy_field(header + 36, binary);
      legacy_field(header + 37, zero_fill);
      if (binary > 1 || zero_fill > 1) { return OB_INVALID_DATA; }
      element.charset_ = static_cast<ObCharsetType>(charset);
      element.is_binary_collation_ = binary != 0;
      element.is_zero_fill_ = zero_fill != 0;
      DatumParameterArray *array = DatumParameterArray::alloc(owner, count);
      if (array == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
      array->element_ = element;
      array->source_value_size_ = source_size;
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        const int64_t child_offset = static_cast<int64_t>(data_offset) + i * 64;
        DatumValue child;
        if (OB_FAIL(read_legacy_value(image, length, child_offset, owner, child, depth + 1, budget))) {
        } else {
          DatumParameterMetadata attributes;
          const char *old = image + child_offset;
          legacy_field(old + 12, attributes.type_meta_);
          legacy_field(old + 16, attributes.accuracy_);
          legacy_field(old + 24, attributes.res_flags_);
          // Old ParamFlag vptr at +32 and its six padding bytes are ignored.
          legacy_field(old + 40, attributes.flag_.flag_);
          legacy_field(old + 41, attributes.flag_.extend_flag_);
          legacy_field(old + 48, attributes.raw_text_pos_);
          legacy_field(old + 52, attributes.raw_text_len_);
          legacy_field(old + 56, attributes.param_meta_);
          array->data_[i].set_metadata(attributes);
          array->data_[i].set_datum(child.datum_);
        }
      }
      if (OB_SUCC(ret)) {
        const int64_t address = reinterpret_cast<int64_t>(array);
        ObDatum wrapper;
        wrapper.ptr_ = reinterpret_cast<const char *>(&address);
        wrapper.pack_ = sizeof(address);
        ret = candidate.assign_legacy_owned(wrapper, meta, owner);
      }
      if (OB_FAIL(ret)) { DatumParameterArray::release_array(array, owner); }
    } else {
      source.ptr_ = image + payload_offset;
      // Historical physical wire cannot establish ownership of a remote PL
      // process address. Preserve marker support, reject non-marker resources.
      if (meta.is_pl_extend_type() && source.has_ext_value()) {
        const int64_t value = source.get_ext();
        const bool marker = (value >= ObActionFlag::OP_MIN_OBJ && value <= ObActionFlag::OP_LOCK_ROW)
            || value == DATUM_MIN_EXT_VALUE || value == DATUM_MAX_EXT_VALUE || value == 0;
        if (!marker) { ret = OB_NOT_SUPPORTED; }
      }
      if (OB_SUCC(ret)) { ret = candidate.assign_legacy_owned(source, meta, owner); }
    }
    if (OB_SUCC(ret)) { output = candidate; }
    return ret;
  }
  static char *pointer(char *address, const char *base)
  { return base == nullptr ? address : reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(base) + 1); }
  static int walk(DatumValue &value, char *buffer, int64_t length, int64_t &pos,
                    bool publish, const char *offset_base, int depth)
  {
    int ret = OB_SUCCESS;
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    if (buffer == nullptr || pos < 0 || pos > length) { return OB_SIZE_OVERFLOW; }
    if (!uses_reference(value)) {
      int64_t bytes = 0;
      return DatumParameterCodec::native_span_size(value.datum_, value.meta_, bytes);
    }
    char *memory = nullptr;
    if (value.is_ext_sql_array()) {
      if (value.datum_.length() != sizeof(int64_t)
          || value.datum_.reference_flags() != Datum::NoReferenceFlags) { return OB_INVALID_DATA; }
      if (length - pos < sizeof(DatumParameterArray)) { return OB_SIZE_OVERFLOW; }
      auto *array = reinterpret_cast<DatumParameterArray *>(buffer + pos);
      if (array->count_ < 0 || array->count_ > (INT64_MAX - sizeof(*array)) / sizeof(DatumParameter)) { return OB_INVALID_DATA; }
      const int64_t bytes = sizeof(*array) + sizeof(DatumParameter) * array->count_;
      if (OB_FAIL(take_span(buffer, length, pos, bytes, memory))) { return ret; }
      auto *data = reinterpret_cast<DatumParameter *>(memory + sizeof(*array));
      for (int64_t i = 0; OB_SUCC(ret) && i < array->count_; ++i) {
        DatumValue child = data[i].value_view();
        if (OB_FAIL(walk(child, buffer, length, pos, publish, offset_base, depth + 1))) {}
        else if (publish) { data[i].datum_ = child.datum_; }
      }
      if (OB_SUCC(ret) && OB_FAIL(take_span(buffer, length, pos, sizeof(int64_t), memory))) {}
      else if (OB_SUCC(ret) && publish) {
        array->data_ = reinterpret_cast<DatumParameter *>(pointer(reinterpret_cast<char *>(data), offset_base));
        const int64_t address = reinterpret_cast<int64_t>(pointer(reinterpret_cast<char *>(array), offset_base));
        MEMCPY(memory, &address, sizeof(address));
        ret = value.datum_.set_reference(pointer(memory, offset_base), sizeof(address));
      }
    } else if (OB_FAIL(take_span(buffer, length, pos, MAX(int64_t(1), int64_t(value.datum_.length())), memory))) {}
    else {
      // Validate only the bounded, relocated bytes, never the stale pointer.
      Datum checked;
      int64_t bytes = 0;
      if (OB_FAIL(checked.set_reference(memory, value.datum_.length(), value.datum_.reference_flags()))) {}
      else if (OB_FAIL(DatumParameterCodec::native_span_size(checked, value.meta_, bytes))) {}
      else if (publish) {
        ret = value.datum_.set_reference(pointer(memory, offset_base),
            value.datum_.length(), value.datum_.reference_flags());
      }
    }
    return ret;
  }
};
} }
