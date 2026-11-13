/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "file_sql_parquet.h"

#include <cstring>
#include <memory>
#include <string>

namespace oceanbase { namespace sql { namespace file_sql {
namespace {

// ---------------------------------------------------------------------------
// Thrift compact protocol
// ---------------------------------------------------------------------------

enum class ThriftType
{
  STOP = 0x00, TRUE = 0x01, FALSE = 0x02, BYTE = 0x03, I16 = 0x04, I32 = 0x05,
  I64 = 0x06, DOUBLE = 0x07, BINARY = 0x08, LIST = 0x09, SET = 0x0A,
  MAP = 0x0B, STRUCT = 0x0C
};

// A minimal reader for the Thrift compact protocol. Parquet uses it for the
// file footer and for every page header.
class ThriftReader
{
public:
  ThriftReader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

  bool ok() const { return ok_; }
  size_t offset() const { return offset_; }

  uint8_t read_byte()
  {
    if (offset_ >= size_) { ok_ = false; return 0; }
    return data_[offset_++];
  }
  uint64_t read_varint()
  {
    uint64_t result = 0;
    int shift = 0;
    while (true) {
      const uint8_t byte = read_byte();
      if (!ok_) { return 0; }
      result |= static_cast<uint64_t>(byte & 0x7f) << shift;
      if ((byte & 0x80) == 0) { break; }
      shift += 7;
      if (shift > 63) { ok_ = false; return 0; }
    }
    return result;
  }
  int64_t read_zigzag()
  {
    const uint64_t value = read_varint();
    return static_cast<int64_t>((value >> 1) ^ (~(value & 1) + 1));
  }
  double read_double()
  {
    if (offset_ + 8 > size_) { ok_ = false; return 0; }
    double value = 0;
    std::memcpy(&value, data_ + offset_, 8);
    offset_ += 8;
    return value;
  }
  std::string read_binary()
  {
    const uint64_t length = read_varint();
    if (!ok_ || offset_ + length > size_) { ok_ = false; return {}; }
    std::string value(reinterpret_cast<const char *>(data_ + offset_), length);
    offset_ += length;
    return value;
  }
  // Reads a list or a set header and returns the element count and type.
  bool read_list_header(uint32_t &count, uint8_t &element_type)
  {
    const uint8_t header = read_byte();
    if (!ok_) { return false; }
    const uint8_t size = static_cast<uint8_t>((header >> 4) & 0x0f);
    element_type = header & 0x0f;
    if (size == 0x0f) { count = static_cast<uint32_t>(read_varint()); }
    else { count = size; }
    return ok_;
  }

private:
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t offset_ = 0;
  bool ok_ = true;
};

// A value of the Thrift compact protocol, decoded into a generic tree.
// The struct members are held behind a shared pointer because a value cannot
// contain a pair of its own incomplete type.
struct ThriftValue
{
  ThriftType type = ThriftType::STOP;
  bool boolean = false;
  int64_t integer = 0;
  double real = 0;
  std::string binary;
  std::vector<ThriftValue> elements;   // list / set
  // The members of a struct, in the order in which they appear.
  std::vector<std::pair<int16_t, std::shared_ptr<ThriftValue>>> fields;
  // The element type of a list or set.
  uint8_t element_type = 0;

