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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_parquet_reader.h"
#include "common/timezone/ob_time_convert.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/page_arena.h"
#include "lib/file/ob_file.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace oceanbase
{
using namespace common;
namespace sql
{

// ==== BEGIN PARQUET CORE ====
/**
 * @file
 * The self contained part of the parquet reader.
 *
 * It implements the thrift compact protocol decoder, the parquet metadata
 * model, the parquet page decoder and the snappy decompressor. It does not
 * use any OceanBase type, so that it can be compiled and tested standalone.
 *
 * Every symbol of this part lives in the ob_parquet_core namespace, so that
 * it cannot collide with the symbols of the other members of the unity build.
 */

namespace ob_parquet_core
{

/* the physical types of the parquet format */
enum ObPqType
{
  OB_PQ_BOOLEAN = 0,
  OB_PQ_INT32 = 1,
  OB_PQ_INT64 = 2,
  OB_PQ_INT96 = 3,
  OB_PQ_FLOAT = 4,
  OB_PQ_DOUBLE = 5,
  OB_PQ_BYTE_ARRAY = 6,
  OB_PQ_FIXED_LEN_BYTE_ARRAY = 7
};

/* the repetition types of the parquet format */
enum ObPqRepetition
{
  OB_PQ_REQUIRED = 0,
  OB_PQ_OPTIONAL = 1,
  OB_PQ_REPEATED = 2
};

/* the compression codecs of the parquet format */
enum ObPqCodec
{
  OB_PQ_CODEC_UNCOMPRESSED = 0,
  OB_PQ_CODEC_SNAPPY = 1
};

/* the encodings of the parquet format */
enum ObPqEncoding
{
  OB_PQ_ENCODING_PLAIN = 0,
  OB_PQ_ENCODING_PLAIN_DICTIONARY = 1,
  OB_PQ_ENCODING_RLE = 2,
  OB_PQ_ENCODING_BIT_PACKED = 3,
  OB_PQ_ENCODING_RLE_DICTIONARY = 7
};

/* the page types of the parquet format */
enum ObPqPageType
{
  OB_PQ_PAGE_DATA = 0,
  OB_PQ_PAGE_INDEX = 1,
  OB_PQ_PAGE_DICTIONARY = 2,
  OB_PQ_PAGE_DATA_V2 = 3
};

/* the logical types of the parquet thrift schema that are supported */
enum ObPqLogicalKind
{
  OB_PQ_LOGICAL_KIND_NONE = 0,
  OB_PQ_LOGICAL_KIND_DATE = 1,
  OB_PQ_LOGICAL_KIND_TIMESTAMP = 2
};

/* the time units of a parquet timestamp */
enum ObPqTimeUnit
{
  OB_PQ_TIME_UNIT_NONE = 0,
  OB_PQ_TIME_UNIT_MILLIS = 1,
  OB_PQ_TIME_UNIT_MICROS = 2,
  OB_PQ_TIME_UNIT_NANOS = 3
};

/* the types of the thrift compact protocol */
enum ObPqThriftType
{
  OB_PQ_THRIFT_STOP = 0,
  OB_PQ_THRIFT_BOOL_TRUE = 1,
  OB_PQ_THRIFT_BOOL_FALSE = 2,
  OB_PQ_THRIFT_BYTE = 3,
  OB_PQ_THRIFT_I16 = 4,
  OB_PQ_THRIFT_I32 = 5,
  OB_PQ_THRIFT_I64 = 6,
  OB_PQ_THRIFT_DOUBLE = 7,
  OB_PQ_THRIFT_BINARY = 8,
  OB_PQ_THRIFT_LIST = 9,
  OB_PQ_THRIFT_SET = 10,
  OB_PQ_THRIFT_MAP = 11,
  OB_PQ_THRIFT_STRUCT = 12
};

/* the converted types of the parquet format that are supported */
enum ObPqConvertedType
{
  OB_PQ_CONVERTED_DATE = 6,
  OB_PQ_CONVERTED_TIMESTAMP_MILLIS = 9,
  OB_PQ_CONVERTED_TIMESTAMP_MICROS = 10
};

/**
 * @brief Record an error message.
 */
static bool ob_pq_fail(std::string &err, const char *message)
{
  err = message;
  return false;
}

/**
 * @brief Record an error message and return the error code.
 */
static int ob_pq_error(std::string &err, const char *message)
{
  err = message;
  return -1;
}

/**
 * @brief The decoder of the thrift compact protocol.
 *
 * The decoder is used to read the parquet file metadata and the page headers,
 * both of them are encoded with the thrift compact protocol.
 */
class ObPqThrift
{
public:
  ObPqThrift() : data_(NULL), size_(0), pos_(0), last_field_id_(0) {}
  ObPqThrift(const char *data, const int64_t size)
      : data_(data), size_(size), pos_(0), last_field_id_(0) {}
  void reset(const char *data, const int64_t size)
  {
    data_ = data;
    size_ = size;
    pos_ = 0;
    last_field_id_ = 0;
    field_id_stack_.clear();
  }
  int64_t pos() const { return pos_; }
  bool read_u8(uint8_t &value)
  {
    if (pos_ >= size_) {
      return false;
    }
    value = (uint8_t)data_[pos_++];
    return true;
  }
  bool read_varint(uint64_t &value)
  {
    uint64_t result = 0;
    int32_t shift = 0;
    while (true) {
      if (pos_ >= size_ || shift > 63) {
        return false;
      }
      const uint8_t byte = (uint8_t)data_[pos_++];
      result |= ((uint64_t)(byte & 0x7F)) << shift;
      if (0 == (byte & 0x80)) {
        break;
      }
      shift += 7;
    }
    value = result;
    return true;
  }
  bool read_zigzag(int64_t &value)
  {
    uint64_t raw = 0;
    if (!read_varint(raw)) {
      return false;
    }
    value = (int64_t)((raw >> 1) ^ (~(raw & 1) + 1));
    return true;
  }
  bool read_double(double &value)
  {
    if (pos_ + 8 > size_) {
      return false;
    }
    uint64_t raw = 0;
    for (int32_t i = 0; i < 8; i++) {
      raw |= ((uint64_t)(uint8_t)data_[pos_ + i]) << (8 * i);
    }
    pos_ += 8;
    memcpy(&value, &raw, sizeof(double));
    return true;
  }
  bool read_binary(const char *&data, int64_t &len)
  {
    uint64_t length = 0;
    if (!read_varint(length)) {
      return false;
    }
    if (pos_ + (int64_t)length > size_) {
      return false;
    }
    data = data_ + pos_;
    len = (int64_t)length;
    pos_ += (int64_t)length;
    return true;
  }
  bool read_string(std::string &value)
  {
    const char *data = NULL;
    int64_t len = 0;
    if (!read_binary(data, len)) {
      return false;
    }
    value.assign(data, (size_t)len);
    return true;
  }
  bool read_list_header(uint8_t &elem_type, int64_t &size)
  {
    uint8_t header = 0;
    if (!read_u8(header)) {
      return false;
    }
    elem_type = header & 0x0F;
    const uint8_t size_nibble = (header >> 4) & 0x0F;
    if (15 == size_nibble) {
      uint64_t length = 0;
      if (!read_varint(length)) {
        return false;
      }
      size = (int64_t)length;
    } else {
      size = size_nibble;
    }
    return true;
  }
  /**
   * @brief Read the header of the next field of a struct.
   *
   * @param type     the type of the field, 0 marks the end of the struct
   * @param field_id the id of the field
   */
  bool read_field_header(uint8_t &type, int32_t &field_id)
  {
    uint8_t header = 0;
    if (!read_u8(header)) {
      return false;
    }
    if (OB_PQ_THRIFT_STOP == header) {
      type = OB_PQ_THRIFT_STOP;
      field_id = 0;
      return true;
    }
    type = header & 0x0F;
    const uint8_t delta = (header >> 4) & 0x0F;
    if (0 == delta) {
      int64_t id = 0;
      if (!read_zigzag(id)) {
        return false;
      }
      field_id = (int32_t)id;
    } else {
      field_id = last_field_id_ + (int32_t)delta;
    }
    last_field_id_ = field_id;
    return true;
  }
  void begin_struct()
  {
    field_id_stack_.push_back(last_field_id_);
    last_field_id_ = 0;
  }
  void end_struct()
  {
    if (!field_id_stack_.empty()) {
      last_field_id_ = field_id_stack_.back();
      field_id_stack_.pop_back();
    }
  }
  bool skip(const uint8_t type);
  bool skip_bytes(const int64_t len)
  {
    if (pos_ + len > size_) {
      return false;
    }
    pos_ += len;
    return true;
  }

private:
  const char *data_;
  int64_t size_;
  int64_t pos_;
  int32_t last_field_id_;
  std::vector<int32_t> field_id_stack_;
};

bool ObPqThrift::skip(const uint8_t type)
{
  switch (type) {
    case OB_PQ_THRIFT_BOOL_TRUE:
    case OB_PQ_THRIFT_BOOL_FALSE: {
      return true;
    }
    case OB_PQ_THRIFT_BYTE: {
      uint8_t value = 0;
      return read_u8(value);
    }
    case OB_PQ_THRIFT_I16:
    case OB_PQ_THRIFT_I32:
    case OB_PQ_THRIFT_I64: {
      uint64_t value = 0;
      return read_varint(value);
    }
    case OB_PQ_THRIFT_DOUBLE: {
      double value = 0.0;
      return read_double(value);
    }
    case OB_PQ_THRIFT_BINARY: {
      const char *data = NULL;
      int64_t len = 0;
      return read_binary(data, len);
    }
    case OB_PQ_THRIFT_LIST:
    case OB_PQ_THRIFT_SET: {
      uint8_t elem_type = 0;
      int64_t size = 0;
      if (!read_list_header(elem_type, size)) {
        return false;
      }
      for (int64_t i = 0; i < size; i++) {
        if (!skip(elem_type)) {
          return false;
        }
      }
      return true;
    }
    case OB_PQ_THRIFT_MAP: {
      uint64_t size = 0;
      if (!read_varint(size)) {
        return false;
      }
      if (0 == size) {
        return true;
      }
      uint8_t types = 0;
      if (!read_u8(types)) {
        return false;
      }
      const uint8_t key_type = (types >> 4) & 0x0F;
      const uint8_t value_type = types & 0x0F;
      for (uint64_t i = 0; i < size; i++) {
        if (!skip(key_type) || !skip(value_type)) {
          return false;
        }
      }
      return true;
    }
    case OB_PQ_THRIFT_STRUCT: {
      begin_struct();
      while (true) {
        uint8_t field_type = 0;
        int32_t field_id = 0;
        if (!read_field_header(field_type, field_id)) {
          end_struct();
          return false;
        }
        if (OB_PQ_THRIFT_STOP == field_type) {
          break;
        }
        if (!skip(field_type)) {
          end_struct();
          return false;
        }
      }
      end_struct();
      return true;
    }
    default: {
      return false;
    }
  }
}

/**
 * @brief One element of the parquet schema.
 */
struct ObPqSchemaElement
{
  int32_t type_;
  int32_t type_length_;
  int32_t repetition_type_;
  int32_t num_children_;
  int32_t converted_type_;
  int32_t logical_kind_;
  int32_t time_unit_;
  bool is_utc_;
  std::string name_;
  ObPqSchemaElement()
      : type_(-1),
        type_length_(0),
        repetition_type_(OB_PQ_REQUIRED),
        num_children_(0),
        converted_type_(-1),
        logical_kind_(OB_PQ_LOGICAL_KIND_NONE),
        time_unit_(OB_PQ_TIME_UNIT_NONE),
        is_utc_(false) {}
};

/**
 * @brief The metadata of one column chunk.
 */
struct ObPqColumnMeta
{
  int32_t type_;
  int32_t codec_;
  int64_t num_values_;
  int64_t total_compressed_size_;
  int64_t total_uncompressed_size_;
  int64_t data_page_offset_;
  int64_t dictionary_page_offset_;
  std::vector<std::string> path_in_schema_;
  ObPqColumnMeta()
      : type_(-1),
        codec_(OB_PQ_CODEC_UNCOMPRESSED),
        num_values_(0),
        total_compressed_size_(0),
        total_uncompressed_size_(0),
        data_page_offset_(-1),
        dictionary_page_offset_(-1) {}
};

/**
 * @brief One column chunk of a row group.
 */
struct ObPqColumnChunk
{
  int64_t file_offset_;
  int64_t num_values_;
  bool has_meta_;
  ObPqColumnMeta meta_;
  ObPqColumnChunk() : file_offset_(-1), num_values_(0), has_meta_(false) {}
};

/**
 * @brief One row group of the parquet file.
 */
struct ObPqRowGroup
{
  int64_t num_rows_;
  std::vector<ObPqColumnChunk> columns_;
  ObPqRowGroup() : num_rows_(0) {}
};

/**
 * @brief The metadata of the parquet file, i.e. the content of the footer.
 */
struct ObPqFileMeta
{
  int32_t version_;
  int64_t num_rows_;
  std::string created_by_;
  std::vector<ObPqSchemaElement> schema_;
  std::vector<ObPqRowGroup> row_groups_;
  ObPqFileMeta() : version_(0), num_rows_(0) {}
};

/**
 * @brief One leaf column of the parquet schema.
 */
struct ObPqLeaf
{
  std::string name_;
  int32_t type_;
  int32_t type_length_;
  int32_t max_def_level_;
  bool optional_;
  int32_t logical_kind_;
  int32_t time_unit_;
  bool is_utc_;
  ObPqLeaf()
      : type_(-1),
        type_length_(0),
        max_def_level_(0),
        optional_(false),
        logical_kind_(OB_PQ_LOGICAL_KIND_NONE),
        time_unit_(OB_PQ_TIME_UNIT_NONE),
        is_utc_(false) {}
};

/**
 * @brief The header of one parquet page.
 */
struct ObPqPageHeader
{
  int32_t type_;
  int32_t uncompressed_page_size_;
  int32_t compressed_page_size_;
  /* the data page v1 */
  int32_t num_values_;
  int32_t encoding_;
  /* the data page v2 */
  int32_t num_nulls_;
  int32_t num_rows_;
  int32_t def_levels_byte_length_;
  int32_t rep_levels_byte_length_;
  bool is_compressed_;
  /* the dictionary page */
  int32_t dict_num_values_;
  int32_t dict_encoding_;
  ObPqPageHeader()
      : type_(-1),
        uncompressed_page_size_(0),
        compressed_page_size_(0),
        num_values_(0),
        encoding_(OB_PQ_ENCODING_PLAIN),
        num_nulls_(0),
        num_rows_(0),
        def_levels_byte_length_(0),
        rep_levels_byte_length_(0),
        is_compressed_(false),
        dict_num_values_(0),
        dict_encoding_(OB_PQ_ENCODING_PLAIN) {}
};

/**
 * @brief One value of a parquet page.
 *
 * A byte array value is stored as an offset and a length into the buffer of
 * the page, a dictionary encoded value is stored as the index of the value in
 * the dictionary of the column chunk.
 */
struct ObPqValue
{
  int64_t int_value_;
  double double_value_;
  bool bool_value_;
  int32_t dict_index_;
  int64_t bin_offset_;
  int32_t bin_len_;
  ObPqValue()
      : int_value_(0),
        double_value_(0.0),
        bool_value_(false),
        dict_index_(-1),
        bin_offset_(0),
        bin_len_(0) {}
};

/**
 * @brief One value of the dictionary of a column chunk.
 */
struct ObPqDictValue
{
  int64_t int_value_;
  double double_value_;
  bool bool_value_;
  int64_t bin_offset_;
  int32_t bin_len_;
  ObPqDictValue()
      : int_value_(0), double_value_(0.0), bool_value_(false), bin_offset_(0), bin_len_(0) {}
};

/**
 * @brief One value produced by the reader, the values of a row.
 */
struct ObPqResolvedValue
{
  int32_t type_;
  int32_t logical_kind_;
  int32_t time_unit_;
  bool is_null_;
  int64_t int_value_;
  double double_value_;
  bool bool_value_;
  const char *bin_data_;
  int32_t bin_len_;
  ObPqResolvedValue()
      : type_(-1),
        logical_kind_(OB_PQ_LOGICAL_KIND_NONE),
        time_unit_(OB_PQ_TIME_UNIT_NONE),
        is_null_(true),
        int_value_(0),
        double_value_(0.0),
        bool_value_(false),
        bin_data_(NULL),
        bin_len_(0) {}
};

/**
 * @brief The source of the bytes of a parquet file.
 */
class ObPqFileSource
{
public:
  virtual ~ObPqFileSource() {}
  /**
   * @brief Read the bytes of the file.
   *
   * @param offset the offset of the first byte to read
   * @param size   the number of the bytes to read
   * @param buf    the buffer that receives the bytes
   * @return the number of the bytes read, -1 on error
   */
  virtual int64_t read(const int64_t offset, const int64_t size, char *buf) = 0;
};

/**
 * @brief Read the bits of a bit packed buffer.
 *
 * The bits are packed from the least significant bit to the most significant
 * one, the bytes are stored in the little endian order.
 */
static uint32_t ob_pq_read_bits(const char *buf,
                                 const int64_t len,
                                 const int64_t bit_offset,
                                 const int32_t bit_width)
{
  if (bit_width <= 0 || bit_offset < 0) {
    return 0;
  }
  const int64_t byte_index = bit_offset / 8;
  const int32_t bit_index = (int32_t)(bit_offset % 8);
  const int64_t need = (bit_index + bit_width + 7) / 8;
  uint64_t raw = 0;
  for (int64_t i = 0; i < need && byte_index + i < len; i++) {
    raw |= ((uint64_t)(uint8_t)buf[byte_index + i]) << (8 * i);
  }
  raw >>= bit_index;
  if (bit_width >= 64) {
    return (uint32_t)raw;
  }
  return (uint32_t)(raw & (((uint64_t)1 << bit_width) - 1));
}

/**
 * @brief The number of the bits that are needed to hold the given values.
 */
static int32_t ob_pq_level_bit_width(const int32_t max_level)
{
  int32_t width = 0;
  while ((1 << width) <= max_level) {
    width++;
  }
  return width;
}

/**
 * @brief Decode a RLE and bit packing hybrid encoded buffer.
 *
 * The buffer is a sequence of the runs, a run is either a repeated value or a
 * group of eight bit packed values. The levels of a page and the indexes of
 * a dictionary are encoded with this format.
 *
 * @param buf        the buffer to decode
 * @param len        the length of the buffer
 * @param bit_width  the number of the bits of a value
 * @param max_count  the number of the values to decode
 * @param out        the decoded values
 * @param consumed   the number of the bytes of the buffer that were read
 */
static bool ob_pq_decode_rle_hybrid(const char *buf,
                                    const int64_t len,
                                    const int32_t bit_width,
                                    const int64_t max_count,
                                    std::vector<uint32_t> &out,
                                    int64_t &consumed,
                                    std::string &err)
{
  consumed = 0;
  int64_t pos = 0;
  while ((int64_t)out.size() < max_count) {
    uint64_t header = 0;
    int32_t shift = 0;
    while (true) {
      if (pos >= len || shift > 63) {
        return ob_pq_fail(err, "malformed rle encoded data");
      }
      const uint8_t byte = (uint8_t)buf[pos++];
      header |= ((uint64_t)(byte & 0x7F)) << shift;
      if (0 == (byte & 0x80)) {
        break;
      }
      shift += 7;
    }
    if (0 != (header & 1)) {
      /* a bit packed run, it holds eight values per group */
      const int64_t groups = (int64_t)(header >> 1);
      const int64_t bytes = groups * bit_width;
      if (pos + bytes > len) {
        return ob_pq_fail(err, "malformed rle encoded data");
      }
      const int64_t values = groups * 8;
      for (int64_t i = 0; i < values && (int64_t)out.size() < max_count; i++) {
        out.push_back(ob_pq_read_bits(buf + pos, bytes, i * bit_width, bit_width));
      }
      pos += bytes;
    } else {
      /* a repeated value */
      const int64_t run = (int64_t)(header >> 1);
      const int64_t byte_len = (bit_width + 7) / 8;
      if (pos + byte_len > len) {
        return ob_pq_fail(err, "malformed rle encoded data");
      }
      uint32_t value = 0;
      for (int64_t i = 0; i < byte_len; i++) {
        value |= ((uint32_t)(uint8_t)buf[pos + i]) << (8 * i);
      }
      for (int64_t i = 0; i < run && (int64_t)out.size() < max_count; i++) {
        out.push_back(value);
      }
      pos += byte_len;
    }
  }
  consumed = pos;
  return true;
}

/**
 * @brief Decompress a snappy compressed block.
 *
 * The snappy format is a sequence of the literals and the copies, a copy may
 * overlap with the data that it produces.
 */
static bool ob_pq_snappy_decompress(const char *in,
                                    const int64_t in_len,
                                    std::vector<char> &out,
                                    std::string &err)
{
  int64_t pos = 0;
  uint64_t uncompressed_len = 0;
  int32_t shift = 0;
  while (true) {
    if (pos >= in_len || shift > 63) {
      return ob_pq_fail(err, "malformed snappy compressed data");
    }
    const uint8_t byte = (uint8_t)in[pos++];
    uncompressed_len |= ((uint64_t)(byte & 0x7F)) << shift;
    if (0 == (byte & 0x80)) {
      break;
    }
    shift += 7;
  }
  out.clear();
  while (pos < in_len) {
    const uint8_t tag = (uint8_t)in[pos++];
    const uint32_t kind = tag & 0x03;
    if (0 == kind) {
      /* a literal */
      const uint32_t size = tag >> 2;
      uint64_t length = 0;
      if (size < 60) {
        length = size + 1;
      } else {
        const int32_t extra = (int32_t)size - 59;
        if (pos + extra > in_len) {
          return ob_pq_fail(err, "malformed snappy compressed data");
        }
        for (int32_t i = 0; i < extra; i++) {
          length |= ((uint64_t)(uint8_t)in[pos + i]) << (8 * i);
        }
        pos += extra;
        length += 1;
      }
      if (pos + (int64_t)length > in_len) {
        return ob_pq_fail(err, "malformed snappy compressed data");
      }
      out.insert(out.end(), in + pos, in + pos + length);
      pos += length;
    } else {
      /* a copy */
      uint64_t length = 0;
      uint64_t offset = 0;
      if (1 == kind) {
        length = 4 + ((tag >> 2) & 0x07);
        if (pos + 1 > in_len) {
          return ob_pq_fail(err, "malformed snappy compressed data");
        }
        offset = (((uint64_t)(tag >> 5)) << 8) | (uint64_t)(uint8_t)in[pos];
        pos += 1;
      } else if (2 == kind) {
        length = (tag >> 2) + 1;
        if (pos + 2 > in_len) {
          return ob_pq_fail(err, "malformed snappy compressed data");
        }
        offset = (uint64_t)(uint8_t)in[pos] | (((uint64_t)(uint8_t)in[pos + 1]) << 8);
        pos += 2;
      } else {
        length = (tag >> 2) + 1;
        if (pos + 4 > in_len) {
          return ob_pq_fail(err, "malformed snappy compressed data");
        }
        offset = (uint64_t)(uint8_t)in[pos] | (((uint64_t)(uint8_t)in[pos + 1]) << 8)
                 | (((uint64_t)(uint8_t)in[pos + 2]) << 16) | (((uint64_t)(uint8_t)in[pos + 3]) << 24);
        pos += 4;
      }
      if (0 == offset || offset > (uint64_t)out.size()) {
        return ob_pq_fail(err, "the snappy compressed data has an invalid copy offset");
      }
      const size_t start = out.size() - (size_t)offset;
      for (uint64_t i = 0; i < length; i++) {
        out.push_back(out[start + i]);
      }
    }
  }
  if ((uint64_t)out.size() != uncompressed_len) {
    return ob_pq_fail(err, "the snappy compressed data has an invalid length");
  }
  return true;
}

/**
 * @brief Parse the logical type of a schema element.
 */
static bool ob_pq_parse_logical_type(ObPqThrift &thrift,
                                      ObPqSchemaElement &element,
                                      std::string &err)
{
  thrift.begin_struct();
  uint8_t type = 0;
  int32_t field_id = 0;
  if (!thrift.read_field_header(type, field_id)) {
    thrift.end_struct();
    return ob_pq_fail(err, "malformed parquet schema element");
  }
  if (OB_PQ_THRIFT_STOP != type) {
    if (6 == field_id) {
      /* DATE */
      element.logical_kind_ = OB_PQ_LOGICAL_KIND_DATE;
      if (!thrift.skip(type)) {
        thrift.end_struct();
        return ob_pq_fail(err, "malformed parquet schema element");
      }
    } else if (8 == field_id) {
      /* TIMESTAMP */
      element.logical_kind_ = OB_PQ_LOGICAL_KIND_TIMESTAMP;
      thrift.begin_struct();
      while (true) {
        uint8_t sub_type = 0;
        int32_t sub_field_id = 0;
        if (!thrift.read_field_header(sub_type, sub_field_id)) {
          thrift.end_struct();
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        if (OB_PQ_THRIFT_STOP == sub_type) {
          break;
        }
        if (1 == sub_field_id) {
          /* isAdjustedToUTC */
          if (OB_PQ_THRIFT_BOOL_TRUE == sub_type) {
            element.is_utc_ = true;
          } else if (OB_PQ_THRIFT_BOOL_FALSE == sub_type) {
            element.is_utc_ = false;
          } else if (!thrift.skip(sub_type)) {
            thrift.end_struct();
            thrift.end_struct();
            return ob_pq_fail(err, "malformed parquet schema element");
          }
        } else if (2 == sub_field_id) {
          /* the time unit */
          thrift.begin_struct();
          uint8_t unit_type = 0;
          int32_t unit_field_id = 0;
          if (!thrift.read_field_header(unit_type, unit_field_id)) {
            thrift.end_struct();
            thrift.end_struct();
            thrift.end_struct();
            return ob_pq_fail(err, "malformed parquet schema element");
          }
          if (OB_PQ_THRIFT_STOP != unit_type) {
            if (1 == unit_field_id) {
              element.time_unit_ = OB_PQ_TIME_UNIT_MILLIS;
            } else if (2 == unit_field_id) {
              element.time_unit_ = OB_PQ_TIME_UNIT_MICROS;
            } else if (3 == unit_field_id) {
              element.time_unit_ = OB_PQ_TIME_UNIT_NANOS;
            }
            if (!thrift.skip(unit_type)) {
              thrift.end_struct();
              thrift.end_struct();
              thrift.end_struct();
              return ob_pq_fail(err, "malformed parquet schema element");
            }
          }
          thrift.end_struct();
        } else if (!thrift.skip(sub_type)) {
          thrift.end_struct();
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
      }
      thrift.end_struct();
    } else if (!thrift.skip(type)) {
      thrift.end_struct();
      return ob_pq_fail(err, "malformed parquet schema element");
    }
  }
  thrift.end_struct();
  return true;
}

/**
 * @brief Parse one element of the parquet schema.
 */
static bool ob_pq_parse_schema_element(ObPqThrift &thrift,
                                        ObPqSchemaElement &element,
                                        std::string &err)
{
  thrift.begin_struct();
  while (true) {
    uint8_t type = 0;
    int32_t field_id = 0;
    if (!thrift.read_field_header(type, field_id)) {
      thrift.end_struct();
      return ob_pq_fail(err, "malformed parquet schema element");
    }
    if (OB_PQ_THRIFT_STOP == type) {
      break;
    }
    int64_t number = 0;
    switch (field_id) {
      case 1: { /* type */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        element.type_ = (int32_t)number;
        break;
      }
      case 2: { /* type_length */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        element.type_length_ = (int32_t)number;
        break;
      }
      case 3: { /* repetition_type */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        element.repetition_type_ = (int32_t)number;
        break;
      }
      case 4: { /* name */
        if (!thrift.read_string(element.name_)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        break;
      }
      case 5: { /* num_children */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        element.num_children_ = (int32_t)number;
        break;
      }
      case 6: { /* converted_type */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        element.converted_type_ = (int32_t)number;
        break;
      }
      case 10: { /* logicalType */
        if (!ob_pq_parse_logical_type(thrift, element, err)) {
          thrift.end_struct();
          return false;
        }
        break;
      }
      default: {
        if (!thrift.skip(type)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet schema element");
        }
        break;
      }
    }
  }
  thrift.end_struct();
  return true;
}

/**
 * @brief Parse the metadata of one column chunk.
 */
static bool ob_pq_parse_column_meta(ObPqThrift &thrift, ObPqColumnMeta &meta, std::string &err)
{
  thrift.begin_struct();
  while (true) {
    uint8_t type = 0;
    int32_t field_id = 0;
    if (!thrift.read_field_header(type, field_id)) {
      thrift.end_struct();
      return ob_pq_fail(err, "malformed parquet column metadata");
    }
    if (OB_PQ_THRIFT_STOP == type) {
      break;
    }
    int64_t number = 0;
    switch (field_id) {
      case 1: { /* type */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        meta.type_ = (int32_t)number;
        break;
      }
      case 3: { /* path_in_schema */
        uint8_t elem_type = 0;
        int64_t size = 0;
        if (!thrift.read_list_header(elem_type, size)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        for (int64_t i = 0; i < size; i++) {
          std::string name;
          if (!thrift.read_string(name)) {
            thrift.end_struct();
            return ob_pq_fail(err, "malformed parquet column metadata");
          }
          meta.path_in_schema_.push_back(name);
        }
        break;
      }
      case 4: { /* codec */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        meta.codec_ = (int32_t)number;
        break;
      }
      case 5: { /* num_values */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        meta.num_values_ = number;
        break;
      }
      case 6: { /* total_uncompressed_size */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        meta.total_uncompressed_size_ = number;
        break;
      }
      case 7: { /* total_compressed_size */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        meta.total_compressed_size_ = number;
        break;
      }
      case 9: { /* data_page_offset */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        meta.data_page_offset_ = number;
        break;
      }
      case 11: { /* dictionary_page_offset */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        meta.dictionary_page_offset_ = number;
        break;
      }
      default: {
        if (!thrift.skip(type)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column metadata");
        }
        break;
      }
    }
  }
  thrift.end_struct();
  return true;
}

/**
 * @brief Parse one column chunk of a row group.
 */
static bool ob_pq_parse_column_chunk(ObPqThrift &thrift, ObPqColumnChunk &chunk, std::string &err)
{
  thrift.begin_struct();
  while (true) {
    uint8_t type = 0;
    int32_t field_id = 0;
    if (!thrift.read_field_header(type, field_id)) {
      thrift.end_struct();
      return ob_pq_fail(err, "malformed parquet column chunk");
    }
    if (OB_PQ_THRIFT_STOP == type) {
      break;
    }
    int64_t number = 0;
    switch (field_id) {
      case 2: { /* file_offset */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column chunk");
        }
        chunk.file_offset_ = number;
        break;
      }
      case 3: { /* meta_data */
        if (!ob_pq_parse_column_meta(thrift, chunk.meta_, err)) {
          thrift.end_struct();
          return false;
        }
        chunk.has_meta_ = true;
        break;
      }
      case 5: { /* num_values */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column chunk");
        }
        chunk.num_values_ = number;
        break;
      }
      default: {
        if (!thrift.skip(type)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet column chunk");
        }
        break;
      }
    }
  }
  thrift.end_struct();
  return true;
}

/**
 * @brief Parse one row group of the parquet file.
 */
static bool ob_pq_parse_row_group(ObPqThrift &thrift, ObPqRowGroup &group, std::string &err)
{
  thrift.begin_struct();
  while (true) {
    uint8_t type = 0;
    int32_t field_id = 0;
    if (!thrift.read_field_header(type, field_id)) {
      thrift.end_struct();
      return ob_pq_fail(err, "malformed parquet row group");
    }
    if (OB_PQ_THRIFT_STOP == type) {
      break;
    }
    int64_t number = 0;
    switch (field_id) {
      case 1: { /* columns */
        uint8_t elem_type = 0;
        int64_t size = 0;
        if (!thrift.read_list_header(elem_type, size)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet row group");
        }
        for (int64_t i = 0; i < size; i++) {
          ObPqColumnChunk chunk;
          if (!ob_pq_parse_column_chunk(thrift, chunk, err)) {
            thrift.end_struct();
            return false;
          }
          group.columns_.push_back(chunk);
        }
        break;
      }
      case 3: { /* num_rows */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet row group");
        }
        group.num_rows_ = number;
        break;
      }
      default: {
        if (!thrift.skip(type)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet row group");
        }
        break;
      }
    }
  }
  thrift.end_struct();
  return true;
}

/**
 * @brief Parse the metadata of the parquet file.
 */
static bool ob_pq_parse_file_metadata(const char *data,
                                       const int64_t size,
                                       ObPqFileMeta &meta,
                                       std::string &err)
{
  ObPqThrift thrift(data, size);
  thrift.begin_struct();
  while (true) {
    uint8_t type = 0;
    int32_t field_id = 0;
    if (!thrift.read_field_header(type, field_id)) {
      thrift.end_struct();
      return ob_pq_fail(err, "malformed parquet file metadata");
    }
    if (OB_PQ_THRIFT_STOP == type) {
      break;
    }
    int64_t number = 0;
    switch (field_id) {
      case 1: { /* version */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet file metadata");
        }
        meta.version_ = (int32_t)number;
        break;
      }
      case 2: { /* schema */
        uint8_t elem_type = 0;
        int64_t size = 0;
        if (!thrift.read_list_header(elem_type, size)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet file metadata");
        }
        for (int64_t i = 0; i < size; i++) {
          ObPqSchemaElement element;
          if (!ob_pq_parse_schema_element(thrift, element, err)) {
            thrift.end_struct();
            return false;
          }
          meta.schema_.push_back(element);
        }
        break;
      }
      case 3: { /* num_rows */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet file metadata");
        }
        meta.num_rows_ = number;
        break;
      }
      case 4: { /* row_groups */
        uint8_t elem_type = 0;
        int64_t size = 0;
        if (!thrift.read_list_header(elem_type, size)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet file metadata");
        }
        for (int64_t i = 0; i < size; i++) {
          ObPqRowGroup group;
          if (!ob_pq_parse_row_group(thrift, group, err)) {
            thrift.end_struct();
            return false;
          }
          meta.row_groups_.push_back(group);
        }
        break;
      }
      case 6: { /* created_by */
        if (!thrift.read_string(meta.created_by_)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet file metadata");
        }
        break;
      }
      default: {
        if (!thrift.skip(type)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet file metadata");
        }
        break;
      }
    }
  }
  thrift.end_struct();
  return true;
}

/**
 * @brief Build the leaves of the parquet schema.
 *
 * Only the flat schemas are supported, a schema with a nested group is
 * rejected.
 */
static bool ob_pq_build_leaves(const ObPqFileMeta &meta,
                               std::vector<ObPqLeaf> &leaves,
                               std::string &err)
{
  if (meta.schema_.empty()) {
    return ob_pq_fail(err, "the parquet file has no schema");
  }
  const int64_t schema_size = (int64_t)meta.schema_.size();
  int64_t root_index = -1;
  int64_t root_children = 0;
  if (1 == schema_size && 0 == meta.schema_[0].num_children_) {
    /* a file with one column and without a root group */
    root_children = 1;
  } else if (meta.schema_[0].num_children_ > 0
             && (int64_t)meta.schema_[0].num_children_ == schema_size - 1) {
    root_index = 0;
    root_children = meta.schema_[0].num_children_;
  } else {
    for (int64_t i = 0; i < schema_size; i++) {
      if ((int64_t)meta.schema_[i].num_children_ == schema_size - 1
          && "root" == meta.schema_[i].name_) {
        root_index = i;
        root_children = meta.schema_[i].num_children_;
        break;
      }
    }
    if (root_index < 0) {
      for (int64_t i = 0; i < schema_size; i++) {
        if (meta.schema_[i].num_children_ > 0) {
          root_index = i;
          root_children = meta.schema_[i].num_children_;
          break;
        }
      }
    }
    if (root_index < 0) {
      return ob_pq_fail(err, "cannot find the root of the parquet schema");
    }
  }
  int64_t index = (root_index < 0) ? 0 : root_index + 1;
  for (int64_t i = 0; i < root_children; i++) {
    if (index >= schema_size) {
      return ob_pq_fail(err, "the schema of the parquet file is truncated");
    }
    const ObPqSchemaElement &element = meta.schema_[(size_t)index];
    if (element.num_children_ > 0) {
      return ob_pq_fail(err, "nested parquet schemas are not supported");
    }
    if (OB_PQ_REPEATED == element.repetition_type_) {
      return ob_pq_fail(err, "repeated parquet columns are not supported");
    }
    ObPqLeaf leaf;
    leaf.name_ = element.name_;
    leaf.type_ = element.type_;
    leaf.type_length_ = element.type_length_;
    leaf.optional_ = (OB_PQ_OPTIONAL == element.repetition_type_);
    leaf.max_def_level_ = leaf.optional_ ? 1 : 0;
    leaf.is_utc_ = element.is_utc_;
    if (OB_PQ_LOGICAL_KIND_DATE == element.logical_kind_) {
      leaf.logical_kind_ = OB_PQ_LOGICAL_KIND_DATE;
    } else if (OB_PQ_LOGICAL_KIND_TIMESTAMP == element.logical_kind_) {
      leaf.logical_kind_ = OB_PQ_LOGICAL_KIND_TIMESTAMP;
      leaf.time_unit_ = (OB_PQ_TIME_UNIT_NONE == element.time_unit_) ? OB_PQ_TIME_UNIT_MICROS
                                                                    : element.time_unit_;
    } else if (OB_PQ_CONVERTED_DATE == element.converted_type_) {
      leaf.logical_kind_ = OB_PQ_LOGICAL_KIND_DATE;
    } else if (OB_PQ_CONVERTED_TIMESTAMP_MILLIS == element.converted_type_) {
      leaf.logical_kind_ = OB_PQ_LOGICAL_KIND_TIMESTAMP;
      leaf.time_unit_ = OB_PQ_TIME_UNIT_MILLIS;
    } else if (OB_PQ_CONVERTED_TIMESTAMP_MICROS == element.converted_type_) {
      leaf.logical_kind_ = OB_PQ_LOGICAL_KIND_TIMESTAMP;
      leaf.time_unit_ = OB_PQ_TIME_UNIT_MICROS;
    }
    leaves.push_back(leaf);
    index++;
  }
  if (leaves.empty()) {
    return ob_pq_fail(err, "the parquet file has no column");
  }
  return true;
}

/**
 * @brief Parse the header of one parquet page.
 *
 * @param consumed the number of the bytes that the header holds
 */
static bool ob_pq_parse_page_header(const char *data,
                                    const int64_t size,
                                    ObPqPageHeader &header,
                                    int64_t &consumed,
                                    std::string &err)
{
  ObPqThrift thrift(data, size);
  thrift.begin_struct();
  while (true) {
    uint8_t type = 0;
    int32_t field_id = 0;
    if (!thrift.read_field_header(type, field_id)) {
      thrift.end_struct();
      return ob_pq_fail(err, "malformed parquet page header");
    }
    if (OB_PQ_THRIFT_STOP == type) {
      break;
    }
    int64_t number = 0;
    switch (field_id) {
      case 1: { /* type */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet page header");
        }
        header.type_ = (int32_t)number;
        break;
      }
      case 2: { /* uncompressed_page_size */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet page header");
        }
        header.uncompressed_page_size_ = (int32_t)number;
        break;
      }
      case 3: { /* compressed_page_size */
        if (!thrift.read_zigzag(number)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet page header");
        }
        header.compressed_page_size_ = (int32_t)number;
        break;
      }
      case 5: { /* data_page_header */
        thrift.begin_struct();
        while (true) {
          uint8_t sub_type = 0;
          int32_t sub_field_id = 0;
          if (!thrift.read_field_header(sub_type, sub_field_id)) {
            thrift.end_struct();
            thrift.end_struct();
            return ob_pq_fail(err, "malformed parquet page header");
          }
          if (OB_PQ_THRIFT_STOP == sub_type) {
            break;
          }
          int64_t sub_number = 0;
          switch (sub_field_id) {
            case 1: { /* num_values */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.num_values_ = (int32_t)sub_number;
              break;
            }
            case 2: { /* encoding */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.encoding_ = (int32_t)sub_number;
              break;
            }
            default: {
              if (!thrift.skip(sub_type)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              break;
            }
          }
        }
        thrift.end_struct();
        break;
      }
      case 7: { /* dictionary_page_header */
        thrift.begin_struct();
        while (true) {
          uint8_t sub_type = 0;
          int32_t sub_field_id = 0;
          if (!thrift.read_field_header(sub_type, sub_field_id)) {
            thrift.end_struct();
            thrift.end_struct();
            return ob_pq_fail(err, "malformed parquet page header");
          }
          if (OB_PQ_THRIFT_STOP == sub_type) {
            break;
          }
          int64_t sub_number = 0;
          switch (sub_field_id) {
            case 1: { /* num_values */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.dict_num_values_ = (int32_t)sub_number;
              break;
            }
            case 2: { /* encoding */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.dict_encoding_ = (int32_t)sub_number;
              break;
            }
            default: {
              if (!thrift.skip(sub_type)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              break;
            }
          }
        }
        thrift.end_struct();
        break;
      }
      case 8: { /* data_page_header_v2 */
        thrift.begin_struct();
        while (true) {
          uint8_t sub_type = 0;
          int32_t sub_field_id = 0;
          if (!thrift.read_field_header(sub_type, sub_field_id)) {
            thrift.end_struct();
            thrift.end_struct();
            return ob_pq_fail(err, "malformed parquet page header");
          }
          if (OB_PQ_THRIFT_STOP == sub_type) {
            break;
          }
          int64_t sub_number = 0;
          switch (sub_field_id) {
            case 1: { /* num_values */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.num_values_ = (int32_t)sub_number;
              break;
            }
            case 2: { /* num_nulls */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.num_nulls_ = (int32_t)sub_number;
              break;
            }
            case 3: { /* num_rows */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.num_rows_ = (int32_t)sub_number;
              break;
            }
            case 4: { /* encoding */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.encoding_ = (int32_t)sub_number;
              break;
            }
            case 5: { /* definition_levels_byte_length */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.def_levels_byte_length_ = (int32_t)sub_number;
              break;
            }
            case 6: { /* repetition_levels_byte_length */
              if (!thrift.read_zigzag(sub_number)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              header.rep_levels_byte_length_ = (int32_t)sub_number;
              break;
            }
            case 7: { /* is_compressed */
              if (OB_PQ_THRIFT_BOOL_TRUE == sub_type) {
                header.is_compressed_ = true;
              } else if (OB_PQ_THRIFT_BOOL_FALSE == sub_type) {
                header.is_compressed_ = false;
              } else if (!thrift.skip(sub_type)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              break;
            }
            default: {
              if (!thrift.skip(sub_type)) {
                thrift.end_struct();
                thrift.end_struct();
                return ob_pq_fail(err, "malformed parquet page header");
              }
              break;
            }
          }
        }
        thrift.end_struct();
        break;
      }
      default: {
        if (!thrift.skip(type)) {
          thrift.end_struct();
          return ob_pq_fail(err, "malformed parquet page header");
        }
        break;
      }
    }
  }
  thrift.end_struct();
  consumed = thrift.pos();
  return true;
}

/**
 * @brief Decode the values of a page that are encoded with the plain encoding.
 */
static bool ob_pq_decode_plain_values(const int32_t type,
                                       const int32_t type_length,
                                       const char *buf,
                                       const int64_t len,
                                       const int64_t count,
                                       std::vector<ObPqValue> &out,
                                       std::string &err)
{
  int64_t pos = 0;
  for (int64_t i = 0; i < count; i++) {
    ObPqValue value;
    switch (type) {
      case OB_PQ_BOOLEAN: {
        value.bool_value_ = (0 != ob_pq_read_bits(buf, len, i, 1));
        break;
      }
      case OB_PQ_INT32: {
        if (pos + 4 > len) {
          return ob_pq_fail(err, "the parquet page holds less values than it declares");
        }
        uint32_t raw = 0;
        for (int32_t j = 0; j < 4; j++) {
          raw |= ((uint32_t)(uint8_t)buf[pos + j]) << (8 * j);
        }
        value.int_value_ = (int64_t)(int32_t)raw;
        pos += 4;
        break;
      }
      case OB_PQ_INT64: {
        if (pos + 8 > len) {
          return ob_pq_fail(err, "the parquet page holds less values than it declares");
        }
        uint64_t raw = 0;
        for (int32_t j = 0; j < 8; j++) {
          raw |= ((uint64_t)(uint8_t)buf[pos + j]) << (8 * j);
        }
        value.int_value_ = (int64_t)raw;
        pos += 8;
        break;
      }
      case OB_PQ_FLOAT: {
        if (pos + 4 > len) {
          return ob_pq_fail(err, "the parquet page holds less values than it declares");
        }
        uint32_t raw = 0;
        for (int32_t j = 0; j < 4; j++) {
          raw |= ((uint32_t)(uint8_t)buf[pos + j]) << (8 * j);
        }
        float single = 0.0f;
        memcpy(&single, &raw, sizeof(float));
        value.double_value_ = (double)single;
        pos += 4;
        break;
      }
      case OB_PQ_DOUBLE: {
        if (pos + 8 > len) {
          return ob_pq_fail(err, "the parquet page holds less values than it declares");
        }
        uint64_t raw = 0;
        for (int32_t j = 0; j < 8; j++) {
          raw |= ((uint64_t)(uint8_t)buf[pos + j]) << (8 * j);
        }
        double dbl = 0.0;
        memcpy(&dbl, &raw, sizeof(double));
        value.double_value_ = dbl;
        pos += 8;
        break;
      }
      case OB_PQ_BYTE_ARRAY: {
        if (pos + 4 > len) {
          return ob_pq_fail(err, "the parquet page holds less values than it declares");
        }
        uint32_t size = 0;
        for (int32_t j = 0; j < 4; j++) {
          size |= ((uint32_t)(uint8_t)buf[pos + j]) << (8 * j);
        }
        pos += 4;
        if (pos + (int64_t)size > len) {
          return ob_pq_fail(err, "the parquet page holds less values than it declares");
        }
        value.bin_offset_ = pos;
        value.bin_len_ = (int32_t)size;
        pos += size;
        break;
      }
      case OB_PQ_FIXED_LEN_BYTE_ARRAY: {
        const int32_t size = (type_length > 0) ? type_length : 0;
        if (pos + size > len) {
          return ob_pq_fail(err, "the parquet page holds less values than it declares");
        }
        value.bin_offset_ = pos;
        value.bin_len_ = size;
        pos += size;
        break;
      }
      default: {
        return ob_pq_fail(err, "unsupported parquet type");
      }
    }
    out.push_back(value);
  }
  return true;
}

/**
 * @brief Decode the indexes of a dictionary encoded page.
 *
 * The indexes are encoded with the RLE and bit packing hybrid encoding. The
 * width of the values depends on the size of the dictionary, some writers
 * store it in the first byte of the page and some do not, both conventions
 * are tried. An index that is out of the range of the dictionary shows that
 * the convention is wrong.
 */
static bool ob_pq_decode_dictionary_indexes(const char *buf,
                                             const int64_t len,
                                             const int64_t dict_size,
                                             const int64_t count,
                                             std::vector<int32_t> &out,
                                             std::string &err)
{
  if (dict_size <= 0) {
    return ob_pq_fail(err, "the parquet page uses a dictionary that does not exist");
  }
  /* the width that is needed to hold the indexes of the dictionary */
  int32_t width = 0;
  while ((1LL << width) <= (int64_t)dict_size) {
    width++;
  }
  if (width < 1) {
    width = 1;
  }
  /* the width that is stored in the first byte of the page, if any */
  const int32_t stored_width = (len > 0) ? (int32_t)(uint8_t)buf[0] : 0;
  int32_t attempts[4];
  int32_t attempt_count = 0;
  if (stored_width >= 1 && stored_width <= 32) {
    attempts[attempt_count++] = stored_width;
  }
  attempts[attempt_count++] = width;
  if (width > 1) {
    attempts[attempt_count++] = width - 1;
  }
  attempts[attempt_count++] = width + 1;
  std::string last_error;
  for (int32_t i = 0; i < attempt_count; i++) {
    const int32_t bit_width = attempts[i];
    if (bit_width < 1 || bit_width > 32) {
      continue;
    }
    /* the width is stored in the first byte of the page when it differs from
     * the width that is computed from the size of the dictionary */
    const bool stored = (bit_width == stored_width && len > 0);
    const char *data = stored ? buf + 1 : buf;
    const int64_t data_len = stored ? len - 1 : len;
    std::vector<uint32_t> values;
    int64_t consumed = 0;
    std::string current_error;
    if (!ob_pq_decode_rle_hybrid(data, data_len, bit_width, count, values, consumed, current_error)) {
      last_error = current_error;
      continue;
    }
    if ((int64_t)values.size() != count) {
      last_error = "the parquet page holds less values than it declares";
      continue;
    }
    bool valid = true;
    for (int64_t j = 0; j < count; j++) {
      if ((int64_t)values[j] >= dict_size) {
        valid = false;
        break;
      }
    }
    if (!valid) {
      last_error = "the parquet page holds an index that is out of the range of the dictionary";
      continue;
    }
    out.clear();
    for (int64_t j = 0; j < count; j++) {
      out.push_back((int32_t)values[j]);
    }
    return true;
  }
  if (last_error.empty()) {
    last_error = "the parquet page holds less values than it declares";
  }
  return ob_pq_fail(err, last_error.c_str());
}

/**
 * @brief The offset of the first page of a column chunk.
 */
static int64_t ob_pq_chunk_start(const ObPqColumnChunk &chunk)
{
  if (chunk.meta_.dictionary_page_offset_ >= 0) {
    return chunk.meta_.dictionary_page_offset_;
  }
  return chunk.meta_.data_page_offset_;
}

/**
 * @brief The state of one column of the reader.
 *
 * The state holds the pages of the current column chunk, the pages are
 * decoded one at a time, the whole chunk is never materialized.
 */
class ObPqColumnState
{
public:
  ObPqColumnState()
      : source_(NULL),
        type_(-1),
        type_length_(0),
        codec_(OB_PQ_CODEC_UNCOMPRESSED),
        logical_kind_(OB_PQ_LOGICAL_KIND_NONE),
        time_unit_(OB_PQ_TIME_UNIT_NONE),
        max_def_level_(0),
        chunk_end_(0),
        chunk_num_values_(0),
        values_read_(0),
        page_pos_(0),
        page_num_values_(0),
        page_row_index_(0),
        page_value_index_(0),
        page_loaded_(false) {}
  void set_leaf(ObPqFileSource *source, const ObPqLeaf &leaf)
  {
    source_ = source;
    type_ = leaf.type_;
    type_length_ = leaf.type_length_;
    max_def_level_ = leaf.max_def_level_;
    logical_kind_ = leaf.logical_kind_;
    time_unit_ = leaf.time_unit_;
    codec_ = OB_PQ_CODEC_UNCOMPRESSED;
  }
  void reset_chunk(const ObPqColumnChunk &chunk, const int64_t limit, const int64_t group_rows)
  {
    codec_ = chunk.meta_.codec_;
    page_pos_ = ob_pq_chunk_start(chunk);
    chunk_end_ = limit;
    chunk_num_values_ = (chunk.meta_.num_values_ > 0) ? chunk.meta_.num_values_ : group_rows;
    values_read_ = 0;
    dict_buf_.clear();
    dict_values_.clear();
    values_buf_.clear();
    def_levels_.clear();
    page_values_.clear();
    page_num_values_ = 0;
    page_row_index_ = 0;
    page_value_index_ = 0;
    page_loaded_ = false;
  }
  /**
   * @brief Decode the next page of the current column chunk.
   *
   * @return 0 when a page was decoded, 1 when the chunk is over, -1 on error
   */
  int load_next_page(std::string &err);
  /**
   * @brief Produce the value of the next row of the current column.
   *
   * @return 0 on success, -1 on error
   */
  int next_value(ObPqResolvedValue &value, std::string &err);

private:
  int decode_data_page(const char *payload,
                       const int64_t payload_len,
                       const ObPqPageHeader &header,
                       const bool is_v2,
                       std::string &err);
  int decode_dictionary_page(const char *payload,
                             const int64_t payload_len,
                             const ObPqPageHeader &header,
                             std::string &err);
  int resolve_value(const int64_t value_index, ObPqResolvedValue &value, std::string &err);

private:
  ObPqFileSource *source_;
  int32_t type_;
  int32_t type_length_;
  int32_t codec_;
  int32_t logical_kind_;
  int32_t time_unit_;
  int32_t max_def_level_;
  int64_t chunk_end_;
  int64_t chunk_num_values_;
  int64_t values_read_;
  /* the offset of the current page in the file */
  int64_t page_pos_;
  /* the dictionary of the current column chunk */
  std::vector<char> dict_buf_;
  std::vector<ObPqDictValue> dict_values_;
  /* the current page */
  std::vector<char> values_buf_;
  std::vector<uint32_t> def_levels_;
  std::vector<ObPqValue> page_values_;
  int64_t page_num_values_;
  int64_t page_row_index_;
  int64_t page_value_index_;
  bool page_loaded_;
};

int ObPqColumnState::decode_dictionary_page(const char *payload,
                                            const int64_t payload_len,
                                            const ObPqPageHeader &header,
                                            std::string &err)
{
  dict_values_.clear();
  if (OB_PQ_ENCODING_PLAIN != header.dict_encoding_
      && OB_PQ_ENCODING_PLAIN_DICTIONARY != header.dict_encoding_) {
    return ob_pq_error(err, "unsupported parquet encoding");
  }
  std::vector<ObPqValue> values;
  if (!ob_pq_decode_plain_values(type_,
                                 type_length_,
                                 payload,
                                 payload_len,
                                 header.dict_num_values_,
                                 values,
                                 err)) {
    return -1;
  }
  dict_buf_.assign(payload, payload + payload_len);
  dict_values_.reserve(values.size());
  for (int64_t i = 0; i < (int64_t)values.size(); i++) {
    ObPqDictValue dict_value;
    dict_value.int_value_ = values[i].int_value_;
    dict_value.double_value_ = values[i].double_value_;
    dict_value.bool_value_ = values[i].bool_value_;
    dict_value.bin_offset_ = values[i].bin_offset_;
    dict_value.bin_len_ = values[i].bin_len_;
    dict_values_.push_back(dict_value);
  }
  return 0;
}

int ObPqColumnState::decode_data_page(const char *payload,
                                      const int64_t payload_len,
                                      const ObPqPageHeader &header,
                                      const bool is_v2,
                                      std::string &err)
{
  def_levels_.clear();
  values_buf_.clear();
  page_values_.clear();
  const int64_t num_values = header.num_values_;
  int64_t values_offset = 0;
  int64_t values_len = 0;
  if (is_v2) {
    /* the levels of a data page v2 are not compressed */
    const int64_t rep_len = header.rep_levels_byte_length_;
    const int64_t def_len = header.def_levels_byte_length_;
    if (rep_len + def_len > payload_len) {
      return ob_pq_error(err, "the parquet page holds less levels than it declares");
    }
    if (max_def_level_ > 0 && def_len > 0) {
      int64_t consumed = 0;
      if (!ob_pq_decode_rle_hybrid(payload + rep_len,
                                   def_len,
                                   ob_pq_level_bit_width(max_def_level_),
                                   num_values,
                                   def_levels_,
                                   consumed,
                                   err)) {
        return -1;
      }
    }
    values_offset = rep_len + def_len;
    values_len = payload_len - values_offset;
  } else {
    /* the levels and the values of a data page v1 are compressed together */
    int64_t pos = 0;
    if (max_def_level_ > 0) {
      int64_t consumed = 0;
      if (!ob_pq_decode_rle_hybrid(payload + pos,
                                   payload_len - pos,
                                   ob_pq_level_bit_width(max_def_level_),
                                   num_values,
                                   def_levels_,
                                   consumed,
                                   err)) {
        return -1;
      }
      pos += consumed;
    }
    values_offset = pos;
    values_len = payload_len - pos;
  }
  if (max_def_level_ > 0 && (int64_t)def_levels_.size() != num_values) {
    return ob_pq_error(err, "the parquet page holds less levels than it declares");
  }
  /* count the values that are not null */
  int64_t value_count = num_values;
  if (max_def_level_ > 0) {
    value_count = 0;
    for (int64_t i = 0; i < (int64_t)def_levels_.size(); i++) {
      if (def_levels_[(size_t)i] >= (uint32_t)max_def_level_) {
        value_count++;
      }
    }
  }
  values_buf_.assign(payload + values_offset, payload + values_offset + values_len);
  const char *values_data = values_buf_.empty() ? "" : &values_buf_[0];
  const int64_t values_size = (int64_t)values_buf_.size();
  if (OB_PQ_ENCODING_PLAIN == header.encoding_) {
    if (!ob_pq_decode_plain_values(
            type_, type_length_, values_data, values_size, value_count, page_values_, err)) {
      return -1;
    }
  } else if (OB_PQ_ENCODING_RLE_DICTIONARY == header.encoding_
             || OB_PQ_ENCODING_PLAIN_DICTIONARY == header.encoding_) {
    std::vector<int32_t> indexes;
    if (!ob_pq_decode_dictionary_indexes(
            values_data, values_size, (int64_t)dict_values_.size(), value_count, indexes, err)) {
      return -1;
    }
    for (int64_t i = 0; i < (int64_t)indexes.size(); i++) {
      ObPqValue value;
      value.dict_index_ = indexes[i];
      page_values_.push_back(value);
    }
  } else {
    return ob_pq_error(err, "unsupported parquet encoding");
  }
  page_num_values_ = num_values;
  page_row_index_ = 0;
  page_value_index_ = 0;
  page_loaded_ = true;
  return 0;
}

int ObPqColumnState::load_next_page(std::string &err)
{
  if (NULL == source_) {
    return ob_pq_error(err, "the parquet column has no source");
  }
  while (page_pos_ < chunk_end_) {
    /* read the header of the page */
    const int64_t header_max = 4096;
    int64_t available = chunk_end_ - page_pos_;
    if (available > header_max) {
      available = header_max;
    }
    std::vector<char> header_buf((size_t)available);
    const int64_t header_read = source_->read(page_pos_, available, &header_buf[0]);
    if (header_read <= 0) {
      return ob_pq_error(err, "cannot read the header of a parquet page");
    }
    ObPqPageHeader header;
    int64_t header_size = 0;
    if (!ob_pq_parse_page_header(&header_buf[0], header_read, header, header_size, err)) {
      return -1;
    }
    const int64_t payload_offset = page_pos_ + header_size;
    const int64_t payload_size = header.compressed_page_size_;
    if (payload_size < 0) {
      return ob_pq_error(err, "the parquet page declares a negative size");
    }
    std::vector<char> payload((size_t)payload_size);
    if (payload_size > 0) {
      const int64_t payload_read = source_->read(payload_offset, payload_size, &payload[0]);
      if (payload_read != payload_size) {
        return ob_pq_error(err, "cannot read the content of a parquet page");
      }
    }
    page_pos_ = payload_offset + payload_size;
    const char *payload_data = payload.empty() ? "" : &payload[0];
    if (OB_PQ_PAGE_DICTIONARY == header.type_) {
      std::vector<char> uncompressed;
      const char *data = payload_data;
      int64_t size = payload_size;
      if (OB_PQ_CODEC_SNAPPY == codec_) {
        if (!ob_pq_snappy_decompress(payload_data, payload_size, uncompressed, err)) {
          return -1;
        }
        data = uncompressed.empty() ? "" : &uncompressed[0];
        size = (int64_t)uncompressed.size();
      } else if (OB_PQ_CODEC_UNCOMPRESSED != codec_) {
        return ob_pq_error(err, "unsupported parquet codec");
      }
      if (0 != decode_dictionary_page(data, size, header, err)) {
        return -1;
      }
      /* the dictionary page is not a data page, the next one is searched */
      continue;
    } else if (OB_PQ_PAGE_DATA == header.type_ || OB_PQ_PAGE_DATA_V2 == header.type_) {
      const bool is_v2 = (OB_PQ_PAGE_DATA_V2 == header.type_);
      std::vector<char> uncompressed;
      const char *data = payload_data;
      int64_t size = payload_size;
      if (is_v2) {
        /* the levels of a data page v2 are not compressed */
        const int64_t rep_len = header.rep_levels_byte_length_;
        const int64_t def_len = header.def_levels_byte_length_;
        const int64_t levels_len = rep_len + def_len;
        if (levels_len > payload_size) {
          return ob_pq_error(err, "the parquet page holds less levels than it declares");
        }
        const int64_t values_size = payload_size - levels_len;
        if (header.is_compressed_ && OB_PQ_CODEC_SNAPPY == codec_) {
          std::vector<char> values;
          if (!ob_pq_snappy_decompress(payload_data + levels_len, values_size, values, err)) {
            return -1;
          }
          uncompressed.resize((size_t)levels_len + values.size());
          memcpy(&uncompressed[0], payload_data, (size_t)levels_len);
          if (!values.empty()) {
            memcpy(&uncompressed[(size_t)levels_len], &values[0], values.size());
          }
          data = uncompressed.empty() ? "" : &uncompressed[0];
          size = (int64_t)uncompressed.size();
        } else if (header.is_compressed_ && OB_PQ_CODEC_UNCOMPRESSED != codec_) {
          return ob_pq_error(err, "unsupported parquet codec");
        }
      } else if (OB_PQ_CODEC_SNAPPY == codec_) {
        if (!ob_pq_snappy_decompress(payload_data, payload_size, uncompressed, err)) {
          return -1;
        }
        data = uncompressed.empty() ? "" : &uncompressed[0];
        size = (int64_t)uncompressed.size();
      } else if (OB_PQ_CODEC_UNCOMPRESSED != codec_) {
        return ob_pq_error(err, "unsupported parquet codec");
      }
      if (0 != decode_data_page(data, size, header, is_v2, err)) {
        return -1;
      }
      return 0;
    } else if (OB_PQ_PAGE_INDEX == header.type_) {
      /* the index pages are not needed, they are skipped */
      continue;
    } else {
      return ob_pq_error(err, "unsupported parquet page type");
    }
  }
  page_loaded_ = false;
  return 1;
}

int ObPqColumnState::resolve_value(const int64_t value_index,
                                    ObPqResolvedValue &value,
                                    std::string &err)
{
  if (value_index < 0 || value_index >= (int64_t)page_values_.size()) {
    return ob_pq_error(err, "the parquet column holds less values than it declares");
  }
  const ObPqValue &page_value = page_values_[(size_t)value_index];
  value.type_ = type_;
  value.logical_kind_ = logical_kind_;
  value.time_unit_ = time_unit_;
  value.is_null_ = false;
  value.bin_data_ = NULL;
  value.bin_len_ = 0;
  if (page_value.dict_index_ >= 0) {
    const int32_t dict_index = page_value.dict_index_;
    if (dict_index >= (int32_t)dict_values_.size()) {
      return ob_pq_error(err, "the parquet dictionary holds less values than it declares");
    }
    const ObPqDictValue &dict_value = dict_values_[(size_t)dict_index];
    value.int_value_ = dict_value.int_value_;
    value.double_value_ = dict_value.double_value_;
    value.bool_value_ = dict_value.bool_value_;
    if ((OB_PQ_BYTE_ARRAY == type_ || OB_PQ_FIXED_LEN_BYTE_ARRAY == type_)
        && dict_value.bin_offset_ + dict_value.bin_len_ <= (int64_t)dict_buf_.size()) {
      value.bin_data_ = &dict_buf_[(size_t)dict_value.bin_offset_];
      value.bin_len_ = dict_value.bin_len_;
    }
  } else {
    value.int_value_ = page_value.int_value_;
    value.double_value_ = page_value.double_value_;
    value.bool_value_ = page_value.bool_value_;
    if ((OB_PQ_BYTE_ARRAY == type_ || OB_PQ_FIXED_LEN_BYTE_ARRAY == type_)
        && page_value.bin_offset_ + page_value.bin_len_ <= (int64_t)values_buf_.size()) {
      value.bin_data_ = &values_buf_[(size_t)page_value.bin_offset_];
      value.bin_len_ = page_value.bin_len_;
    }
  }
  return 0;
}

int ObPqColumnState::next_value(ObPqResolvedValue &value, std::string &err)
{
  while (true) {
    if (!page_loaded_) {
      if (values_read_ >= chunk_num_values_) {
        return ob_pq_error(err, "the parquet column holds less values than it declares");
      }
      const int ret = load_next_page(err);
      if (ret < 0) {
        return -1;
      } else if (ret > 0) {
        return ob_pq_error(err, "the parquet column holds less values than it declares");
      }
    }
    if (page_row_index_ >= page_num_values_) {
      page_loaded_ = false;
      continue;
    }
    const int64_t row = page_row_index_++;
    if (max_def_level_ > 0 && def_levels_[(size_t)row] < (uint32_t)max_def_level_) {
      /* the value is null */
      value.type_ = type_;
      value.logical_kind_ = logical_kind_;
      value.time_unit_ = time_unit_;
      value.is_null_ = true;
      value.int_value_ = 0;
      value.double_value_ = 0.0;
      value.bool_value_ = false;
      value.bin_data_ = NULL;
      value.bin_len_ = 0;
      values_read_++;
      return 0;
    }
    const int ret = resolve_value(page_value_index_++, value, err);
    if (0 != ret) {
      return ret;
    }
    values_read_++;
    return 0;
  }
}

/**
 * @brief The reader of the rows of a parquet file.
 *
 * The reader decodes one page at a time, the rows of the row groups are
 * produced one by one, the whole file is never materialized.
 */
class ObPqRowReader
{
public:
  ObPqRowReader()
      : meta_(NULL),
        leaves_(NULL),
        group_index_(-1),
        group_rows_(0),
        row_index_in_group_(0),
        data_end_(0),
        done_(false) {}
  bool init(ObPqFileSource &source,
            const ObPqFileMeta &meta,
            const std::vector<ObPqLeaf> &leaves,
            const std::vector<int64_t> &projection,
            const int64_t data_end,
            std::string &err)
  {
    meta_ = &meta;
    leaves_ = &leaves;
    projection_ = projection;
    data_end_ = data_end;
    states_.clear();
    for (int64_t i = 0; i < (int64_t)projection_.size(); i++) {
      if (projection_[(size_t)i] < 0 || projection_[(size_t)i] >= (int64_t)leaves_->size()) {
        return ob_pq_fail(err, "the parquet file does not have the projected column");
      }
      ObPqColumnState state;
      state.set_leaf(&source, (*leaves_)[(size_t)projection_[(size_t)i]]);
      states_.push_back(state);
    }
    group_index_ = -1;
    group_rows_ = 0;
    row_index_in_group_ = 0;
    done_ = false;
    return true;
  }
  /**
   * @brief Produce the next row.
   *
   * @return 1 when a row was produced, 0 at the end of the file, -1 on error
   */
  int next_row(ObPqResolvedValue *values, const int64_t value_count, std::string &err)
  {
    if (done_) {
      return 0;
    }
    if (row_index_in_group_ >= group_rows_) {
      int64_t next_group = group_index_ + 1;
      while (next_group < (int64_t)meta_->row_groups_.size()
             && meta_->row_groups_[(size_t)next_group].num_rows_ <= 0) {
        next_group++;
      }
      if (next_group >= (int64_t)meta_->row_groups_.size()) {
        done_ = true;
        return 0;
      }
      if (!load_row_group(next_group, err)) {
        return -1;
      }
    }
    for (int64_t i = 0; i < value_count; i++) {
      if (0 != states_[(size_t)i].next_value(values[i], err)) {
        return -1;
      }
    }
    row_index_in_group_++;
    return 1;
  }

private:
  bool load_row_group(const int64_t group_index, std::string &err)
  {
    const ObPqRowGroup &group = meta_->row_groups_[(size_t)group_index];
    group_index_ = group_index;
    group_rows_ = group.num_rows_;
    row_index_in_group_ = 0;
    for (int64_t i = 0; i < (int64_t)states_.size(); i++) {
      const ObPqLeaf &leaf = (*leaves_)[(size_t)projection_[(size_t)i]];
      int64_t chunk_index = -1;
      /* the columns are matched with the leaves of the schema by name */
      for (int64_t j = 0; j < (int64_t)group.columns_.size(); j++) {
        const ObPqColumnChunk &chunk = group.columns_[(size_t)j];
        if (!chunk.has_meta_ || chunk.meta_.path_in_schema_.empty()) {
          continue;
        }
        if (chunk.meta_.path_in_schema_.back() == leaf.name_) {
          chunk_index = j;
          break;
        }
      }
      if (chunk_index < 0) {
        /* the columns of the file are in the order of the schema */
        if (projection_[(size_t)i] < (int64_t)group.columns_.size()) {
          chunk_index = projection_[(size_t)i];
        }
      }
      if (chunk_index < 0) {
        err = "cannot find the column " + leaf.name_ + " in the parquet file";
        return false;
      }
      const ObPqColumnChunk &chunk = group.columns_[(size_t)chunk_index];
      if (!chunk.has_meta_) {
        err = "the parquet file has no metadata for the column " + leaf.name_;
        return false;
      }
      const int64_t start = ob_pq_chunk_start(chunk);
      if (start < 0) {
        err = "the parquet file has no data for the column " + leaf.name_;
        return false;
      }
      /* the chunk ends where the next chunk of the row group begins */
      int64_t limit = data_end_;
      for (int64_t j = 0; j < (int64_t)group.columns_.size(); j++) {
        if (j == chunk_index) {
          continue;
        }
        const int64_t other = ob_pq_chunk_start(group.columns_[(size_t)j]);
        if (other > start && other < limit) {
          limit = other;
        }
      }
      states_[(size_t)i].reset_chunk(chunk, limit, group.num_rows_);
    }
    return true;
  }

private:
  const ObPqFileMeta *meta_;
  const std::vector<ObPqLeaf> *leaves_;
  std::vector<int64_t> projection_;
  std::vector<ObPqColumnState> states_;
  int64_t group_index_;
  int64_t group_rows_;
  int64_t row_index_in_group_;
  int64_t data_end_;
  bool done_;
};

/**
 * @brief Convert a number of days since the epoch into a civil date.
 */
static void ob_pq_civil_from_days(const int64_t days,
                                  int64_t &year,
                                  int32_t &month,
                                  int32_t &day)
{
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  day = (int32_t)(doy - (153 * mp + 2) / 5 + 1);
  month = (int32_t)(mp < 10 ? mp + 3 : mp - 9);
  year = yy + (month <= 2 ? 1 : 0);
}

/**
 * @brief The floor of the division of two integers.
 */
static int64_t ob_pq_floor_div(const int64_t value, const int64_t divisor)
{
  int64_t quotient = value / divisor;
  if (0 != (value % divisor) && ((value < 0) != (divisor < 0))) {
    quotient--;
  }
  return quotient;
}

/**
 * @brief Format a date of a parquet file as a text.
 *
 * The date of a parquet file is a number of days since the epoch, the text is
 * the one that the SQL engine expects.
 */
static bool ob_pq_format_date(const int64_t days, char *buf, const int32_t buf_len, int32_t &len)
{
  int64_t year = 0;
  int32_t month = 0;
  int32_t day = 0;
  ob_pq_civil_from_days(days, year, month, day);
  len = snprintf(buf, (size_t)buf_len, "%04lld-%02d-%02d", (long long)year, month, day);
  if (len < 0 || len >= buf_len) {
    return false;
  }
  return true;
}

/**
 * @brief Format a timestamp of a parquet file as a text.
 *
 * The timestamp of a parquet file is a number of the units that are counted
 * since the epoch, the text is the one that the SQL engine expects.
 */
static bool ob_pq_format_timestamp(const int64_t value,
                                   const int32_t time_unit,
                                   char *buf,
                                   const int32_t buf_len,
                                   int32_t &len)
{
  int64_t micros = 0;
  int32_t fraction_digits = 6;
  if (OB_PQ_TIME_UNIT_MILLIS == time_unit) {
    micros = value * 1000;
    fraction_digits = 3;
  } else if (OB_PQ_TIME_UNIT_NANOS == time_unit) {
    micros = value / 1000;
  } else {
    micros = value;
  }
  const int64_t micros_per_day = 86400LL * 1000000LL;
  const int64_t days = ob_pq_floor_div(micros, micros_per_day);
  int64_t remainder = micros - days * micros_per_day;
  const int64_t hour = remainder / 3600000000LL;
  remainder -= hour * 3600000000LL;
  const int64_t minute = remainder / 60000000LL;
  remainder -= minute * 60000000LL;
  const int64_t second = remainder / 1000000LL;
  const int64_t fraction = remainder - second * 1000000LL;
  int64_t year = 0;
  int32_t month = 0;
  int32_t day = 0;
  ob_pq_civil_from_days(days, year, month, day);
  if (fraction_digits > 0) {
    len = snprintf(buf,
                   (size_t)buf_len,
                   "%04lld-%02d-%02d %02lld:%02lld:%02lld.%0*lld",
                   (long long)year,
                   month,
                   day,
                   (long long)hour,
                   (long long)minute,
                   (long long)second,
                   fraction_digits,
                   (long long)fraction);
  } else {
    len = snprintf(buf,
                   (size_t)buf_len,
                   "%04lld-%02d-%02d %02lld:%02lld:%02lld",
                   (long long)year,
                   month,
                   day,
                   (long long)hour,
                   (long long)minute,
                   (long long)second);
  }
  if (len < 0 || len >= buf_len) {
    return false;
  }
  return true;
}

} // namespace ob_parquet_core
// ==== END PARQUET CORE ====

/**
 * @brief The SQL type of a column of a parquet file.
 *
 * The mapping is the one that the competition expects: the integers are
 * widened to the big integers and the floats are widened to the doubles, so
 * that the parquet files and the csv files share the same vocabulary of the
 * types.
 */
static common::ObObjType ob_parquet_sql_type(const ob_parquet_core::ObPqLeaf &leaf)
{
  common::ObObjType type = common::ObUnknownType;
  if (ob_parquet_core::OB_PQ_LOGICAL_KIND_DATE == leaf.logical_kind_) {
    type = common::ObDateType;
  } else if (ob_parquet_core::OB_PQ_LOGICAL_KIND_TIMESTAMP == leaf.logical_kind_) {
    type = common::ObDateTimeType;
  } else {
    switch (leaf.type_) {
      case ob_parquet_core::OB_PQ_BOOLEAN: {
        type = common::ObTinyIntType;
        break;
      }
      case ob_parquet_core::OB_PQ_INT32:
      case ob_parquet_core::OB_PQ_INT64: {
        type = common::ObIntType;
        break;
      }
      case ob_parquet_core::OB_PQ_FLOAT:
      case ob_parquet_core::OB_PQ_DOUBLE: {
        type = common::ObDoubleType;
        break;
      }
      case ob_parquet_core::OB_PQ_BYTE_ARRAY:
      case ob_parquet_core::OB_PQ_FIXED_LEN_BYTE_ARRAY: {
        type = common::ObVarcharType;
        break;
      }
      default: {
        type = common::ObUnknownType;
        break;
      }
    }
  }
  return type;
}

/**
 * @brief The source of the bytes of a parquet file.
 */
class ObParquetFileSource : public ob_parquet_core::ObPqFileSource
{
public:
  ObParquetFileSource() : last_ret_(OB_SUCCESS) {}
  int open(const common::ObString &path)
  {
    last_ret_ = reader_.open(path, false);
    return last_ret_;
  }
  void close() { reader_.close(); }
  bool is_opened() const { return reader_.is_opened(); }
  int pread(void *buf, const int64_t count, const int64_t offset, int64_t &read_size)
  {
    last_ret_ = reader_.pread(buf, count, offset, read_size);
    return last_ret_;
  }
  virtual int64_t read(const int64_t offset, const int64_t size, char *buf)
  {
    if (!reader_.is_opened()) {
      last_ret_ = OB_ERR_UNEXPECTED;
      return -1;
    }
    int64_t read_size = 0;
    last_ret_ = reader_.pread(buf, size, offset, read_size);
    if (OB_SUCCESS != last_ret_) {
      return -1;
    }
    return read_size;
  }
  int get_last_ret() const { return last_ret_; }

private:
  common::ObFileReader reader_;
  int last_ret_;
};

/**
 * @brief The implementation of the parquet reader.
 */
class ObParquetFileScanReader::Impl
{
public:
  Impl(const ObFileScanOptions &options, const common::ObIArray<ObFileColumnDef> &columns)
      : options_(options),
        columns_(),
        file_size_(-1),
        data_end_(0),
        opened_(false),
        meta_(),
        leaves_(),
        projection_(),
        reader_(),
        values_(),
        column_types_()
  {
    for (int64_t i = 0; i < columns.count(); i++) {
      columns_.push_back(columns.at(i));
    }
  }
  ~Impl() { close(); }
  int open();
  int next_row();
  int close();
  int infer_schema(common::ObIAllocator &allocator, common::ObIArray<ObFileColumnDef> &columns);
  int64_t get_value_count() const { return (int64_t)values_.size(); }
  const ob_parquet_core::ObPqResolvedValue &get_value(const int64_t index) const
  {
    return values_[(size_t)index];
  }
  common::ObObjType get_column_type(const int64_t index) const
  {
    return column_types_[(size_t)index];
  }
  const common::ObString &get_path() const { return options_.path_; }
  int set_value(common::ObObj &obj,
                const ob_parquet_core::ObPqResolvedValue &value,
                const common::ObObjType type);

private:
  int read_footer();
  int build_projection();

private:
  ObFileScanOptions options_;
  common::ObArray<ObFileColumnDef> columns_;
  int64_t file_size_;
  int64_t data_end_;
  bool opened_;
  ob_parquet_core::ObPqFileMeta meta_;
  std::vector<ob_parquet_core::ObPqLeaf> leaves_;
  std::vector<int64_t> projection_;
  ObParquetFileSource source_;
  ob_parquet_core::ObPqRowReader reader_;
  std::vector<ob_parquet_core::ObPqResolvedValue> values_;
  std::vector<common::ObObjType> column_types_;
  /* the strings of the row that is produced, they live until the next row */
  common::ObArenaAllocator row_arena_;
};

int ObParquetFileScanReader::Impl::open()
{
  int ret = OB_SUCCESS;
  if (opened_) {
    return OB_SUCCESS;
  }
  if (OB_FAIL(read_footer())) {
    LOG_WARN("cannot read the footer of the parquet file", K(ret), K(options_.path_));
    return ret;
  }
  if (OB_FAIL(build_projection())) {
    LOG_WARN("cannot project the columns of the parquet file", K(ret), K(options_.path_));
    return ret;
  }
  opened_ = true;
  return ret;
}

int ObParquetFileScanReader::Impl::read_footer()
{
  int ret = OB_SUCCESS;
  if (file_size_ < 0) {
    int64_t file_mtime = 0;
    if (OB_FAIL(ObFileScanUtils::stat_file(options_.path_, file_size_, file_mtime))) {
      LOG_WARN("cannot stat the parquet file", K(ret), K(options_.path_));
      return ret;
    }
  }
  if (file_size_ < 12) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the parquet file is too small to hold a footer", K(ret), K(file_size_));
    return ret;
  }
  if (OB_FAIL(source_.open(options_.path_))) {
    LOG_WARN("cannot open the parquet file", K(ret), K(options_.path_));
    return ret;
  }
  /* the footer is the last part of the file, it is preceded by its length */
  char tail[8];
  int64_t read_size = 0;
  ret = source_.pread(tail, (int64_t)sizeof(tail), file_size_ - 8, read_size);
  if (OB_FAIL(ret) || read_size != (int64_t)sizeof(tail)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot read the tail of the parquet file", K(ret), K(file_size_));
    source_.close();
    return ret;
  }
  if (0 != memcmp(tail + 4, "PAR1", 4)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the parquet file does not end with the PAR1 magic", K(ret));
    source_.close();
    return ret;
  }
  uint32_t footer_size = 0;
  for (int32_t i = 0; i < 4; i++) {
    footer_size |= ((uint32_t)(uint8_t)tail[i]) << (8 * i);
  }
  const int64_t footer_offset = file_size_ - 8 - (int64_t)footer_size;
  if (footer_offset < 4 || (int64_t)footer_size > file_size_ - 12) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the footer of the parquet file is truncated", K(ret), K(footer_size));
    source_.close();
    return ret;
  }
  std::vector<char> footer((size_t)footer_size);
  read_size = 0;
  ret = source_.pread(&footer[0], (int64_t)footer.size(), footer_offset, read_size);
  if (OB_FAIL(ret) || read_size != (int64_t)footer.size()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot read the footer of the parquet file", K(ret), K(footer_size));
    source_.close();
    return ret;
  }
  char magic[4];
  read_size = 0;
  ret = source_.pread(magic, (int64_t)sizeof(magic), 0, read_size);
  if (OB_FAIL(ret) || read_size != (int64_t)sizeof(magic) || 0 != memcmp(magic, "PAR1", 4)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the parquet file does not start with the PAR1 magic", K(ret));
    source_.close();
    return ret;
  }
  std::string error;
  if (!ob_parquet_core::ob_pq_parse_file_metadata(&footer[0], (int64_t)footer.size(), meta_, error)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot parse the metadata of the parquet file", K(ret), KCSTRING(error.c_str()));
    source_.close();
    return ret;
  }
  leaves_.clear();
  if (!ob_parquet_core::ob_pq_build_leaves(meta_, leaves_, error)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot build the schema of the parquet file", K(ret), KCSTRING(error.c_str()));
    source_.close();
    return ret;
  }
  data_end_ = footer_offset;
  return ret;
}

int ObParquetFileScanReader::Impl::build_projection()
{
  int ret = OB_SUCCESS;
  projection_.clear();
  column_types_.clear();
  for (int64_t i = 0; i < columns_.count(); i++) {
    const ObFileColumnDef &column = columns_.at(i);
    int64_t leaf_index = -1;
    for (int64_t j = 0; j < (int64_t)leaves_.size(); j++) {
      if (0 == column.name_.case_compare(leaves_[(size_t)j].name_.c_str())) {
        leaf_index = j;
        break;
      }
    }
    if (leaf_index < 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the column does not exist in the parquet file", K(ret), K(column.name_));
      return ret;
    }
    const common::ObObjType type = ob_parquet_sql_type(leaves_[(size_t)leaf_index]);
    if (common::ObUnknownType == type) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unsupported parquet type", K(ret), K(column.name_), K(leaves_[(size_t)leaf_index].type_));
      return ret;
    }
    projection_.push_back(leaf_index);
    column_types_.push_back(type);
  }
  values_.resize((size_t)columns_.count());
  std::string error;
  if (!reader_.init(source_, meta_, leaves_, projection_, data_end_, error)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot initialize the parquet reader", K(ret), KCSTRING(error.c_str()));
    return ret;
  }
  return ret;
}

