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
  void seek(size_t offset) { offset_ = offset; }

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

// Decodes the RLE/bit-packing hybrid encoding used for the definition levels.
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
          uint64_t packed = 0;
          for (int b = 0; b < bit_width_; ++b) {
            packed |= static_cast<uint64_t>(data_[offset_ + b]) << (8 * b);
          }
          offset_ += static_cast<size_t>(bit_width_);
          for (int k = 0; k < 8; ++k) {
            const uint64_t mask = (bit_width_ >= 64) ? ~0ULL : ((1ULL << bit_width_) - 1);
            values.push_back(static_cast<int32_t>((packed >> (k * bit_width_)) & mask));
          }
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

private:
  const uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t offset_ = 0;
  int bit_width_ = 0;
};

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
  start_ = ftello(input_);
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
  parquet_columns_.clear();
  for (size_t i = 1; i < schema->elements.size(); ++i) {
    const ThriftValue &element = schema->elements[i];
    ParquetColumn column;
    const std::string *name = element.field_binary(4);
    if (name == nullptr) { continue; }
    column.name = *name;
    column.type = static_cast<ParquetType>(element.field_int(1, 6));
    column.type_length = static_cast<int>(element.field_int(2, 0));
    column.max_definition_level = static_cast<int>(element.field_int(3, 0));
    column.optional = column.max_definition_level > 0;
    parquet_columns_.push_back(std::move(column));
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

// Reads one page header and returns its type plus the offset and the size of
// the page payload.
namespace {

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

} // namespace

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
  const int64_t codec = chunk.codec;
  const int64_t num_values = chunk.num_values;
  const uint64_t data_page_offset = static_cast<uint64_t>(chunk.data_page_offset);
  const uint64_t total_compressed = static_cast<uint64_t>(chunk.total_compressed_size);

  const ParquetColumn &column = parquet_columns_[column_index];
  std::vector<ScalarValue> values;
  values.reserve(static_cast<size_t>(num_values));

  uint64_t offset = static_cast<uint64_t>(data_page_offset);
  uint64_t remaining = static_cast<uint64_t>(total_compressed);
  size_t produced = 0;
  while (produced < static_cast<size_t>(num_values)) {
    int32_t page_type = 0;
    uint64_t data_offset = 0;
    uint64_t data_size = 0;
    ThriftValue page_header;
    const FileStatus header_status = read_page_header(input_, offset, page_type, data_offset, data_size, page_header);
    if (!header_status.ok()) { return header_status; }

    std::string payload;
    payload.resize(static_cast<size_t>(data_size));
    if (data_size > 0 && fseeko(input_, static_cast<off_t>(data_offset), SEEK_SET) != 0) {
      return fail(FileCode::IO_ERROR,column_index,"cannot seek to a parquet page payload");
    }
    if (data_size > 0 && fread(&payload[0], 1, static_cast<size_t>(data_size), input_) != data_size) {
      return fail(FileCode::IO_ERROR,column_index,"cannot read a parquet page payload");
    }

    // The definition levels of a data page v2 come before the values.
    int64_t page_num_values = 0;
    int64_t definition_bytes = 0;
    int64_t repetition_bytes = 0;
    if (page_type == 3) {
      const ThriftValue *v2 = page_header.field(8);
      if (v2 == nullptr) { return fail(FileCode::INVALID_DATA,column_index,"parquet v2 page header is missing"); }
      page_num_values = v2->field_int(1, 0);
      definition_bytes = v2->field_int(5, 0);
      repetition_bytes = v2->field_int(6, 0);
    } else if (page_type == 0) {
      const ThriftValue *v1 = page_header.field(5);
      if (v1 == nullptr) { return fail(FileCode::INVALID_DATA,column_index,"parquet page header is missing"); }
      page_num_values = v1->field_int(1, 0);
    } else {
      return fail(FileCode::INVALID_DATA,column_index,"unsupported parquet page type");
    }

    std::vector<int32_t> levels;
    std::vector<ScalarValue> page_values;
    if (page_type == 3) {
      // A v2 page stores the repetition levels, then the definition levels,
      // then the values. Only the definition levels are needed here.
      const uint8_t *bytes = reinterpret_cast<const uint8_t *>(payload.data());
      size_t consumed = 0;
      if (column.max_definition_level > 0) {
        const size_t repetition_size = static_cast<size_t>(repetition_bytes);
        const size_t definition_size = static_cast<size_t>(definition_bytes);
        if (repetition_size + definition_size > payload.size()) {
          return fail(FileCode::INVALID_DATA,column_index,"truncated parquet level section");
        }
        // The definition levels are RLE encoded with a bit width of one.
        RleDecoder decoder(bytes + repetition_size, definition_size, 1);
        if (!decoder.decode(static_cast<size_t>(page_num_values), levels)) {
          return fail(FileCode::INVALID_DATA,column_index,"cannot decode the parquet definition levels");
        }
        consumed = repetition_size + definition_size;
      }
      // The values of a v2 page are compressed with the column codec.
      std::string values_bytes(payload.begin() + static_cast<long>(consumed), payload.end());
      std::string plain;
      if (codec == 1) {  // SNAPPY
        if (!snappy_decompress(reinterpret_cast<const uint8_t *>(values_bytes.data()),
                               values_bytes.size(), plain, options_.max_batch_bytes)) {
          return fail(FileCode::INVALID_DATA,column_index,"cannot decompress the parquet page values");
        }
      } else if (codec == 0) {  // UNCOMPRESSED
        plain = std::move(values_bytes);
      } else {
        return fail(FileCode::INVALID_DATA,column_index,"unsupported parquet compression codec");
      }
      const FileStatus decoded = decode_plain(reinterpret_cast<const uint8_t *>(plain.data()),
                                              plain.size(), column,
                                              static_cast<size_t>(page_num_values), page_values);
      if (!decoded.ok()) { return decoded; }
    } else {
      // A data page v1 stores the levels with an explicit length prefix.
      return fail(FileCode::INVALID_DATA,column_index,"parquet data page v1 is not supported");
    }

    // Apply the definition levels: a level below the maximum marks a null.
    for (size_t i = 0; i < page_values.size(); ++i) {
      if (!levels.empty() && levels[i] < column.max_definition_level) {
        ScalarValue null_value;
        null_value.type = columns_[column_index].type;
        null_value.is_null = true;
        page_values[i] = std::move(null_value);
      }
      values.push_back(std::move(page_values[i]));
    }
    produced += static_cast<size_t>(page_num_values);
    offset = data_offset + data_size;
    remaining = (remaining > data_size) ? (remaining - data_size) : 0;
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

bool ParquetReader::is_projected(size_t column_index) const
{
  if (options_.projection.empty()) { return true; }
  const std::string &name = parquet_columns_[column_index].name;
  for (const auto &wanted : options_.projection) {
    if (wanted == name) { return true; }
  }
  return false;
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

}}} // namespace oceanbase::sql::file_sql