  const ThriftValue *field(int16_t id) const
  {
    for (const auto &entry : fields) {
      if (entry.first == id) { return entry.second.get(); }
    }
    return nullptr;
  }
  int64_t field_int(int16_t id, int64_t fallback) const
  {
    const ThriftValue *value = field(id);
    return (value != nullptr) ? value->integer : fallback;
  }
  const std::string *field_binary(int16_t id) const
  {
    const ThriftValue *value = field(id);
    return (value != nullptr) ? &value->binary : nullptr;
  }
};

// Decodes one compact-protocol value.
ThriftValue thrift_read_value(ThriftReader &reader, uint8_t type)
{
  ThriftValue value;
  value.type = static_cast<ThriftType>(type);
  switch (value.type) {
    case ThriftType::STOP:
    case ThriftType::TRUE:
      value.boolean = true;
      break;
    case ThriftType::FALSE:
      value.boolean = false;
      break;
    case ThriftType::BYTE:
      value.integer = reader.read_byte();
      break;
    case ThriftType::I16:
    case ThriftType::I32:
    case ThriftType::I64:
      value.integer = reader.read_zigzag();
      break;
    case ThriftType::DOUBLE:
      value.real = reader.read_double();
      break;
    case ThriftType::BINARY:
      value.binary = reader.read_binary();
      break;
    case ThriftType::LIST:
    case ThriftType::SET: {
      uint32_t count = 0;
      uint8_t element_type = 0;
      if (!reader.read_list_header(count, element_type)) { break; }
      value.element_type = element_type;
      value.elements.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        value.elements.push_back(thrift_read_value(reader, element_type));
        if (!reader.ok()) { break; }
      }
      break;
    }
    case ThriftType::MAP: {
      const uint64_t count = reader.read_varint();
      if (count > 0) {
        const uint8_t types = reader.read_byte();
        const uint8_t key_type = static_cast<uint8_t>((types >> 4) & 0x0f);
        const uint8_t value_type = types & 0x0f;
        for (uint64_t i = 0; i < count; ++i) {
          thrift_read_value(reader, key_type);
          thrift_read_value(reader, value_type);
          if (!reader.ok()) { break; }
        }
      }
      break;
    }
    case ThriftType::STRUCT: {
      int16_t field_id = 0;
      while (true) {
        const uint8_t header = reader.read_byte();
        if (!reader.ok() || header == 0x00) { break; }
        const uint8_t field_type = header & 0x0f;
        const uint8_t delta = static_cast<uint8_t>((header >> 4) & 0x0f);
        if (delta == 0) { field_id = static_cast<int16_t>(reader.read_zigzag()); }
        else { field_id = static_cast<int16_t>(field_id + delta); }
        value.fields.emplace_back(field_id,
                                  std::make_shared<ThriftValue>(thrift_read_value(reader, field_type)));
        if (!reader.ok()) { break; }
      }
      break;
    }
  }
  return value;
}

// ---------------------------------------------------------------------------
// Snappy
// ---------------------------------------------------------------------------

// Decompresses the Snappy raw format. Parquet applies Snappy to a page.
bool snappy_decompress(const uint8_t *input, size_t input_size,
                       std::string &output, size_t max_output)
{
  size_t offset = 0;
  uint64_t expected = 0;
  int shift = 0;
  while (offset < input_size) {
    const uint8_t byte = input[offset++];
    expected |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) { break; }
    shift += 7;
    if (shift > 63) { return false; }
  }
  if (expected > max_output) { return false; }
  output.clear();
  output.reserve(static_cast<size_t>(expected));
  while (offset < input_size) {
    const uint8_t tag = input[offset++];
    const uint8_t kind = tag & 0x03;
    if (kind == 0) {  // literal
      uint64_t length = tag >> 2;
      if (length >= 60) {
        const size_t extra = static_cast<size_t>(length) - 59;
        if (offset + extra > input_size) { return false; }
        uint64_t value = 0;
        for (size_t i = 0; i < extra; ++i) { value |= static_cast<uint64_t>(input[offset + i]) << (8 * i); }
        offset += extra;
        length = value;
      }
      const size_t count = static_cast<size_t>(length) + 1;
      if (offset + count > input_size) { return false; }
      if (output.size() + count > max_output) { return false; }
      output.append(reinterpret_cast<const char *>(input + offset), count);
      offset += count;
      continue;
    }
    uint64_t count = 0;
    size_t distance = 0;
    if (kind == 1) {
      if (offset >= input_size) { return false; }
      count = 4 + ((tag >> 2) & 0x07);
      distance = static_cast<size_t>((tag >> 5) << 8) | input[offset++];
    } else if (kind == 2) {
      if (offset + 2 > input_size) { return false; }
      count = (tag >> 2) + 1;
      distance = static_cast<size_t>(input[offset]) | (static_cast<size_t>(input[offset + 1]) << 8);
      offset += 2;
    } else {
      if (offset + 4 > input_size) { return false; }
      count = (tag >> 2) + 1;
      distance = static_cast<size_t>(input[offset])
                 | (static_cast<size_t>(input[offset + 1]) << 8)
                 | (static_cast<size_t>(input[offset + 2]) << 16)
                 | (static_cast<size_t>(input[offset + 3]) << 24);
      offset += 4;
    }
    if (distance == 0 || distance > output.size()) { return false; }
    if (output.size() + count > max_output) { return false; }
    size_t source = output.size() - distance;
    for (uint64_t i = 0; i < count; ++i) {
      output.push_back(output[source + i]);
    }
  }
  return output.size() == expected;
}

// ---------------------------------------------------------------------------
// RLE / bit-packing hybrid
// ---------------------------------------------------------------------------

// Decodes the RLE/bit-packing hybrid encoding. Parquet uses it for the
// definition levels and for the dictionary indices.
class RleDecoder
{
public:
  RleDecoder(const uint8_t *data, size_t size, int bit_width)
      : data_(data), size_(size), bit_width_(bit_width) {}

  // Decodes exactly count values. Returns false on a malformed stream.
  bool decode(size_t count, std::vector<int32_t> &values)
  {
    values.clear();
    values.reserve(count);
    while (values.size() < count) {
      if (offset_ >= size_) { return false; }
      uint64_t header = 0;
      int shift = 0;
      while (true) {
        if (offset_ >= size_) { return false; }
        const uint8_t byte = data_[offset_++];
        header |= static_cast<uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) { break; }
        shift += 7;
        if (shift > 63) { return false; }
      }
      if (header & 1) {
        // Bit-packed run: header >> 1 groups of eight values.
        const uint64_t groups = header >> 1;
        const size_t bytes = static_cast<size_t>(groups) * static_cast<size_t>(bit_width_);
        if (offset_ + bytes > size_) { return false; }
        for (uint64_t g = 0; g < groups; ++g) {
          // A group holds eight values of the bit width. They are packed
          // into a bit stream that spans the whole group, so the values are
          // taken from the stream and not from a single word.
          uint64_t accumulator = 0;
          int available = 0;
          size_t consumed = 0;
          for (int k = 0; k < 8; ++k) {
            while (available < bit_width_) {
              if (consumed >= static_cast<size_t>(bit_width_)) {
                return false;
              }
              accumulator |= static_cast<uint64_t>(data_[offset_ + consumed]) << available;
              ++consumed;
              available += 8;
            }
            const uint64_t mask = (bit_width_ >= 64) ? ~0ULL : ((1ULL << bit_width_) - 1);
            values.push_back(static_cast<int32_t>(accumulator & mask));
            accumulator >>= bit_width_;
            available -= bit_width_;
          }
          offset_ += static_cast<size_t>(bit_width_);
        }
      } else {
        // RLE run: header >> 1 repetitions of a single value.
        const uint64_t repetitions = header >> 1;
        const size_t bytes = static_cast<size_t>((bit_width_ + 7) / 8);
        if (offset_ + bytes > size_) { return false; }
        uint64_t value = 0;
        for (size_t b = 0; b < bytes; ++b) {
          value |= static_cast<uint64_t>(data_[offset_ + b]) << (8 * b);
        }
        offset_ += bytes;
        const uint64_t mask = (bit_width_ >= 64) ? ~0ULL : ((1ULL << bit_width_) - 1);
        for (uint64_t r = 0; r < repetitions; ++r) {
          values.push_back(static_cast<int32_t>(value & mask));
        }
      }
    }
    values.resize(count);
    return true;
  }

  // The number of bytes the decoder consumed.
  size_t consumed() const { return offset_; }