int ObParquetFileScanReader::Impl::next_row()
{
  int ret = OB_SUCCESS;
  if (!opened_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the parquet file is not open", K(ret), K(options_.path_));
    return ret;
  }
  /* the strings of the previous row are not needed any more */
  row_arena_.reset();
  std::string error;
  const int64_t count = (int64_t)values_.size();
  const int result = reader_.next_row(count > 0 ? &values_[0] : NULL, count, error);
  if (result < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot decode the next row of the parquet file", K(ret), KCSTRING(error.c_str()));
    return ret;
  } else if (0 == result) {
    ret = OB_ITER_END;
    return ret;
  }
  return ret;
}

int ObParquetFileScanReader::Impl::close()
{
  row_arena_.reset();
  source_.close();
  opened_ = false;
  return OB_SUCCESS;
}

int ObParquetFileScanReader::Impl::infer_schema(common::ObIAllocator &allocator,
                                                 common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  if (leaves_.empty()) {
    if (OB_FAIL(read_footer())) {
      LOG_WARN("cannot read the footer of the parquet file", K(ret), K(options_.path_));
      return ret;
    }
  }
  columns.reset();
  for (int64_t i = 0; i < (int64_t)leaves_.size(); i++) {
    if (i >= OB_FILE_TABLE_MAX_COLUMN_CNT) {
      char msg[256];
      snprintf(msg, sizeof(msg),
               "the parquet file has more than %ld columns, "
               "the greatest number of the columns of a file is %ld",
               static_cast<long>(OB_FILE_TABLE_MAX_COLUMN_CNT),
               static_cast<long>(OB_FILE_TABLE_MAX_COLUMN_CNT));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, msg);
      ret = OB_NOT_SUPPORTED;
      return ret;
    }
    const ob_parquet_core::ObPqLeaf &leaf = leaves_[(size_t)i];
    const common::ObObjType type = ob_parquet_sql_type(leaf);
    if (common::ObUnknownType == type) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unsupported parquet type", K(ret), K(leaf.name_.c_str()), K(leaf.type_));
      return ret;
    }
    ObFileColumnDef column;
    const int64_t name_len = (int64_t)leaf.name_.size();
    char *name = (char *)allocator.alloc(name_len + 1);
    if (OB_ISNULL(name)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("cannot allocate the name of a column", K(ret));
      return ret;
    }
    if (name_len > 0) {
      memcpy(name, leaf.name_.c_str(), (size_t)name_len);
    }
    name[name_len] = '\0';
    column.name_ = common::ObString(name_len, name);
    column.type_ = type;
    column.nullable_ = leaf.optional_ || leaf.max_def_level_ > 0;
    column.precision_ = 0;
    column.scale_ = 0;
    if (OB_FAIL(columns.push_back(column))) {
      LOG_WARN("cannot add a column to the schema", K(ret));
      return ret;
    }
  }
  return ret;
}

