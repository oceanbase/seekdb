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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace oceanbase {
namespace sql {
namespace file_source {

/// Parquet physical types, see `parquet.thrift`.
enum class ParquetPhysicalType
{
  BOOLEAN = 0,
  INT32 = 1,
  INT64 = 2,
  INT96 = 3,
  FLOAT = 4,
  DOUBLE = 5,
  BYTE_ARRAY = 6,
  FIXED_LEN_BYTE_ARRAY = 7,
};

/// Parquet field repetition, see `parquet.thrift`.
enum class ParquetRepetition
{
  REQUIRED = 0,
  OPTIONAL = 1,
  REPEATED = 2,
};

/// Parquet compression codec, see `parquet.thrift`.
enum class ParquetCodec
{
  UNCOMPRESSED = 0,
  SNAPPY = 1,
  GZIP = 2,
  LZO = 3,
  BROTLI = 4,
  LZ4 = 5,
  ZSTD = 6,
  LZ4_RAW = 7,
};

/// Parquet page encoding, see `parquet.thrift`.
enum class ParquetEncoding
{
  PLAIN = 0,
  PLAIN_DICTIONARY = 2,
  RLE = 3,
  BIT_PACKED = 4,
  DELTA_BINARY_PACKED = 5,
  DELTA_LENGTH_BYTE_ARRAY = 6,
  DELTA_BYTE_ARRAY = 7,
  RLE_DICTIONARY = 8,
  BYTE_STREAM_SPLIT = 9,
};

/// One element of the parquet schema.
struct ParquetSchemaNode
{
  /// Physical type of the node, `-1` for group nodes.
  int32_t type = -1;
  int32_t type_length = 0;
  /// One of `ParquetRepetition`.
  int32_t repetition_type = 0;
  std::string name;
  int32_t num_children = 0;
  /// Legacy converted type, `-1` when the node does not carry one.
  int32_t converted_type = -1;
  /// Field id of the logical type union, `-1` when the node has no logical type.
  int32_t logical_type_kind = -1;
};

/// Description of a leaf column of the file.
struct ParquetColumnDesc
{
  std::string name;
  int32_t type = -1;
  int32_t type_length = 0;
  int32_t repetition_type = 0;
  int32_t max_def_level = 1;
  int32_t max_rep_level = 0;
  int32_t converted_type = -1;
  int32_t logical_type_kind = -1;
};

/// Description of one column chunk of a row group.
struct ParquetColumnChunk
{
  int64_t file_offset = 0;
  int64_t num_values = 0;
  int64_t total_compressed_size = 0;
  int64_t total_uncompressed_size = 0;
  int64_t data_page_offset = 0;
  /// `-1` when the chunk does not contain a dictionary page.
  int64_t dictionary_page_offset = -1;
  int32_t codec = 0;
  std::vector<int32_t> encodings;
  std::string path_in_schema;
};

/// Description of one row group.
struct ParquetRowGroup
{
  std::vector<ParquetColumnChunk> columns;
  int64_t num_rows = 0;
  int64_t total_byte_size = 0;
};

/// Reader for the thrift compact protocol.
///
/// Only the subset of the protocol that is required by the parquet metadata
/// and the page headers is implemented.
class ThriftCompactReader
{
public:
  ThriftCompactReader(const unsigned char *data, size_t size) : data_(data), size_(size) {}

public:
  bool read_uint32(uint32_t &value);
  bool read_int32(int32_t &value);
  bool read_int64(int64_t &value);
  bool read_bool(bool &value);
  bool read_binary(std::string &value);
  /// Read the header of the next field, returns false at the end of the struct.
  bool read_field_begin(int16_t &field_id, int8_t &type, bool &is_stop);
  /// Read the header of a list, the element type is returned in `elem_type`.
  bool read_list_begin(int8_t &elem_type, uint32_t &size);
  /// Skip the payload of a field of the given type.
  bool skip_field(int8_t type);
  /// Enter a nested struct, the field ids restart at zero.
  void push_struct();
  /// Leave the current nested struct.
  void pop_struct();

  bool eof() const { return pos_ >= size_; }
  size_t position() const { return pos_; }

private:
  bool read_varint(uint64_t &value);
  bool read_zigzag(int64_t &value);

private:
  const unsigned char *data_;
  size_t size_;
  size_t pos_ = 0;
  int16_t last_field_id_ = 0;
  /// The field id scopes of the enclosing structs.
  std::vector<int16_t> field_id_stack_;
};

/// Decompress a raw snappy block.
bool snappy_uncompress(const unsigned char *src, size_t src_size, std::vector<unsigned char> &dst);

/// Decode the RLE / bit-packing hybrid encoding.
///
/// `bit_width` is the number of bits per value, the values are appended to
/// `values`. Returns the number of consumed bytes.
bool decode_rle_hybrid(const unsigned char *data,
                       size_t data_size,
                       int32_t bit_width,
                       int64_t count,
                       std::vector<int32_t> &values,
                       size_t &consumed);

/// Decode plain encoded parquet values of the given physical type.
bool decode_plain_values(const unsigned char *data,
                         size_t data_size,
                         int32_t physical_type,
                         int32_t type_length,
                         int32_t converted_type,
                         int64_t count,
                         std::vector<std::string> &values);

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