  // Decodes a level section. The levels of a page do not carry the bit width
  // byte, because the maximum level implies it.
  bool decode_levels(size_t count, std::vector<int32_t> &values)
  {
    return decode(count, values);
  }

private:
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t offset_ = 0;
  int bit_width_ = 0;
};

// The Parquet encodings that the reader understands.
enum ParquetEncoding
{
  ENCODING_PLAIN = 0,
  ENCODING_PLAIN_DICTIONARY = 2,   // the deprecated spelling of the dictionary
  ENCODING_RLE = 3,
  ENCODING_BIT_PACKED = 4,
  ENCODING_RLE_DICTIONARY = 8,
};

// The Parquet page types that the reader understands.
enum ParquetPageType
{
  PAGE_DATA = 0,
  PAGE_INDEX = 1,
  PAGE_DICTIONARY = 2,
  PAGE_DATA_V2 = 3,
};

// The data page v1 is the page of the first format. The reader treats it
// separately because it stores the level sections differently.
const int32_t PAGE_DATA_V1 = PAGE_DATA;

// ---------------------------------------------------------------------------
// The plain encoding
// ---------------------------------------------------------------------------

// Decodes the PLAIN encoding of a physical type into scalars.
FileStatus decode_plain(const uint8_t *data, size_t size,
                         const ParquetColumn &column, size_t count,
                         std::vector<ScalarValue> &values)
{
  values.clear();
  values.reserve(count);
  switch (column.type) {
    case ParquetType::BOOLEAN: {
      if (size < (count + 7) / 8) { return {FileCode::INVALID_DATA,0,0,0,"truncated boolean page"}; }
      for (size_t i = 0; i < count; ++i) {
        const uint8_t byte = data[i / 8];
        ScalarValue value;
        value.type = ScalarType::BOOLEAN;
        value.is_null = false;
        value.integer = (byte >> (i % 8)) & 1;
        values.push_back(std::move(value));
      }
      return {};
    }
    case ParquetType::INT32: {
      if (size < count * 4) { return {FileCode::INVALID_DATA,0,0,0,"truncated int32 page"}; }
      for (size_t i = 0; i < count; ++i) {
        int32_t raw = 0;
        std::memcpy(&raw, data + i * 4, 4);
        ScalarValue value;
        value.type = ScalarType::BIGINT;
        value.is_null = false;
        value.integer = raw;
        values.push_back(std::move(value));
      }
      return {};
    }
    case ParquetType::INT64: {
      if (size < count * 8) { return {FileCode::INVALID_DATA,0,0,0,"truncated int64 page"}; }
      for (size_t i = 0; i < count; ++i) {
        int64_t raw = 0;
        std::memcpy(&raw, data + i * 8, 8);
        ScalarValue value;
        value.type = ScalarType::BIGINT;
        value.is_null = false;
        value.integer = raw;
        values.push_back(std::move(value));
      }
      return {};
    }
    case ParquetType::FLOAT: {
      if (size < count * 4) { return {FileCode::INVALID_DATA,0,0,0,"truncated float page"}; }
      for (size_t i = 0; i < count; ++i) {
        float raw = 0;
        std::memcpy(&raw, data + i * 4, 4);
        ScalarValue value;
        value.type = ScalarType::DOUBLE;
        value.is_null = false;
        value.real = raw;
        values.push_back(std::move(value));
      }
      return {};
    }
    case ParquetType::DOUBLE: {
      if (size < count * 8) { return {FileCode::INVALID_DATA,0,0,0,"truncated double page"}; }
      for (size_t i = 0; i < count; ++i) {
        double raw = 0;
        std::memcpy(&raw, data + i * 8, 8);
        ScalarValue value;
        value.type = ScalarType::DOUBLE;
        value.is_null = false;
        value.real = raw;
        values.push_back(std::move(value));
      }
      return {};
    }
    case ParquetType::FIXED_LEN_BYTE_ARRAY: {
      const size_t width = static_cast<size_t>(column.type_length);
      if (width == 0 || size < count * width) {
        return {FileCode::INVALID_DATA,0,0,0,"truncated fixed length byte array page"};
      }
      for (size_t i = 0; i < count; ++i) {
        ScalarValue value;
        value.type = ScalarType::VARCHAR;
        value.is_null = false;
        value.text.assign(reinterpret_cast<const char *>(data + i * width), width);
        values.push_back(std::move(value));
      }
      return {};
    }
    case ParquetType::BYTE_ARRAY: {
      size_t offset = 0;
      for (size_t i = 0; i < count; ++i) {
        if (offset + 4 > size) { return {FileCode::INVALID_DATA,0,0,0,"truncated byte array page"}; }
        uint32_t length = 0;
        std::memcpy(&length, data + offset, 4);
        offset += 4;
        if (offset + length > size) { return {FileCode::INVALID_DATA,0,0,0,"truncated byte array page"}; }
        ScalarValue value;
        value.type = ScalarType::VARCHAR;
        value.is_null = false;
        value.text.assign(reinterpret_cast<const char *>(data + offset), length);
        offset += length;
        values.push_back(std::move(value));
      }
      return {};
    }
    case ParquetType::INT96:
      // The INT96 timestamp type is not exposed by this reader.
      return {FileCode::INVALID_DATA,0,0,0,"parquet INT96 columns are not supported"};
  }
  return {FileCode::INVALID_DATA,0,0,0,"unsupported parquet physical type"};
}

// ---------------------------------------------------------------------------
// Page headers
// ---------------------------------------------------------------------------

// Reads one page header and returns its type plus the offset and the size of
// the page payload.
FileStatus read_page_header(FILE *input, uint64_t offset, int32_t &page_type,
                             uint64_t &data_offset, uint64_t &data_size,
                             ThriftValue &header)
{
  if (fseeko(input, static_cast<off_t>(offset), SEEK_SET) != 0) {
    return {FileCode::IO_ERROR,0,0,0,"cannot seek to a parquet page header"};
  }
  // The header is small, but a generous bound keeps a corrupt file from
  // allocating without limit.
  uint8_t buffer[4096];
  const size_t read = fread(buffer, 1, sizeof(buffer), input);
  if (read == 0) { return {FileCode::IO_ERROR,0,0,0,"cannot read a parquet page header"}; }
  // The header length is not stored, so the payload offset is found by
  // decoding the header and using the reader position.
  ThriftReader reader(buffer, read);
  header = thrift_read_value(reader, static_cast<uint8_t>(ThriftType::STRUCT));
  if (!reader.ok()) {
    return {FileCode::INVALID_DATA,0,0,0,"cannot decode a parquet page header"};
  }
  page_type = static_cast<int32_t>(header.field_int(1, 0));
  const uint64_t compressed_size = static_cast<uint64_t>(header.field_int(3, 0));
  data_offset = offset + reader.offset();
  data_size = compressed_size;
  return {};
}

// The information a data page carries about its own contents.
struct DataPageInfo
{
  int64_t num_values = 0;
  int64_t encoding = ENCODING_PLAIN;
  // The number of bytes of the definition and repetition level sections. A
  // data page v2 states them in the header, a data page v1 states the
  // encoded length of the section.
  int64_t definition_bytes = 0;
  int64_t repetition_bytes = 0;
  int64_t num_nulls = 0;
  // Whether the values of the page are compressed. A data page v2 states it,
  // a data page v1 always compresses the whole page.
  bool is_compressed = true;
};

// Extracts the data page information of a page header.
bool parse_data_page_header(const ThriftValue &page_header, int32_t page_type,
                             DataPageInfo &info)
{
  if (page_type == PAGE_DATA_V2) {
    const ThriftValue *v2 = page_header.field(8);
    if (v2 == nullptr) { return false; }
    info.num_values = v2->field_int(1, 0);
    info.num_nulls = v2->field_int(2, 0);
    info.encoding = v2->field_int(4, ENCODING_PLAIN);
    info.definition_bytes = v2->field_int(5, 0);
    info.repetition_bytes = v2->field_int(6, 0);
    const ThriftValue *compressed = v2->field(7);
    info.is_compressed = (compressed != nullptr) ? compressed->boolean : true;
    return true;
  }
  if (page_type == PAGE_DATA) {
    const ThriftValue *v1 = page_header.field(5);
    if (v1 == nullptr) { return false; }
    info.num_values = v1->field_int(1, 0);
    info.encoding = v1->field_int(2, ENCODING_PLAIN);
    info.definition_bytes = v1->field_int(3, 0);
    info.repetition_bytes = v1->field_int(4, 0);
    return true;
  }
  return false;
}

// The information a dictionary page carries about its own contents.
struct DictionaryPageInfo
{
  int64_t num_values = 0;
  int64_t encoding = ENCODING_PLAIN;
};

// The maximum definition level of a column that carries a definition level
// section. A writer does not always mark a column as optional in the schema,
// so a schema without any optional level falls back to one.
int max_definition_level_of(const ParquetColumn &column)
{
  return column.max_definition_level > 0 ? column.max_definition_level : 1;
}

// The number of bits that a level of the given maximum needs.
int level_bit_width(int max_level)
{
  int width = 0;
  while ((1 << width) <= max_level) { ++width; }
  return width;
}

// Extracts the dictionary page information of a page header.
bool parse_dictionary_page_header(const ThriftValue &page_header, DictionaryPageInfo &info)
{
  const ThriftValue *v1 = page_header.field(7);
  if (v1 == nullptr) { return false; }
  info.num_values = v1->field_int(1, 0);
  info.encoding = v1->field_int(2, ENCODING_PLAIN);
  return true;
}

} // namespace