int ObParquetFileScanReader::Impl::set_value(common::ObObj &obj,
                                             const ob_parquet_core::ObPqResolvedValue &value,
                                             const common::ObObjType type)
{
  int ret = OB_SUCCESS;
  if (value.is_null_) {
    obj.set_null();
  } else if (common::ObVarcharType == type) {
    const int64_t len = value.bin_len_;
    char *data = (char *)row_arena_.alloc(len > 0 ? len : 1);
    if (OB_ISNULL(data)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      if (len > 0 && OB_NOT_NULL(value.bin_data_)) {
        memcpy(data, value.bin_data_, (size_t)len);
      }
      obj.set_string(common::ObVarcharType, data, len);
      obj.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
    }
  } else if (common::ObTinyIntType == type) {
    obj.set_bool(value.bool_value_);
  } else if (common::ObIntType == type) {
    obj.set_type(common::ObIntType);
    obj.set_int_value(value.int_value_);
  } else if (common::ObDoubleType == type) {
    obj.set_type(common::ObDoubleType);
    obj.set_double_value(value.double_value_);
  } else if (common::ObDateType == type) {
    char text[32];
    int32_t len = 0;
    if (!ob_parquet_core::ob_pq_format_date(value.int_value_, text, (int32_t)sizeof(text), len)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot format the date of the parquet file", K(ret), K(value.int_value_));
    } else {
      ret = ObFileScanUtils::set_obj_from_text(obj, common::ObDateType, text, len);
    }
  } else if (common::ObDateTimeType == type) {
    char text[64];
    int32_t len = 0;
    if (!ob_parquet_core::ob_pq_format_timestamp(
            value.int_value_, value.time_unit_, text, (int32_t)sizeof(text), len)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot format the timestamp of the parquet file", K(ret), K(value.int_value_));
    } else {
      ret = ObFileScanUtils::set_obj_from_text(obj, common::ObDateTimeType, text, len);
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupported parquet column type", K(ret), K(type));
  }
  return ret;
}

ObParquetFileScanReader::ObParquetFileScanReader(const ObFileScanOptions &options,
                                                 const common::ObIArray<ObFileColumnDef> &columns)
    : impl_(NULL)
{
  impl_ = OB_NEW(Impl, ObModIds::OB_MODULE_PAGE_ALLOCATOR, options, columns);
}

ObParquetFileScanReader::~ObParquetFileScanReader()
{
  OB_DELETE(Impl, ObModIds::OB_MODULE_PAGE_ALLOCATOR, impl_);
}

int ObParquetFileScanReader::open()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(impl_)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("cannot create the parquet reader", K(ret));
  } else if (OB_FAIL(impl_->open())) {
    LOG_WARN("cannot open the parquet file", K(ret), K(impl_->get_path()));
  }
  return ret;
}

