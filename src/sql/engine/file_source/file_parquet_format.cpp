/**
 * Copyright (c) 2026 OceanBase
 *
 * OceanBase is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * OceanBase is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OceanBase. If not, see <http://www.gnu.org/licenses/>.
 */

#include "sql/engine/file_source/file_parquet_format.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace oceanbase {
namespace sql {
namespace file_source {

namespace {

// thrift compact protocol field types
const int8_t kTypeStop = 0;
const int8_t kTypeBooleanTrue = 1;
const int8_t kTypeBooleanFalse = 2;
const int8_t kTypeByte = 3;
const int8_t kTypeI16 = 4;
const int8_t kTypeI32 = 5;
const int8_t kTypeI64 = 6;
const int8_t kTypeDouble = 7;
const int8_t kTypeBinary = 8;
const int8_t kTypeList = 9;
const int8_t kTypeSet = 10;
const int8_t kTypeMap = 11;
const int8_t kTypeStruct = 12;

/// Convert a number of days since 1970-01-01 into a calendar date.
void civil_from_days(int64_t z, int64_t &year, int64_t &month, int64_t &day)
{
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  day = doy - (153 * mp + 2) / 5 + 1;
  month = mp < 10 ? mp + 3 : mp - 9;
  year = y + (month <= 2 ? 1 : 0);
}

std::string format_date(int64_t days)
{
  int64_t year = 0;
  int64_t month = 0;
  int64_t day = 0;
  civil_from_days(days, year, month, day);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04lld-%02lld-%02lld", (long long)year, (long long)month, (long long)day);
  return buffer;
}

std::string format_datetime(int64_t seconds, int64_t micros)
{
  const int64_t days = seconds / 86400;
  int64_t rest = seconds % 86400;
  if (rest < 0) {
    rest += 86400;
  }
  int64_t year = 0;
  int64_t month = 0;
  int64_t day = 0;
  civil_from_days(days, year, month, day);
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%04lld-%02lld-%02lld %02lld:%02lld:%02lld", (long long)year, (long long)month,
                (long long)day, (long long)(rest / 3600), (long long)((rest % 3600) / 60), (long long)(rest % 60));
  std::string result = buffer;
  if (micros > 0) {
    char fraction[16];
    std::snprintf(fraction, sizeof(fraction), ".%06lld", (long long)micros);
    result += fraction;
  }
  return result;
}

/// Legacy converted types, see `parquet.thrift`.
const int32_t kConvertedUtf8 = 0;
const int32_t kConvertedDate = 6;
const int32_t kConvertedTimestampMillis = 9;
const int32_t kConvertedTimestampMicros = 10;

}  // namespace

//
// ------------------------------------------------------------------ thrift
//

bool ThriftCompactReader::read_varint(uint64_t &value)
{
  value = 0;
  int shift = 0;
  while (pos_ < size_) {
    const unsigned char byte = data_[pos_++];
    value |= static_cast<uint64_t>(byte & 0x7F) << shift;
    if (0 == (byte & 0x80)) {
      return true;
    }
    shift += 7;
    if (shift > 63) {
      return false;
    }
  }
  return false;
}

bool ThriftCompactReader::read_zigzag(int64_t &value)
{
  uint64_t raw = 0;
  if (!read_varint(raw)) {
    return false;
  }
  value = static_cast<int64_t>((raw >> 1) ^ (~(raw & 1) + 1));
  return true;
}

bool ThriftCompactReader::read_uint32(uint32_t &value)
{
  uint64_t raw = 0;
  if (!read_varint(raw)) {
    return false;
  }
  value = static_cast<uint32_t>(raw);
  return true;
}

bool ThriftCompactReader::read_int32(int32_t &value)
{
  int64_t zigzag = 0;
  if (!read_zigzag(zigzag)) {
    return false;
  }
  value = static_cast<int32_t>(zigzag);
  return true;
}

bool ThriftCompactReader::read_int64(int64_t &value) { return read_zigzag(value); }

bool ThriftCompactReader::read_bool(bool &value)
{
  value = true;
  return true;
}

bool ThriftCompactReader::read_binary(std::string &value)
{
  uint32_t length = 0;
  if (!read_uint32(length)) {
    return false;
  }
  if (pos_ + length > size_) {
    return false;
  }
  value.assign(reinterpret_cast<const char *>(data_ + pos_), length);
  pos_ += length;
  return true;
}

bool ThriftCompactReader::read_field_begin(int16_t &field_id, int8_t &type, bool &is_stop)
{
  if (pos_ >= size_) {
    is_stop = true;
    return true;
  }
  const unsigned char header = data_[pos_++];
  type = static_cast<int8_t>(header & 0x0F);
  const int8_t delta = static_cast<int8_t>((header & 0xF0) >> 4);
  if (kTypeStop == type) {
    is_stop = true;
    return true;
  }
  is_stop = false;
  if (0 == delta) {
    int64_t id = 0;
    if (!read_zigzag(id)) {
      return false;
    }
    field_id = static_cast<int16_t>(id);
  } else if (15 == delta) {
    int64_t id = 0;
    if (!read_zigzag(id)) {
      return false;
    }
    field_id = static_cast<int16_t>(id);
  } else {
    field_id = static_cast<int16_t>(last_field_id_ + delta);
  }
  last_field_id_ = field_id;
  return true;
}

void ThriftCompactReader::push_struct()
{
  field_id_stack_.push_back(last_field_id_);
  last_field_id_ = 0;
}

void ThriftCompactReader::pop_struct()
{
  if (!field_id_stack_.empty()) {
    last_field_id_ = field_id_stack_.back();
    field_id_stack_.pop_back();
  }
}

bool ThriftCompactReader::read_list_begin(int8_t &elem_type, uint32_t &size)
{
  if (pos_ >= size_) {
    return false;
  }
  const unsigned char header = data_[pos_++];
  elem_type = static_cast<int8_t>(header & 0x0F);
  size = static_cast<uint32_t>((header & 0xF0) >> 4);
  if (15 == size) {
    uint64_t count = 0;
    if (!read_varint(count)) {
      return false;
    }
    size = static_cast<uint32_t>(count);
  }
  return true;
}

bool ThriftCompactReader::skip_field(int8_t type)
{
  switch (type) {
    case kTypeBooleanTrue:
    case kTypeBooleanFalse: return true;
    case kTypeByte: {
      if (pos_ >= size_) {
        return false;
      }
      ++pos_;
      return true;
    }
    case kTypeI16:
    case kTypeI32:
    case kTypeI64: {
      uint64_t value = 0;
      return read_varint(value);
    }
    case kTypeDouble: {
      if (pos_ + 8 > size_) {
        return false;
      }
      pos_ += 8;
      return true;
    }
    case kTypeBinary: {
      std::string value;
      return read_binary(value);
    }
    case kTypeList:
    case kTypeSet: {
      int8_t elem_type = 0;
      uint32_t count = 0;
      if (!read_list_begin(elem_type, count)) {
        return false;
      }
      for (uint32_t i = 0; i < count; ++i) {
        if (!skip_field(elem_type)) {
          return false;
        }
      }
      return true;
    }
    case kTypeMap: {
      uint64_t count = 0;
      if (!read_varint(count)) {
        return false;
      }
      if (0 == count) {
        return true;
      }
      if (pos_ >= size_) {
        return false;
      }
      const int8_t key_type = static_cast<int8_t>(data_[pos_] & 0x0F);
      const int8_t value_type = static_cast<int8_t>((data_[pos_] & 0xF0) >> 4);
      ++pos_;
      for (uint64_t i = 0; i < count; ++i) {
        if (!skip_field(key_type) || !skip_field(value_type)) {
          return false;
        }
      }
      return true;
    }
    case kTypeStruct: {
      push_struct();
      while (true) {
        int16_t field_id = 0;
        int8_t field_type = 0;
        bool is_stop = false;
        if (!read_field_begin(field_id, field_type, is_stop)) {
          pop_struct();
          return false;
        }
        if (is_stop) {
          pop_struct();
          return true;
        }
        if (!skip_field(field_type)) {
          pop_struct();
          return false;
        }
      }
    }
    default: return false;
  }
}

//
// ------------------------------------------------------------------ snappy
//

bool snappy_uncompress(const unsigned char *src, size_t src_size, std::vector<unsigned char> &dst)
{
  dst.clear();
  if (src_size == 0) {
    return false;
  }
  size_t pos = 0;
  uint64_t expected = 0;
  int shift = 0;
  while (pos < src_size) {
    const unsigned char byte = src[pos++];
    expected |= static_cast<uint64_t>(byte & 0x7F) << shift;
    if (0 == (byte & 0x80)) {
      break;
    }
    shift += 7;
    if (shift > 63) {
      return false;
    }
  }
  dst.reserve(static_cast<size_t>(expected));
  while (pos < src_size) {
    const unsigned char tag = src[pos++];
    const int type = tag & 0x03;
    if (0 == type) {
      uint64_t length = (tag >> 2) + 1;
      if (length > 60) {
        const uint64_t extra = length - 60;
        if (pos + extra > src_size) {
          return false;
        }
        length = 0;
        for (uint64_t i = 0; i < extra; ++i) {
          length |= static_cast<uint64_t>(src[pos + i]) << (8 * i);
        }
        pos += extra;
        length += 1;
      }
      if (pos + length > src_size) {
        return false;
      }
      dst.insert(dst.end(), src + pos, src + pos + length);
      pos += length;
      continue;
    }
    uint64_t length = 0;
    uint64_t offset = 0;
    if (1 == type) {
      if (pos >= src_size) {
        return false;
      }
      length = 4 + ((tag >> 2) & 0x07);
      offset = (static_cast<uint64_t>(tag >> 5) << 8) | src[pos];
      pos += 1;
    } else if (2 == type) {
      if (pos + 2 > src_size) {
        return false;
      }
      length = 1 + (tag >> 2);
      offset = static_cast<uint64_t>(src[pos]) | (static_cast<uint64_t>(src[pos + 1]) << 8);
      pos += 2;
    } else {
      if (pos + 4 > src_size) {
        return false;
      }
      length = 1 + (tag >> 2);
      offset = static_cast<uint64_t>(src[pos]) | (static_cast<uint64_t>(src[pos + 1]) << 8)
               | (static_cast<uint64_t>(src[pos + 2]) << 16) | (static_cast<uint64_t>(src[pos + 3]) << 24);
      pos += 4;
    }
    if (0 == offset || offset > dst.size()) {
      return false;
    }
    size_t from = dst.size() - static_cast<size_t>(offset);
    for (uint64_t i = 0; i < length; ++i) {
      dst.push_back(dst[from + i]);
    }
  }
  return expected == 0 || dst.size() == expected;
}

//
// -------------------------------------------------------------- rle / plain
//

bool decode_rle_hybrid(const unsigned char *data,
                       size_t data_size,
                       int32_t bit_width,
                       int64_t count,
                       std::vector<int32_t> &values,
                       size_t &consumed)
{
  const uint64_t mask = (bit_width >= 64) ? ~0ULL : ((1ULL << bit_width) - 1);
  const size_t value_bytes = (bit_width + 7) / 8;
  size_t pos = 0;
  while (static_cast<int64_t>(values.size()) < count && pos < data_size) {
    uint64_t header = 0;
    int shift = 0;
    bool done = false;
    while (pos < data_size) {
      const unsigned char byte = data[pos++];
      header |= static_cast<uint64_t>(byte & 0x7F) << shift;
      if (0 == (byte & 0x80)) {
        done = true;
        break;
      }
      shift += 7;
    }
    if (!done) {
      break;
    }
    if (0 == (header & 1)) {
      // run length encoded run
      const int64_t run_length = static_cast<int64_t>(header >> 1);
      if (pos + value_bytes > data_size) {
        break;
      }
      uint32_t value = 0;
      for (size_t i = 0; i < value_bytes; ++i) {
        value |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
      }
      pos += value_bytes;
      for (int64_t i = 0; i < run_length && static_cast<int64_t>(values.size()) < count; ++i) {
        values.push_back(static_cast<int32_t>(value & mask));
      }
      continue;
    }
    // bit packed run
    const int64_t groups = static_cast<int64_t>(header >> 1);
    const int64_t packed_values = groups * 8;
    const size_t needed = static_cast<size_t>(groups) * value_bytes;
    if (pos + needed > data_size) {
      break;
    }
    uint64_t buffer = 0;
    int bits = 0;
    size_t buffer_pos = pos;
    for (int64_t i = 0; i < packed_values && static_cast<int64_t>(values.size()) < count; ++i) {
      while (bits < bit_width) {
        buffer |= static_cast<uint64_t>(data[buffer_pos++]) << bits;
        bits += 8;
      }
      values.push_back(static_cast<int32_t>(buffer & mask));
      buffer >>= bit_width;
      bits -= bit_width;
    }
    pos += needed;
  }
  consumed = pos;
  return static_cast<int64_t>(values.size()) >= count;
}

bool decode_plain_values(const unsigned char *data,
                         size_t data_size,
                         int32_t physical_type,
                         int32_t type_length,
                         int32_t converted_type,
                         int64_t count,
                         std::vector<std::string> &values)
{
  size_t pos = 0;
  switch (physical_type) {
    case static_cast<int32_t>(ParquetPhysicalType::BOOLEAN): {
      uint64_t buffer = 0;
      int bits = 0;
      for (int64_t i = 0; i < count; ++i) {
        while (bits < 1) {
          if (pos >= data_size) {
            return false;
          }
          buffer |= static_cast<uint64_t>(data[pos++]) << bits;
          bits += 8;
        }
        values.push_back((buffer & 1) ? "true" : "false");
        buffer >>= 1;
        bits -= 1;
      }
      return true;
    }
    case static_cast<int32_t>(ParquetPhysicalType::INT32): {
      for (int64_t i = 0; i < count; ++i) {
        if (pos + 4 > data_size) {
          return false;
        }
        int32_t value = 0;
        std::memcpy(&value, data + pos, 4);
        pos += 4;
        if (kConvertedDate == converted_type) {
          values.push_back(format_date(value));
        } else {
          values.push_back(std::to_string(value));
        }
      }
      return true;
    }
    case static_cast<int32_t>(ParquetPhysicalType::INT64): {
      for (int64_t i = 0; i < count; ++i) {
        if (pos + 8 > data_size) {
          return false;
        }
        int64_t value = 0;
        std::memcpy(&value, data + pos, 8);
        pos += 8;
        if (kConvertedTimestampMillis == converted_type) {
          values.push_back(format_datetime(value / 1000, (value % 1000) * 1000));
        } else if (kConvertedTimestampMicros == converted_type) {
          int64_t micros = value % 1000000;
          int64_t seconds = value / 1000000;
          if (micros < 0) {
            micros += 1000000;
            seconds -= 1;
          }
          values.push_back(format_datetime(seconds, micros));
        } else {
          values.push_back(std::to_string(value));
        }
      }
      return true;
    }
    case static_cast<int32_t>(ParquetPhysicalType::FLOAT): {
      for (int64_t i = 0; i < count; ++i) {
        if (pos + 4 > data_size) {
          return false;
        }
        float value = 0;
        std::memcpy(&value, data + pos, 4);
        pos += 4;
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
        values.push_back(buffer);
      }
      return true;
    }
    case static_cast<int32_t>(ParquetPhysicalType::DOUBLE): {
      for (int64_t i = 0; i < count; ++i) {
        if (pos + 8 > data_size) {
          return false;
        }
        double value = 0;
        std::memcpy(&value, data + pos, 8);
        pos += 8;
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.15g", value);
        values.push_back(buffer);
      }
      return true;
    }
    case static_cast<int32_t>(ParquetPhysicalType::BYTE_ARRAY): {
      for (int64_t i = 0; i < count; ++i) {
        if (pos + 4 > data_size) {
          return false;
        }
        uint32_t length = 0;
        std::memcpy(&length, data + pos, 4);
        pos += 4;
        if (pos + length > data_size) {
          return false;
        }
        values.push_back(std::string(reinterpret_cast<const char *>(data + pos), length));
        pos += length;
      }
      return true;
    }
    case static_cast<int32_t>(ParquetPhysicalType::FIXED_LEN_BYTE_ARRAY): {
      const size_t width = type_length > 0 ? static_cast<size_t>(type_length) : 0;
      for (int64_t i = 0; i < count; ++i) {
        if (pos + width > data_size) {
          return false;
        }
        values.push_back(std::string(reinterpret_cast<const char *>(data + pos), width));
        pos += width;
      }
      return true;
    }
    default: return false;
  }
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