FileStatus ParquetReader::fail(FileCode code, size_t column, const char *message)
{
  failure_ = {code, 0, 0, column, message};
  return failure_;
}

FileStatus ParquetReader::init(FILE *input, const ParquetOptions &options)
{
  if (input == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"parquet input is null"}; }
  if (fstat(fileno(input), &snapshot_) != 0) {
    return {FileCode::IO_ERROR,0,0,0,"cannot stat parquet input"};
  }
  input_ = input;
  options_ = options;
  start_ = ftello(input);
  footer_read_ = false;
  failure_ = {};
  return read_footer();
}

FileStatus ParquetReader::read_footer()
{
  // The footer is a Thrift compact FileMetaData, preceded by its length and
  // followed by the trailing magic.
  uint8_t tail[8];
  if (fseeko(input_, -8, SEEK_END) != 0) {
    return fail(FileCode::IO_ERROR,0,"cannot seek to the end of the parquet file");
  }
  if (fread(tail, 1, 8, input_) != 8) {
    return fail(FileCode::IO_ERROR,0,"cannot read the parquet footer");
  }
  if (std::memcmp(tail + 4, "PAR1", 4) != 0) {
    return fail(FileCode::INVALID_DATA,0,"parquet file does not end with the magic bytes");
  }
  uint32_t footer_length = 0;
  std::memcpy(&footer_length, tail, 4);
  if (footer_length == 0 || footer_length > (1u << 30)) {
    return fail(FileCode::INVALID_DATA,0,"parquet footer length is invalid");
  }
  long footer_offset = ftello(input_) - 8 - static_cast<long>(footer_length);
  if (footer_offset < 0) {
    return fail(FileCode::INVALID_DATA,0,"parquet footer length exceeds the file size");
  }
  std::string footer;
  footer.resize(footer_length);
  if (fseeko(input_, footer_offset, SEEK_SET) != 0
      || fread(&footer[0], 1, footer_length, input_) != footer_length) {
    return fail(FileCode::IO_ERROR,0,"cannot read the parquet footer");
  }

  ThriftReader reader(reinterpret_cast<const uint8_t *>(footer.data()), footer.size());
  const ThriftValue metadata = thrift_read_value(reader, static_cast<uint8_t>(ThriftType::STRUCT));
  if (!reader.ok()) {
    return fail(FileCode::INVALID_DATA,0,"cannot decode the parquet footer");
  }
  const ThriftValue *schema = metadata.field(2);
  const ThriftValue *row_groups = metadata.field(4);
  if (schema == nullptr || schema->elements.empty() || row_groups == nullptr) {
    return fail(FileCode::INVALID_DATA,0,"parquet footer does not carry a schema");
  }
  if (row_groups->elements.size() > options_.max_row_groups) {
    return fail(FileCode::RESOURCE_LIMIT,0,"parquet file exceeds the configured row group limit");
  }

  // The first schema element is the root, its children are the leaves.
  // The schema is a depth first tree. The root holds the columns, the leaves
  // are the physical columns. The repetition level of a leaf is the number of
  // repeated ancestors, the definition level is the number of optional ones.
  parquet_columns_.clear();
  {
    // The repetition type of a schema element: 1 required, 2 optional,
    // 3 repeated.
    const int64_t repeated = 3;
    // The stack holds the number of repeated and the number of optional
    // ancestors of the element that is being read.
    std::vector<std::pair<int64_t, int64_t>> stack;
    for (size_t i = 1; i < schema->elements.size(); ++i) {
      const ThriftValue &element = schema->elements[i];
      const int64_t repetition_type = element.field_int(3, 1);
      const int64_t children = element.field_int(5, 0);
      int64_t repeated_count = 0;
      int64_t optional_count = 0;
      if (!stack.empty()) {
        repeated_count = stack.back().first;
        optional_count = stack.back().second;
      }
      if (repetition_type == repeated) { ++repeated_count; }
      else if (repetition_type == 2) { ++optional_count; }
      if (children > 0) {
        stack.emplace_back(repeated_count, optional_count);
        continue;
      }
      ParquetColumn column;
      const std::string *name = element.field_binary(4);
      if (name == nullptr) { continue; }
      column.name = *name;
      column.type = static_cast<ParquetType>(element.field_int(1, 6));
      column.type_length = static_cast<int>(element.field_int(2, 0));
      column.max_definition_level = static_cast<int>(optional_count);
      column.max_repetition_level = static_cast<int>(repeated_count);
      column.optional = column.max_definition_level > 0;
      parquet_columns_.push_back(std::move(column));
      if (!stack.empty()) { stack.pop_back(); }
    }
  }
  if (parquet_columns_.empty()) {
    return fail(FileCode::INVALID_DATA,0,"parquet schema does not contain any leaf column");
  }

  // The row groups carry the offsets of every column chunk.
  row_groups_.clear();
  for (const ThriftValue &group : row_groups->elements) {
    ParquetRowGroup row_group;
    row_group.num_rows = group.field_int(3, 0);
    const ThriftValue *columns = group.field(1);
    if (columns != nullptr) {
      for (const ThriftValue &column : columns->elements) {
        ParquetColumnChunk chunk;
        const ThriftValue *meta = column.field(3);
        if (meta != nullptr) {
          chunk.codec = meta->field_int(4, 0);
          chunk.num_values = meta->field_int(5, 0);
          chunk.total_compressed_size = meta->field_int(7, 0);
          chunk.data_page_offset = meta->field_int(9, 0);
          chunk.dictionary_page_offset = meta->field_int(11, 0);
          // The statistics carry the number of null values. A writer does not
          // always mark a column as optional in the schema, so the count is
          // the reliable signal that a column can hold null values.
          const ThriftValue *statistics = meta->field(12);
          if (statistics != nullptr) {
            chunk.has_statistics = true;
            chunk.null_count = statistics->field_int(3, 0);
          }
        }
        row_group.columns.push_back(chunk);
      }
    }
    row_groups_.push_back(std::move(row_group));
  }

  footer_read_ = true;
  return build_schema();
}