int ObParquetFileScanReader::next_row()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(impl_)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("cannot create the parquet reader", K(ret));
    return ret;
  }
  row_values_.reuse();
  ret = impl_->next_row();
  if (OB_ITER_END == ret) {
    /* the end of the file is not an error */
    return ret;
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("cannot read the next row of the parquet file", K(ret));
    return ret;
  }
  const int64_t count = impl_->get_value_count();
  for (int64_t i = 0; i < count; i++) {
    common::ObObj obj;
    if (OB_FAIL(impl_->set_value(obj, impl_->get_value(i), impl_->get_column_type(i)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot convert the value of the parquet file", K(ret), K(i));
      break;
    }
    if (OB_FAIL(row_values_.push_back(obj))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cannot add a value to the row", K(ret), K(i));
      break;
    }
  }
  return ret;
}

int ObParquetFileScanReader::close()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(impl_)) {
    ret = impl_->close();
  }
  row_values_.reuse();
  return ret;
}

int ObParquetFileScanReader::infer_schema(common::ObIAllocator &allocator,
                                          common::ObIArray<ObFileColumnDef> &columns)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(impl_)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("cannot create the parquet reader", K(ret));
  } else if (OB_FAIL(impl_->infer_schema(allocator, columns))) {
    LOG_WARN("cannot infer the schema of the parquet file", K(ret), K(impl_->get_path()));
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