FileStatus ParquetReader::build_schema()
{
  columns_.clear();
  for (const auto &column : parquet_columns_) {
    ColumnSchema schema;
    schema.name = column.name;
    schema.nullable = column.optional;
    switch (column.type) {
      case ParquetType::BOOLEAN: schema.type = ScalarType::BOOLEAN; break;
      case ParquetType::INT32: schema.type = ScalarType::BIGINT; break;
      case ParquetType::INT64: schema.type = ScalarType::BIGINT; break;
      case ParquetType::FLOAT:
      case ParquetType::DOUBLE: schema.type = ScalarType::DOUBLE; break;
      case ParquetType::BYTE_ARRAY:
      case ParquetType::FIXED_LEN_BYTE_ARRAY:
      case ParquetType::INT96: schema.type = ScalarType::VARCHAR; break;
      default: schema.type = ScalarType::VARCHAR; break;
    }
    columns_.push_back(std::move(schema));
  }
  return {};
}

// Reads a page payload into a string.
FileStatus ParquetReader::read_payload(uint64_t offset, uint64_t size, std::string &payload)
{
  payload.clear();
  if (size == 0) { return {}; }
  payload.resize(static_cast<size_t>(size));
  if (fseeko(input_, static_cast<off_t>(offset), SEEK_SET) != 0) {
    return fail(FileCode::IO_ERROR,0,"cannot seek to a parquet page payload");
  }
  if (fread(&payload[0], 1, static_cast<size_t>(size), input_) != size) {
    return fail(FileCode::IO_ERROR,0,"cannot read a parquet page payload");
  }
  return {};
}

// Decompresses a page payload with the codec of the column.
FileStatus ParquetReader::decompress(int64_t codec, const std::string &payload, std::string &plain)
{
  if (codec == 0) {  // UNCOMPRESSED
    plain = payload;
    return {};
  }
  if (codec == 1) {  // SNAPPY
    if (!snappy_decompress(reinterpret_cast<const uint8_t *>(payload.data()),
                            payload.size(), plain, options_.max_batch_bytes)) {
      return fail(FileCode::INVALID_DATA,0,"cannot decompress the parquet page values");
    }
    return {};
  }
  return fail(FileCode::INVALID_DATA,0,"unsupported parquet compression codec");
}

// Reads the dictionary page of a column chunk, if the chunk has one.
FileStatus ParquetReader::read_dictionary_page(const ParquetColumnChunk &chunk,
                                                const ParquetColumn &column,
                                                std::vector<ScalarValue> &dictionary)
{
  dictionary.clear();
  if (chunk.dictionary_page_offset <= 0) { return {}; }

  int32_t page_type = 0;
  uint64_t data_offset = 0;
  uint64_t data_size = 0;
  ThriftValue page_header;
  const FileStatus header_status = read_page_header(input_, chunk.dictionary_page_offset,
                                                     page_type, data_offset, data_size, page_header);
  if (!header_status.ok()) { return header_status; }
  if (page_type != PAGE_DICTIONARY) {
    return fail(FileCode::INVALID_DATA,0,"the parquet dictionary offset does not point to a dictionary page");
  }
  DictionaryPageInfo info;
  if (!parse_dictionary_page_header(page_header, info)) {
    return fail(FileCode::INVALID_DATA,0,"cannot decode the parquet dictionary page header");
  }

  std::string payload;
  const FileStatus read_status = read_payload(data_offset, data_size, payload);
  if (!read_status.ok()) { return read_status; }
  std::string plain;
  const FileStatus decoded = decompress(chunk.codec, payload, plain);
  if (!decoded.ok()) { return decoded; }
  // The dictionary page holds the values of the dictionary. It uses the
  // plain encoding, or the deprecated plain dictionary spelling of it.
  if (info.encoding != ENCODING_PLAIN && info.encoding != ENCODING_PLAIN_DICTIONARY) {
    return fail(FileCode::INVALID_DATA,0,"unsupported parquet dictionary encoding");
  }
  return decode_plain(reinterpret_cast<const uint8_t *>(plain.data()), plain.size(),
                      column, static_cast<size_t>(info.num_values), dictionary);
}

// Decodes the values of a data page that uses the PLAIN encoding.
FileStatus ParquetReader::decode_plain_values(const ParquetColumn &column,
                                               const std::string &plain,
                                               int64_t num_values,
                                               std::vector<ScalarValue> &values)
{
  return decode_plain(reinterpret_cast<const uint8_t *>(plain.data()), plain.size(),
                      column, static_cast<size_t>(num_values), values);
}

// Decodes the values of a data page that uses the RLE encoding. The reader
// uses it for the boolean columns, whose values are stored with the RLE and
// the bit packing hybrid.
FileStatus ParquetReader::decode_rle_values(const ParquetColumn &column,
                                             const std::string &plain,
                                             int64_t num_values,
                                             int64_t present_values,
                                             std::vector<ScalarValue> &values)
{
  (void)column;
  (void)num_values;
  values.clear();
  values.reserve(static_cast<size_t>(present_values));
  const uint8_t *data = reinterpret_cast<const uint8_t *>(plain.data());
  const size_t size = plain.size();
  // The values of the RLE encoding are a byte stream, so they carry their
  // length in a four byte prefix.
  if (size < 5) { return fail(FileCode::INVALID_DATA,0,"truncated parquet rle page"); }
  uint32_t length = 0;
  std::memcpy(&length, data, 4);
  if (length > size - 4) { return fail(FileCode::INVALID_DATA,0,"truncated parquet rle page"); }
  const uint8_t *stream = data + 4;
  if (length < 1) { return fail(FileCode::INVALID_DATA,0,"truncated parquet rle page"); }
  // The bit width of the values of a boolean column is one. The stream does
  // not carry it, unlike the dictionary indices which do.
  const int bit_width = 1;
  std::vector<int32_t> raw;
  RleDecoder decoder(stream, length, bit_width);
  if (!decoder.decode(static_cast<size_t>(present_values), raw)) {
    return fail(FileCode::INVALID_DATA,0,"cannot decode the parquet rle values");
  }
  for (const int32_t item : raw) {
    ScalarValue value;
    value.type = ScalarType::BOOLEAN;
    value.is_null = false;
    value.integer = (item != 0) ? 1 : 0;
    values.push_back(std::move(value));
  }
  return {};
}

// Decodes the values of a data page that uses a dictionary encoding.
FileStatus ParquetReader::decode_dictionary_values(const ParquetColumn &column,
                                                   const std::string &plain,
                                                   int64_t num_values,
                                                   int64_t present_values,
                                                   int64_t encoding,
                                                   const std::vector<ScalarValue> &dictionary,
                                                   std::vector<ScalarValue> &values)
{
  (void)column;
  (void)num_values;
  values.clear();
  values.reserve(static_cast<size_t>(present_values));
  const uint8_t *data = reinterpret_cast<const uint8_t *>(plain.data());
  const size_t size = plain.size();

  // The RLE_DICTIONARY encoding stores the indices with the RLE and the bit
  // packing hybrid. The first byte carries the bit width. The deprecated
  // spelling of the encoding, which the writers of the first format use,
  // holds the same hybrid stream.
  std::vector<int32_t> indices;
  if (encoding == ENCODING_RLE_DICTIONARY || encoding == ENCODING_PLAIN_DICTIONARY) {
    if (size < 1) { return fail(FileCode::INVALID_DATA,0,"truncated parquet dictionary indices"); }
    const int bit_width = data[0];
    if (bit_width > 32) { return fail(FileCode::INVALID_DATA,0,"invalid parquet dictionary bit width"); }
    RleDecoder decoder(data + 1, size - 1, bit_width);
    if (!decoder.decode(static_cast<size_t>(present_values), indices)) {
      return fail(FileCode::INVALID_DATA,0,"cannot decode the parquet dictionary indices");
    }
  } else {
    // The plain encoding of the indices, one byte per index.
    if (size < static_cast<size_t>(present_values)) {
      return fail(FileCode::INVALID_DATA,0,"truncated parquet dictionary indices");
    }
    indices.reserve(static_cast<size_t>(present_values));
    for (int64_t i = 0; i < present_values; ++i) {
      indices.push_back(static_cast<int32_t>(data[i]));
    }
  }

  for (const int32_t index : indices) {
    if (index < 0 || static_cast<size_t>(index) >= dictionary.size()) {
      return fail(FileCode::INVALID_DATA,0,"the parquet dictionary index is out of range");
    }
    values.push_back(dictionary[static_cast<size_t>(index)]);
  }
  return {};
}

FileStatus ParquetReader::read_column_chunk(uint64_t group_index, size_t column_index)
{
  if (group_index >= row_groups_.size()) {
    return fail(FileCode::INVALID_DATA,column_index,"parquet row group is out of range");
  }
  const ParquetRowGroup &row_group = row_groups_[group_index];
  if (column_index >= row_group.columns.size()) {
    return fail(FileCode::INVALID_DATA,column_index,"parquet row group does not contain the column");
  }
  const ParquetColumnChunk &chunk = row_group.columns[column_index];
  const int64_t num_values = chunk.num_values;
  // The number of null values that the statistics of the chunk report.
  const int64_t null_count = chunk.null_count;
  const uint64_t data_page_offset = static_cast<uint64_t>(chunk.data_page_offset);

  const ParquetColumn &column = parquet_columns_[column_index];
  std::vector<ScalarValue> values;
  values.reserve(static_cast<size_t>(num_values));

  // A chunk with a dictionary reads the dictionary page first.
  std::vector<ScalarValue> dictionary;
  if (chunk.dictionary_page_offset > 0) {
    const FileStatus status = read_dictionary_page(chunk, column, dictionary);
    if (!status.ok()) { return status; }
  }

  uint64_t offset = data_page_offset;
  size_t produced = 0;
  while (produced < static_cast<size_t>(num_values)) {
    int32_t page_type = 0;
    uint64_t data_offset = 0;
    uint64_t data_size = 0;
    ThriftValue page_header;
    const FileStatus header_status = read_page_header(input_, offset, page_type, data_offset, data_size, page_header);
    if (!header_status.ok()) { return header_status; }
    if (page_type == PAGE_DICTIONARY) {
      // A dictionary page inside the chunk is skipped, the dictionary was
      // already read from its own offset.
      offset = data_offset + data_size;
      continue;
    }

    DataPageInfo info;
    if (!parse_data_page_header(page_header, page_type, info)) {
      return fail(FileCode::INVALID_DATA,column_index,"unsupported parquet page type");
    }

    std::string payload;
    const FileStatus read_status = read_payload(data_offset, data_size, payload);
    if (!read_status.ok()) { return read_status; }

    // A data page v1 compresses the whole page, so the page is decompressed
    // first and the level sections are read from the plain data. A data page
    // v2 stores the level sections uncompressed at the start of the payload
    // and compresses only the values behind them.
    std::string plain;
    if (page_type == PAGE_DATA_V1) {
      const FileStatus decoded = decompress(chunk.codec, payload, plain);
      if (!decoded.ok()) { return decoded; }
    } else {
      plain = payload;
    }

    // The level sections. A data page v2 stores the repetition levels and
    // then the definition levels, both without a prefix. A data page v1
    // prefixes the first section that is present with its length.
    // The presence of the level sections. The page header states their
    // length, so a section is present when its length is not zero. A data
    // page v1 states the length of the second section only, the first one
    // is prefixed with its length in the payload.
    const bool has_repetition = column.max_repetition_level > 0
                             && (page_type == PAGE_DATA_V1 || info.repetition_bytes > 0);
    const bool has_definition = info.definition_bytes > 0;
    // The length of a level section. A data page v2 states it in the page
    // header. A data page v1 prefixes the first section that is present with
    // its length, so the length is read from the payload.
    size_t repetition_length = has_repetition
                             ? static_cast<size_t>(info.repetition_bytes) : 0;
    size_t definition_length = has_definition
                             ? static_cast<size_t>(info.definition_bytes) : 0;

    size_t level_offset = 0;
    size_t definition_offset = 0;
    if (page_type == PAGE_DATA_V1) {
      if (has_repetition || has_definition) {
        // The first section that is present is prefixed with its length.
        if (level_offset + 4 > plain.size()) {
          return fail(FileCode::INVALID_DATA,column_index,"truncated parquet level section");
        }
        uint32_t length = 0;
        std::memcpy(&length, plain.data() + level_offset, 4);
        level_offset += 4;
        if (length > plain.size() - level_offset) {
          return fail(FileCode::INVALID_DATA,column_index,"truncated parquet level section");
        }
        if (has_repetition) {
          // The prefixed section is the repetition levels.
          repetition_length = length;
          level_offset += length;
          if (has_definition) {
            // The definition levels follow. Their length is the one of the
            // header, because the prefix only covers the first section.
            definition_offset = level_offset;
            level_offset += definition_length;
          }
        } else {
          // The prefixed section is the definition levels.
          definition_offset = level_offset;
          definition_length = length;
          level_offset += length;
        }
      }
    } else {
      if (has_repetition) {
        level_offset += repetition_length;
      }
      if (has_definition) {
        definition_offset = level_offset;
        level_offset += definition_length;
      }
    }
    if (level_offset > plain.size()) {
      return fail(FileCode::INVALID_DATA,column_index,"truncated parquet level section");
    }

    // The definition levels tell which values are present. They are RLE
    // encoded with a bit width that the maximum definition level implies.
    std::vector<int32_t> levels;
    if (has_definition) {
      // The bit width of the definition levels follows from the maximum
      // definition level of the column. A writer does not always mark a
      // column as optional in the schema, so a schema without any optional
      // level falls back to the width of one bit, which is the width of the
      // definition levels of a flat column.
      int bit_width = level_bit_width(column.max_definition_level);
      if (bit_width < 1) { bit_width = 1; }
      RleDecoder decoder(reinterpret_cast<const uint8_t *>(plain.data()) + definition_offset,
                         definition_length, bit_width);
      if (!decoder.decode_levels(static_cast<size_t>(info.num_values), levels)) {
        return fail(FileCode::INVALID_DATA,column_index,"cannot decode the parquet definition levels");
      }
    }

    // The values follow the level sections. A data page v1 has them in the
    // decompressed page, a data page v2 has them compressed behind them.
    std::string values_data;
    if (page_type == PAGE_DATA_V1) {
      values_data = plain.substr(level_offset);
    } else if (info.is_compressed) {
      // The values of a data page v2 are compressed behind the levels.
      const FileStatus decoded = decompress(chunk.codec, payload.substr(level_offset), values_data);
      if (!decoded.ok()) { return decoded; }
    } else {
      // The values are stored as they are.
      values_data = payload.substr(level_offset);
    }
    std::vector<ScalarValue> page_values;
    const bool is_dictionary = (info.encoding == ENCODING_RLE_DICTIONARY
                                || info.encoding == ENCODING_PLAIN_DICTIONARY);
    if (is_dictionary) {
      if (dictionary.empty()) {
        return fail(FileCode::INVALID_DATA,column_index,"the parquet page uses a dictionary that is missing");
      }
      const FileStatus decoded = decode_dictionary_values(column, values_data, info.num_values,
                                                          info.num_values - null_count,
                                                          info.encoding, dictionary, page_values);
      if (!decoded.ok()) { return decoded; }
    } else if (info.encoding == ENCODING_PLAIN) {
      const FileStatus decoded = decode_plain_values(column, values_data, info.num_values, page_values);
      if (!decoded.ok()) { return decoded; }
    } else if (info.encoding == ENCODING_RLE || info.encoding == ENCODING_BIT_PACKED) {
      // The encoding only holds the values that are not null.
      const int64_t present_values = info.num_values - null_count;
      const FileStatus decoded = decode_rle_values(column, values_data, info.num_values,
                                                   present_values, page_values);
      if (!decoded.ok()) { return decoded; }
    } else {
      return fail(FileCode::INVALID_DATA,column_index,"unsupported parquet encoding");
    }

    // The encodings that hold a byte stream only store the values that are
    // not null. The definition levels say which ones they are, so the values
    // are expanded back to the number of the rows of the page.
    if (!levels.empty() && page_values.size() < static_cast<size_t>(info.num_values)) {
      std::vector<ScalarValue> expanded;
      expanded.reserve(static_cast<size_t>(info.num_values));
      size_t present = 0;
      for (size_t i = 0; i < static_cast<size_t>(info.num_values); ++i) {
        if (levels[i] < max_definition_level_of(column)) {
          ScalarValue null_value;
          null_value.type = columns_[column_index].type;
          null_value.is_null = true;
          expanded.push_back(std::move(null_value));
        } else if (present < page_values.size()) {
          expanded.push_back(page_values[present++]);
        }
      }
      page_values = std::move(expanded);
    }

    // Apply the definition levels: a level below the maximum marks a null.
    // A writer does not always mark a column as optional in the schema, so
    // the number of null values that the statistics report decides whether
    // a level below the maximum is a null.
    const int max_definition_level = has_definition ? 1 : 0;
    const bool column_has_nulls = chunk.has_statistics && chunk.null_count > 0;

    for (size_t i = 0; i < page_values.size(); ++i) {
      if (column_has_nulls && !levels.empty() && levels[i] < max_definition_level) {
        ScalarValue null_value;
        null_value.type = columns_[column_index].type;
        null_value.is_null = true;
        page_values[i] = std::move(null_value);
      }
      values.push_back(std::move(page_values[i]));
    }
    produced += static_cast<size_t>(info.num_values);
    offset = data_offset + data_size;
  }

  group_values_[column_index] = std::move(values);
  return {};
}

FileStatus ParquetReader::read_row_group(uint64_t group_index)
{
  if (group_index >= row_groups_.size()) {
    return fail(FileCode::INVALID_DATA,0,"parquet row group is out of range");
  }
  const ParquetRowGroup &row_group = row_groups_[group_index];
  if (row_group.num_rows < 0 || static_cast<uint64_t>(row_group.num_rows) > options_.max_rows) {
    return fail(FileCode::RESOURCE_LIMIT,0,"parquet row group exceeds the configured row limit");
  }
  group_rows_ = static_cast<uint64_t>(row_group.num_rows);
  row_in_group_ = 0;

  // Only the projected columns are read, which is the column pruning.
  group_values_.assign(parquet_columns_.size(), std::vector<ScalarValue>());
  for (size_t i = 0; i < parquet_columns_.size(); ++i) {
    if (!is_projected(i)) { continue; }
    const FileStatus status = read_column_chunk(group_index, i);
    if (!status.ok()) { return status; }
    if (group_values_[i].size() != group_rows_) {
      return fail(FileCode::INVALID_DATA,i,"parquet column chunk does not match the row group size");
    }
  }
  group_index_ = group_index + 1;
  return {};
}

FileStatus ParquetReader::next_batch(size_t limit, std::vector<ScalarRow> &rows)
{
  rows.clear();
  if (!footer_read_) { return {FileCode::INVALID_ARGUMENT,0,0,0,"parquet reader is not initialized"}; }
  const FileStatus unchanged = check_unchanged();
  if (!unchanged.ok()) { return unchanged; }

  size_t bytes = 0;
  while (rows.size() < limit) {
    if (row_in_group_ >= group_rows_) {
      if (group_index_ >= row_groups_.size()) { break; }
      const FileStatus status = read_row_group(group_index_);
      if (!status.ok()) { return status; }
      if (group_rows_ == 0) { continue; }
    }
    ScalarRow row;
    row.reserve(parquet_columns_.size());
    for (size_t i = 0; i < parquet_columns_.size(); ++i) {
      if (!is_projected(i)) {
        ScalarValue empty;
        empty.type = columns_[i].type;
        empty.is_null = true;
        row.push_back(std::move(empty));
        continue;
      }
      row.push_back(group_values_[i][row_in_group_]);
    }
    bytes += row.size() * sizeof(ScalarValue);
    rows.push_back(std::move(row));
    ++row_in_group_;
    ++row_count_;
    if (bytes > options_.max_batch_bytes) { break; }
  }
  return {};
}

FileStatus ParquetReader::rescan()
{
  if (!footer_read_) { return {FileCode::INVALID_ARGUMENT,0,0,0,"parquet reader is not initialized"}; }
  group_index_ = 0;
  row_in_group_ = 0;
  group_rows_ = 0;
  row_count_ = 0;
  group_values_.clear();
  return {};
}

FileStatus ParquetReader::check_unchanged() const
{
  struct stat now {};
  if (input_ == nullptr || fstat(fileno(input_), &now) != 0) {
    return {FileCode::IO_ERROR,0,0,0,"cannot stat parquet input"};
  }
  if (now.st_ino != snapshot_.st_ino || now.st_dev != snapshot_.st_dev
      || now.st_size != snapshot_.st_size
      || now.st_mtime != snapshot_.st_mtime || now.st_ctime != snapshot_.st_ctime) {
    return {FileCode::FILE_CHANGED,0,0,0,"parquet input changed during the scan"};
  }
  return {};
}

bool ParquetReader::is_projected(size_t column_index) const
{
  if (options_.projection.empty()) { return true; }
  const std::string &name = parquet_columns_[column_index].name;
  for (const auto &wanted : options_.projection) {
    if (wanted == name) { return true; }
  }
  return false;
}

}}} // namespace oceanbase::sql::file_sql
