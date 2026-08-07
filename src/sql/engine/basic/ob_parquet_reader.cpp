/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */
#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/basic/ob_parquet_reader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/oblog/ob_log.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace sql
{
namespace
{
enum CompactType : uint8_t
{
  CT_STOP = 0, CT_TRUE = 1, CT_FALSE = 2, CT_BYTE = 3, CT_I16 = 4,
  CT_I32 = 5, CT_I64 = 6, CT_DOUBLE = 7, CT_BINARY = 8, CT_LIST = 9,
  CT_SET = 10, CT_MAP = 11, CT_STRUCT = 12
};

enum ParquetType : int32_t
{
  PT_BOOLEAN = 0, PT_INT32 = 1, PT_INT64 = 2, PT_INT96 = 3,
  PT_FLOAT = 4, PT_DOUBLE = 5, PT_BYTE_ARRAY = 6, PT_FIXED_LEN_BYTE_ARRAY = 7
};
enum ParquetEncoding : int32_t
{
  PE_PLAIN = 0, PE_PLAIN_DICTIONARY = 2, PE_RLE = 3, PE_BIT_PACKED = 4,
  PE_RLE_DICTIONARY = 8
};
enum ParquetCodec : int32_t { PC_UNCOMPRESSED = 0, PC_SNAPPY = 1 };
enum ParquetPageType : int32_t
{
  PP_DATA_PAGE = 0, PP_INDEX_PAGE = 1, PP_DICTIONARY_PAGE = 2, PP_DATA_PAGE_V2 = 3
};
enum LogicalAnnotation : int32_t
{
  LA_NONE = 0, LA_STRING, LA_DATE, LA_TIMESTAMP_MILLIS, LA_TIMESTAMP_MICROS,
  LA_TIMESTAMP_NANOS, LA_INTEGER, LA_UNSUPPORTED
};

struct CompactCursor
{
  CompactCursor(const uint8_t *data, const size_t size) : data_(data), size_(size), pos_(0) {}
  bool read_byte(uint8_t &value)
  {
    const bool ok = pos_ < size_;
    if (ok) value = data_[pos_++];
    return ok;
  }
  bool read_var_uint(uint64_t &value)
  {
    value = 0;
    int shift = 0;
    uint8_t byte = 0;
    bool ok = true;
    do {
      ok = shift < 64 && read_byte(byte);
      if (ok) {
        value |= static_cast<uint64_t>(byte & 0x7f) << shift;
        shift += 7;
      }
    } while (ok && 0 != (byte & 0x80));
    return ok;
  }
  bool read_i64(int64_t &value)
  {
    uint64_t encoded = 0;
    const bool ok = read_var_uint(encoded);
    if (ok) value = static_cast<int64_t>((encoded >> 1) ^ (~(encoded & 1) + 1));
    return ok;
  }
  bool read_i32(int32_t &value)
  {
    int64_t wide = 0;
    const bool ok = read_i64(wide) && wide >= std::numeric_limits<int32_t>::min()
                                      && wide <= std::numeric_limits<int32_t>::max();
    if (ok) value = static_cast<int32_t>(wide);
    return ok;
  }
  bool read_binary(std::string &value)
  {
    uint64_t length = 0;
    const bool ok = read_var_uint(length) && length <= size_ - pos_;
    if (ok) {
      value.assign(reinterpret_cast<const char *>(data_ + pos_), static_cast<size_t>(length));
      pos_ += static_cast<size_t>(length);
    }
    return ok;
  }
  bool read_field(int16_t &last_id, int16_t &field_id, uint8_t &type)
  {
    uint8_t header = 0;
    bool ok = read_byte(header);
    type = header & 0x0f;
    if (ok && CT_STOP != type) {
      const int16_t delta = static_cast<int16_t>(header >> 4);
      if (0 != delta) {
        field_id = last_id + delta;
      } else {
        int64_t id = 0;
        ok = read_i64(id) && id > 0 && id <= std::numeric_limits<int16_t>::max();
        field_id = static_cast<int16_t>(id);
      }
      if (ok) last_id = field_id;
    }
    return ok;
  }
  bool read_list_header(uint8_t &element_type, uint64_t &size)
  {
    uint8_t header = 0;
    bool ok = read_byte(header);
    element_type = header & 0x0f;
    size = header >> 4;
    if (ok && 15 == size) ok = read_var_uint(size);
    return ok;
  }
  bool skip(uint8_t type)
  {
    bool ok = true;
    uint8_t byte = 0;
    uint64_t size = 0;
    switch (type) {
      case CT_TRUE: case CT_FALSE: break;
      case CT_BYTE: ok = read_byte(byte); break;
      case CT_I16: case CT_I32: case CT_I64: ok = read_var_uint(size); break;
      case CT_DOUBLE:
        ok = size_ - pos_ >= sizeof(double); if (ok) pos_ += sizeof(double); break;
      case CT_BINARY:
        ok = read_var_uint(size) && size <= size_ - pos_;
        if (ok) pos_ += static_cast<size_t>(size);
        break;
      case CT_LIST: case CT_SET: {
        uint8_t element_type = 0;
        ok = read_list_header(element_type, size);
        for (uint64_t i = 0; ok && i < size; ++i) ok = skip(element_type);
        break;
      }
      case CT_MAP: {
        ok = read_var_uint(size);
        uint8_t types = 0;
        if (ok && size > 0) ok = read_byte(types);
        for (uint64_t i = 0; ok && i < size; ++i) {
          ok = skip(types >> 4) && skip(types & 0x0f);
        }
        break;
      }
      case CT_STRUCT: {
        int16_t last = 0, id = 0;
        uint8_t field_type = 0;
        do {
          ok = read_field(last, id, field_type);
          if (ok && CT_STOP != field_type) ok = skip(field_type);
        } while (ok && CT_STOP != field_type);
        break;
      }
      default: ok = false; break;
    }
    return ok;
  }
  const uint8_t *data_;
  size_t size_;
  size_t pos_;
};

bool read_integer(CompactCursor &cursor, const uint8_t type, int64_t &value)
{
  return (CT_I16 == type || CT_I32 == type || CT_I64 == type) && cursor.read_i64(value);
}

struct SchemaElement
{
  SchemaElement() : physical_type_(-1), type_length_(0), repetition_(0), name_(),
                    num_children_(0), converted_type_(-1), scale_(0), precision_(0),
                    logical_annotation_(LA_NONE), integer_bit_width_(0), integer_signed_(true) {}
  int32_t physical_type_;
  int32_t type_length_;
  int32_t repetition_;
  std::string name_;
  int32_t num_children_;
  int32_t converted_type_;
  int32_t scale_;
  int32_t precision_;
  int32_t logical_annotation_;
  int32_t integer_bit_width_;
  bool integer_signed_;
};

bool parse_time_unit(CompactCursor &cursor, int32_t &annotation)
{
  bool ok = true, found = false;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    if (!ok || CT_STOP == type) {
    } else if (CT_STRUCT == type && id >= 1 && id <= 3) {
      ok = cursor.skip(type);
      if (ok) {
        annotation = 1 == id ? LA_TIMESTAMP_MILLIS
                   : 2 == id ? LA_TIMESTAMP_MICROS : LA_TIMESTAMP_NANOS;
        found = true;
      }
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && found;
}

bool parse_timestamp_type(CompactCursor &cursor, int32_t &annotation)
{
  bool ok = true, found_unit = false;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    if (!ok || CT_STOP == type) {
    } else if (2 == id && CT_STRUCT == type) {
      ok = parse_time_unit(cursor, annotation);
      found_unit = ok;
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && found_unit;
}

bool parse_integer_type(CompactCursor &cursor, SchemaElement &element)
{
  bool ok = true, found_width = false, found_signed = false;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    if (!ok || CT_STOP == type) {
    } else if (1 == id && CT_BYTE == type) {
      uint8_t width = 0;
      ok = cursor.read_byte(width);
      if (ok) { element.integer_bit_width_ = width; found_width = true; }
    } else if (2 == id && (CT_TRUE == type || CT_FALSE == type)) {
      element.integer_signed_ = CT_TRUE == type;
      found_signed = true;
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && found_width && found_signed;
}

bool parse_logical_type(CompactCursor &cursor, SchemaElement &element)
{
  bool ok = true, found = false;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    if (!ok || CT_STOP == type) {
    } else if (CT_STRUCT != type) {
      ok = cursor.skip(type);
    } else if (1 == id || 6 == id) {
      ok = cursor.skip(type);
      if (ok) {
        element.logical_annotation_ = 1 == id ? LA_STRING : LA_DATE;
        found = true;
      }
    } else if (8 == id) {
      ok = parse_timestamp_type(cursor, element.logical_annotation_);
      found = ok;
    } else if (9 == id) {
      element.logical_annotation_ = LA_INTEGER;
      ok = parse_integer_type(cursor, element);
      found = ok;
    } else {
      element.logical_annotation_ = LA_UNSUPPORTED;
      ok = cursor.skip(type);
      found = ok;
    }
  } while (ok && CT_STOP != type);
  return ok && found;
}

struct ColumnMeta
{
  ColumnMeta() : physical_type_(-1), codec_(PC_UNCOMPRESSED), num_values_(0),
                 total_compressed_size_(0), data_page_offset_(-1), dictionary_page_offset_(-1) {}
  int32_t physical_type_;
  std::vector<std::string> path_;
  int32_t codec_;
  int64_t num_values_;
  int64_t total_compressed_size_;
  int64_t data_page_offset_;
  int64_t dictionary_page_offset_;
};

struct RowGroupMeta
{
  RowGroupMeta() : columns_(), num_rows_(0) {}
  std::vector<ColumnMeta> columns_;
  int64_t num_rows_;
};

struct FileMeta
{
  std::vector<SchemaElement> schema_;
  std::vector<RowGroupMeta> row_groups_;
  int64_t num_rows_ = 0;
};

bool parse_schema_element(CompactCursor &cursor, SchemaElement &element)
{
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (1 == id && read_integer(cursor, type, value)) {
      element.physical_type_ = static_cast<int32_t>(value);
    } else if (2 == id && read_integer(cursor, type, value)) {
      element.type_length_ = static_cast<int32_t>(value);
    } else if (3 == id && read_integer(cursor, type, value)) {
      element.repetition_ = static_cast<int32_t>(value);
    } else if (4 == id && CT_BINARY == type) {
      ok = cursor.read_binary(element.name_);
    } else if (5 == id && read_integer(cursor, type, value)) {
      element.num_children_ = static_cast<int32_t>(value);
    } else if (6 == id && read_integer(cursor, type, value)) {
      element.converted_type_ = static_cast<int32_t>(value);
    } else if (7 == id && read_integer(cursor, type, value)) {
      element.scale_ = static_cast<int32_t>(value);
    } else if (8 == id && read_integer(cursor, type, value)) {
      element.precision_ = static_cast<int32_t>(value);
    } else if (10 == id && CT_STRUCT == type) {
      ok = parse_logical_type(cursor, element);
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && !element.name_.empty();
}

bool parse_column_meta(CompactCursor &cursor, ColumnMeta &meta)
{
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (1 == id && read_integer(cursor, type, value)) {
      meta.physical_type_ = static_cast<int32_t>(value);
    } else if (3 == id && CT_LIST == type) {
      uint8_t element_type = 0; uint64_t count = 0;
      ok = cursor.read_list_header(element_type, count) && CT_BINARY == element_type;
      for (uint64_t i = 0; ok && i < count; ++i) {
        std::string path;
        ok = cursor.read_binary(path);
        if (ok) meta.path_.push_back(path);
      }
    } else if (4 == id && read_integer(cursor, type, value)) {
      meta.codec_ = static_cast<int32_t>(value);
    } else if (5 == id && read_integer(cursor, type, value)) {
      meta.num_values_ = value;
    } else if (7 == id && read_integer(cursor, type, value)) {
      meta.total_compressed_size_ = value;
    } else if (9 == id && read_integer(cursor, type, value)) {
      meta.data_page_offset_ = value;
    } else if (11 == id && read_integer(cursor, type, value)) {
      meta.dictionary_page_offset_ = value;
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && meta.physical_type_ >= 0 && meta.num_values_ >= 0
            && meta.data_page_offset_ >= 0 && !meta.path_.empty();
}

bool parse_column_chunk(CompactCursor &cursor, ColumnMeta &meta)
{
  bool ok = true, found = false;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    if (!ok || CT_STOP == type) {
    } else if (3 == id && CT_STRUCT == type) {
      ok = parse_column_meta(cursor, meta); found = ok;
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && found;
}

bool parse_row_group(CompactCursor &cursor, RowGroupMeta &group)
{
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (1 == id && CT_LIST == type) {
      uint8_t element_type = 0; uint64_t count = 0;
      ok = cursor.read_list_header(element_type, count) && CT_STRUCT == element_type;
      for (uint64_t i = 0; ok && i < count; ++i) {
        ColumnMeta meta;
        ok = parse_column_chunk(cursor, meta);
        if (ok) group.columns_.push_back(meta);
      }
    } else if (3 == id && read_integer(cursor, type, value)) {
      group.num_rows_ = value;
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && group.num_rows_ >= 0 && !group.columns_.empty();
}

bool parse_file_meta(const uint8_t *data, const size_t size, FileMeta &meta)
{
  CompactCursor cursor(data, size);
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (2 == id && CT_LIST == type) {
      uint8_t element_type = 0; uint64_t count = 0;
      ok = cursor.read_list_header(element_type, count) && CT_STRUCT == element_type && count <= 1025;
      for (uint64_t i = 0; ok && i < count; ++i) {
        SchemaElement element;
        ok = parse_schema_element(cursor, element);
        if (ok) meta.schema_.push_back(element);
      }
    } else if (3 == id && read_integer(cursor, type, value)) {
      meta.num_rows_ = value;
    } else if (4 == id && CT_LIST == type) {
      uint8_t element_type = 0; uint64_t count = 0;
      ok = cursor.read_list_header(element_type, count) && CT_STRUCT == element_type && count <= 1000000;
      for (uint64_t i = 0; ok && i < count; ++i) {
        RowGroupMeta group;
        ok = parse_row_group(cursor, group);
        if (ok) meta.row_groups_.push_back(group);
      }
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && meta.num_rows_ >= 0 && meta.schema_.size() >= 2;
}

uint32_t load_u32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
       | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}
uint64_t load_u64(const uint8_t *data)
{
  return static_cast<uint64_t>(load_u32(data)) | (static_cast<uint64_t>(load_u32(data + 4)) << 32);
}

int pread_all(const int fd, const int64_t offset, uint8_t *data, const size_t size)
{
  int ret = OB_SUCCESS;
  size_t done = 0;
  while (OB_SUCC(ret) && done < size) {
    const ssize_t count = pread(fd, data + done, size - done, offset + done);
    if (count > 0) done += static_cast<size_t>(count);
    else ret = OB_IO_ERROR;
  }
  return ret;
}

int read_file_metadata(const int fd, const int64_t file_size, FileMeta &meta)
{
  int ret = OB_SUCCESS;
  uint8_t magic[4];
  uint8_t trailer[8];
  if (file_size < 12 || OB_FAIL(pread_all(fd, 0, magic, sizeof(magic)))
      || OB_FAIL(pread_all(fd, file_size - 8, trailer, sizeof(trailer)))) {
    if (OB_SUCC(ret)) ret = OB_INVALID_DATA;
  } else if (0 != memcmp(magic, "PAR1", 4) || 0 != memcmp(trailer + 4, "PAR1", 4)) {
    ret = OB_INVALID_DATA;
  } else {
    const uint32_t metadata_size = load_u32(trailer);
    if (metadata_size > 64U * 1024U * 1024U
        || static_cast<int64_t>(metadata_size) > file_size - 12) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      std::vector<uint8_t> bytes(metadata_size);
      if (OB_FAIL(pread_all(fd, file_size - 8 - metadata_size, bytes.data(), bytes.size()))) {
        LOG_WARN("failed to read parquet footer", K(ret));
      } else if (!parse_file_meta(bytes.data(), bytes.size(), meta)) {
        ret = OB_INVALID_DATA;
      }
    }
  }
  return ret;
}

bool ascii_equal_ci(const std::string &left, const std::string &right)
{
  bool equal = left.size() == right.size();
  for (size_t i = 0; equal && i < left.size(); ++i) {
    equal = std::tolower(static_cast<unsigned char>(left[i]))
         == std::tolower(static_cast<unsigned char>(right[i]));
  }
  return equal;
}

std::string unique_name(const std::string &source, const std::vector<ObFileColumnSchema> &columns)
{
  std::string base = source.empty() ? "_c" + std::to_string(columns.size() + 1) : source;
  std::string result = base;
  for (int64_t suffix = 2;; ++suffix) {
    bool duplicate = false;
    for (size_t i = 0; !duplicate && i < columns.size(); ++i) {
      duplicate = ascii_equal_ci(result, columns[i].column_name_);
    }
    if (!duplicate) break;
    result = base + "__" + std::to_string(suffix);
  }
  return result;
}

int map_schema(const FileMeta &meta, std::vector<ObFileColumnSchema> &columns)
{
  int ret = OB_SUCCESS;
  columns.clear();
  if (meta.schema_.empty() || meta.schema_[0].num_children_ <= 0
      || meta.schema_[0].num_children_ != static_cast<int32_t>(meta.schema_.size() - 1)) {
    ret = OB_NOT_SUPPORTED; // MVP supports a flat primitive schema.
  }
  for (size_t i = 1; OB_SUCC(ret) && i < meta.schema_.size(); ++i) {
    const SchemaElement &element = meta.schema_[i];
    ObFileColumnSchema column;
    if (element.num_children_ > 0 || 2 == element.repetition_) {
      ret = OB_NOT_SUPPORTED;
    } else if (LA_UNSUPPORTED == element.logical_annotation_
               || (LA_INTEGER == element.logical_annotation_
                   && !element.integer_signed_ && 64 == element.integer_bit_width_)) {
      ret = OB_NOT_SUPPORTED;
    } else if (PT_BOOLEAN == element.physical_type_) {
      column.type_ = ObFileColumnType::BOOLEAN;
      column.source_type_name_ = "BOOLEAN";
    } else if (PT_INT32 == element.physical_type_) {
      column.type_ = (6 == element.converted_type_ || LA_DATE == element.logical_annotation_)
                   ? ObFileColumnType::DATE : ObFileColumnType::BIGINT;
      column.source_type_name_ = ObFileColumnType::DATE == column.type_ ? "INT32/DATE" : "INT32";
    } else if (PT_INT64 == element.physical_type_) {
      column.type_ = (9 == element.converted_type_ || 10 == element.converted_type_
                      || LA_TIMESTAMP_MILLIS == element.logical_annotation_
                      || LA_TIMESTAMP_MICROS == element.logical_annotation_
                      || LA_TIMESTAMP_NANOS == element.logical_annotation_)
                   ? ObFileColumnType::DATETIME : ObFileColumnType::BIGINT;
      if (LA_TIMESTAMP_NANOS == element.logical_annotation_) {
        column.source_type_name_ = "INT64/TIMESTAMP_NANOS";
      } else if (10 == element.converted_type_ || LA_TIMESTAMP_MICROS == element.logical_annotation_) {
        column.source_type_name_ = "INT64/TIMESTAMP_MICROS";
      } else if (9 == element.converted_type_ || LA_TIMESTAMP_MILLIS == element.logical_annotation_) {
        column.source_type_name_ = "INT64/TIMESTAMP_MILLIS";
      } else {
        column.source_type_name_ = "INT64";
      }
    } else if (PT_INT96 == element.physical_type_) {
      column.type_ = ObFileColumnType::DATETIME;
      column.source_type_name_ = "INT96";
    } else if (PT_FLOAT == element.physical_type_ || PT_DOUBLE == element.physical_type_) {
      column.type_ = ObFileColumnType::DOUBLE;
      column.source_type_name_ = PT_FLOAT == element.physical_type_ ? "FLOAT" : "DOUBLE";
    } else if (PT_BYTE_ARRAY == element.physical_type_
               || PT_FIXED_LEN_BYTE_ARRAY == element.physical_type_) {
      column.type_ = ObFileColumnType::VARCHAR;
      column.max_length_ = std::max<int32_t>(element.type_length_, 65535);
      if (PT_FIXED_LEN_BYTE_ARRAY == element.physical_type_) {
        column.source_type_name_ = "FIXED_LEN_BYTE_ARRAY(" + std::to_string(element.type_length_) + ")";
      } else if (0 == element.converted_type_ || LA_STRING == element.logical_annotation_) {
        column.source_type_name_ = "BYTE_ARRAY/UTF8";
      } else {
        column.source_type_name_ = "BYTE_ARRAY";
      }
    } else {
      ret = OB_NOT_SUPPORTED;
    }
    if (OB_SUCC(ret)) {
      column.source_name_ = element.name_;
      column.column_name_ = unique_name(element.name_, columns);
      column.nullable_ = 1 == element.repetition_;
      columns.push_back(column);
    }
  }
  for (size_t group_idx = 0; OB_SUCC(ret) && group_idx < meta.row_groups_.size(); ++group_idx) {
    const RowGroupMeta &group = meta.row_groups_[group_idx];
    if (group.columns_.size() != columns.size()
        || group.num_rows_ > 1024L * 1024L
        || (!columns.empty()
            && group.num_rows_ > 1024L * 1024L / static_cast<int64_t>(columns.size()))) {
      ret = OB_NOT_SUPPORTED;
    } else {
      for (size_t column_idx = 0; OB_SUCC(ret) && column_idx < columns.size(); ++column_idx) {
        const ColumnMeta &chunk = group.columns_[column_idx];
        const SchemaElement &schema = meta.schema_[column_idx + 1];
        if (chunk.physical_type_ != schema.physical_type_
            || chunk.path_.size() != 1 || chunk.path_[0] != schema.name_) {
          ret = OB_INVALID_DATA;
        }
      }
    }
  }
  int64_t total_rows = 0;
  for (size_t i = 0; OB_SUCC(ret) && i < meta.row_groups_.size(); ++i) {
    if (meta.row_groups_[i].num_rows_ > meta.num_rows_ - total_rows) ret = OB_INVALID_DATA;
    else total_rows += meta.row_groups_[i].num_rows_;
  }
  if (OB_SUCC(ret) && total_rows != meta.num_rows_) ret = OB_INVALID_DATA;
  return ret;
}

int open_verified(const std::string &path, const uint64_t expected_device,
                  const uint64_t expected_inode, const int64_t expected_size,
                  const int64_t expected_mtime_ns, int &fd)
{
  int ret = OB_SUCCESS;
  fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  struct stat st;
  if (fd < 0 || 0 != fstat(fd, &st) || !S_ISREG(st.st_mode)) {
    if (fd >= 0) ::close(fd);
    fd = -1;
    ret = OB_FILE_NOT_OPENED;
  } else {
    const int64_t mtime_ns = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000L
                           + static_cast<int64_t>(st.st_mtim.tv_nsec);
    if ((0 != expected_device && static_cast<uint64_t>(st.st_dev) != expected_device)
        || (0 != expected_inode && static_cast<uint64_t>(st.st_ino) != expected_inode)
        || (expected_size >= 0 && static_cast<int64_t>(st.st_size) != expected_size)
        || (expected_mtime_ns >= 0 && mtime_ns != expected_mtime_ns)) {
      ::close(fd); fd = -1; ret = OB_SCHEMA_EAGAIN;
    }
  }
  return ret;
}

struct PageHeader
{
  PageHeader()
    : type_(-1), uncompressed_size_(-1), compressed_size_(-1), num_values_(-1),
      encoding_(-1), definition_encoding_(-1), repetition_encoding_(-1),
      num_nulls_(0), num_rows_(-1), definition_length_(0), repetition_length_(0),
      is_compressed_(true)
  {}
  int32_t type_;
  int32_t uncompressed_size_;
  int32_t compressed_size_;
  int32_t num_values_;
  int32_t encoding_;
  int32_t definition_encoding_;
  int32_t repetition_encoding_;
  int32_t num_nulls_;
  int32_t num_rows_;
  int32_t definition_length_;
  int32_t repetition_length_;
  bool is_compressed_;
};

bool parse_data_page_header(CompactCursor &cursor, PageHeader &header)
{
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (1 == id && read_integer(cursor, type, value)) {
      header.num_values_ = static_cast<int32_t>(value);
    } else if (2 == id && read_integer(cursor, type, value)) {
      header.encoding_ = static_cast<int32_t>(value);
    } else if (3 == id && read_integer(cursor, type, value)) {
      header.definition_encoding_ = static_cast<int32_t>(value);
    } else if (4 == id && read_integer(cursor, type, value)) {
      header.repetition_encoding_ = static_cast<int32_t>(value);
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && header.num_values_ >= 0 && header.encoding_ >= 0;
}

bool parse_dictionary_page_header(CompactCursor &cursor, PageHeader &header)
{
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (1 == id && read_integer(cursor, type, value)) {
      header.num_values_ = static_cast<int32_t>(value);
    } else if (2 == id && read_integer(cursor, type, value)) {
      header.encoding_ = static_cast<int32_t>(value);
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && header.num_values_ >= 0 && header.encoding_ >= 0;
}

bool parse_data_page_v2_header(CompactCursor &cursor, PageHeader &header)
{
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (1 == id && read_integer(cursor, type, value)) {
      header.num_values_ = static_cast<int32_t>(value);
    } else if (2 == id && read_integer(cursor, type, value)) {
      header.num_nulls_ = static_cast<int32_t>(value);
    } else if (3 == id && read_integer(cursor, type, value)) {
      header.num_rows_ = static_cast<int32_t>(value);
    } else if (4 == id && read_integer(cursor, type, value)) {
      header.encoding_ = static_cast<int32_t>(value);
    } else if (5 == id && read_integer(cursor, type, value)) {
      header.definition_length_ = static_cast<int32_t>(value);
    } else if (6 == id && read_integer(cursor, type, value)) {
      header.repetition_length_ = static_cast<int32_t>(value);
    } else if (7 == id && (CT_TRUE == type || CT_FALSE == type)) {
      header.is_compressed_ = CT_TRUE == type;
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  return ok && header.num_values_ >= 0 && header.num_rows_ >= 0 && header.encoding_ >= 0
         && header.definition_length_ >= 0 && header.repetition_length_ >= 0;
}

bool parse_page_header(const uint8_t *data, const size_t size,
                       PageHeader &header, size_t &consumed)
{
  CompactCursor cursor(data, size);
  bool ok = true;
  int16_t last = 0, id = 0;
  uint8_t type = 0;
  do {
    ok = cursor.read_field(last, id, type);
    int64_t value = 0;
    if (!ok || CT_STOP == type) {
    } else if (1 == id && read_integer(cursor, type, value)) {
      header.type_ = static_cast<int32_t>(value);
    } else if (2 == id && read_integer(cursor, type, value)) {
      header.uncompressed_size_ = static_cast<int32_t>(value);
    } else if (3 == id && read_integer(cursor, type, value)) {
      header.compressed_size_ = static_cast<int32_t>(value);
    } else if (5 == id && CT_STRUCT == type) {
      ok = parse_data_page_header(cursor, header);
    } else if (7 == id && CT_STRUCT == type) {
      ok = parse_dictionary_page_header(cursor, header);
    } else if (8 == id && CT_STRUCT == type) {
      ok = parse_data_page_v2_header(cursor, header);
    } else {
      ok = cursor.skip(type);
    }
  } while (ok && CT_STOP != type);
  consumed = cursor.pos_;
  return ok && header.type_ >= 0 && header.uncompressed_size_ >= 0
         && header.compressed_size_ >= 0;
}

bool read_var_uint(const uint8_t *data, const size_t size, size_t &pos, uint64_t &value)
{
  value = 0;
  int shift = 0;
  uint8_t byte = 0;
  bool ok = true;
  do {
    ok = pos < size && shift < 64;
    if (ok) {
      byte = data[pos++];
      value |= static_cast<uint64_t>(byte & 0x7f) << shift;
      shift += 7;
    }
  } while (ok && 0 != (byte & 0x80));
  return ok;
}

int snappy_decompress(const uint8_t *data, const size_t size,
                      const size_t expected_size, std::vector<uint8_t> &output)
{
  int ret = OB_SUCCESS;
  size_t pos = 0;
  uint64_t declared_size = 0;
  output.clear();
  if (!read_var_uint(data, size, pos, declared_size)
      || declared_size != expected_size || declared_size > 64U * 1024U * 1024U) {
    ret = OB_INVALID_DATA;
  } else {
    output.reserve(static_cast<size_t>(declared_size));
  }
  while (OB_SUCC(ret) && pos < size && output.size() < declared_size) {
    const uint8_t tag = data[pos++];
    const uint8_t kind = tag & 0x03;
    size_t length = 0;
    size_t offset = 0;
    if (0 == kind) {
      const uint8_t encoded = tag >> 2;
      if (encoded < 60) {
        length = static_cast<size_t>(encoded) + 1;
      } else {
        const size_t length_bytes = static_cast<size_t>(encoded) - 59;
        if (length_bytes > 4 || length_bytes > size - pos) {
          ret = OB_INVALID_DATA;
        } else {
          uint32_t length_minus_one = 0;
          for (size_t i = 0; i < length_bytes; ++i) {
            length_minus_one |= static_cast<uint32_t>(data[pos++]) << (8 * i);
          }
          length = static_cast<size_t>(length_minus_one) + 1;
        }
      }
      if (OB_SUCC(ret) && (length > size - pos || length > declared_size - output.size())) {
        ret = OB_INVALID_DATA;
      } else if (OB_SUCC(ret)) {
        output.insert(output.end(), data + pos, data + pos + length);
        pos += length;
      }
    } else {
      if (1 == kind) {
        length = 4 + ((tag >> 2) & 0x07);
        if (pos >= size) ret = OB_INVALID_DATA;
        else offset = (static_cast<size_t>(tag & 0xe0) << 3) | data[pos++];
      } else if (2 == kind) {
        length = 1 + (tag >> 2);
        if (size - pos < 2) ret = OB_INVALID_DATA;
        else { offset = static_cast<size_t>(data[pos]) | (static_cast<size_t>(data[pos + 1]) << 8); pos += 2; }
      } else {
        length = 1 + (tag >> 2);
        if (size - pos < 4) ret = OB_INVALID_DATA;
        else { offset = load_u32(data + pos); pos += 4; }
      }
      if (OB_SUCC(ret) && (0 == offset || offset > output.size()
                           || length > declared_size - output.size())) {
        ret = OB_INVALID_DATA;
      }
      for (size_t i = 0; OB_SUCC(ret) && i < length; ++i) {
        output.push_back(output[output.size() - offset]);
      }
    }
  }
  if (OB_SUCC(ret) && (output.size() != declared_size || pos != size)) ret = OB_INVALID_DATA;
  return ret;
}

int decode_hybrid(const uint8_t *data, const size_t size, const uint8_t bit_width,
                  const size_t value_count, std::vector<uint32_t> &values)
{
  int ret = OB_SUCCESS;
  size_t pos = 0;
  values.clear();
  if (bit_width > 32) ret = OB_NOT_SUPPORTED;
  while (OB_SUCC(ret) && values.size() < value_count) {
    uint64_t header = 0;
    if (!read_var_uint(data, size, pos, header) || 0 == header) {
      ret = OB_INVALID_DATA;
    } else if (0 == (header & 1)) {
      const uint64_t run_length = header >> 1;
      const size_t byte_width = (bit_width + 7) / 8;
      uint32_t value = 0;
      if (run_length > value_count - values.size() || byte_width > size - pos) {
        ret = OB_INVALID_DATA;
      } else {
        for (size_t i = 0; i < byte_width; ++i) value |= static_cast<uint32_t>(data[pos++]) << (8 * i);
        values.insert(values.end(), static_cast<size_t>(run_length), value);
      }
    } else {
      const uint64_t groups = header >> 1;
      if (groups > (std::numeric_limits<size_t>::max() / 8)) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        const size_t run_values = static_cast<size_t>(groups) * 8;
        const size_t run_bytes = (run_values * bit_width + 7) / 8;
        if (run_bytes > size - pos) {
          ret = OB_INVALID_DATA;
        } else {
          for (size_t i = 0; i < run_values && values.size() < value_count; ++i) {
            uint32_t value = 0;
            for (uint8_t bit = 0; bit < bit_width; ++bit) {
              const size_t absolute_bit = i * bit_width + bit;
              value |= ((data[pos + absolute_bit / 8] >> (absolute_bit % 8)) & 1U) << bit;
            }
            values.push_back(value);
          }
          pos += run_bytes;
        }
      }
    }
  }
  return ret;
}

int decode_levels(const uint8_t *data, const size_t size, const size_t value_count,
                  const bool length_prefixed, std::vector<uint32_t> &levels, size_t &consumed)
{
  int ret = OB_SUCCESS;
  consumed = 0;
  if (length_prefixed) {
    if (size < 4) ret = OB_INVALID_DATA;
    else {
      const uint32_t encoded_size = load_u32(data);
      if (encoded_size > size - 4) ret = OB_INVALID_DATA;
      else if (OB_FAIL(decode_hybrid(data + 4, encoded_size, 1, value_count, levels))) {}
      else consumed = 4 + encoded_size;
    }
  } else if (OB_FAIL(decode_hybrid(data, size, 1, value_count, levels))) {
  } else {
    consumed = size;
  }
  return ret;
}

int decode_plain_values(const uint8_t *data, const size_t size, const size_t value_count,
                        const SchemaElement &schema, std::vector<ObFileCell> &values,
                        size_t &consumed)
{
  int ret = OB_SUCCESS;
  consumed = 0;
  values.assign(value_count, ObFileCell());
  for (size_t i = 0; OB_SUCC(ret) && i < value_count; ++i) {
    ObFileCell &cell = values[i];
    cell.is_null_ = false;
    switch (schema.physical_type_) {
      case PT_BOOLEAN: {
        const size_t byte = i / 8;
        if (byte >= size) ret = OB_INVALID_DATA;
        else cell.bool_value_ = 0 != ((data[byte] >> (i % 8)) & 1);
        consumed = (value_count + 7) / 8;
        break;
      }
      case PT_INT32:
        if (size - consumed < 4) ret = OB_INVALID_DATA;
        else {
          const int32_t value = static_cast<int32_t>(load_u32(data + consumed));
          if (6 == schema.converted_type_ || LA_DATE == schema.logical_annotation_) cell.date_value_ = value;
          else cell.int_value_ = value;
          consumed += 4;
        }
        break;
      case PT_INT64:
        if (size - consumed < 8) ret = OB_INVALID_DATA;
        else {
          const int64_t value = static_cast<int64_t>(load_u64(data + consumed));
          if (9 == schema.converted_type_ || LA_TIMESTAMP_MILLIS == schema.logical_annotation_) {
            if (value > std::numeric_limits<int64_t>::max() / 1000L
                || value < std::numeric_limits<int64_t>::min() / 1000L) ret = OB_SIZE_OVERFLOW;
            else cell.datetime_value_ = value * 1000L;
          } else if (10 == schema.converted_type_ || LA_TIMESTAMP_MICROS == schema.logical_annotation_) {
            cell.datetime_value_ = value;
          } else if (LA_TIMESTAMP_NANOS == schema.logical_annotation_) {
            cell.datetime_value_ = value / 1000L;
          }
          else cell.int_value_ = value;
          consumed += 8;
        }
        break;
      case PT_INT96:
        if (size - consumed < 12) ret = OB_INVALID_DATA;
        else {
          const uint64_t nanos_of_day = load_u64(data + consumed);
          const int64_t julian_day = load_u32(data + consumed + 8);
          if (nanos_of_day >= 86400ULL * 1000000000ULL) {
            ret = OB_INVALID_DATA;
          } else {
            cell.datetime_value_ = (julian_day - 2440588L) * 86400L * 1000000L
                                 + static_cast<int64_t>(nanos_of_day / 1000L);
            consumed += 12;
          }
        }
        break;
      case PT_FLOAT:
        if (size - consumed < 4) ret = OB_INVALID_DATA;
        else {
          const uint32_t bits = load_u32(data + consumed);
          float value = 0;
          memcpy(&value, &bits, sizeof(value));
          cell.double_value_ = value;
          consumed += 4;
        }
        break;
      case PT_DOUBLE:
        if (size - consumed < 8) ret = OB_INVALID_DATA;
        else {
          const uint64_t bits = load_u64(data + consumed);
          memcpy(&cell.double_value_, &bits, sizeof(bits));
          consumed += 8;
        }
        break;
      case PT_BYTE_ARRAY: {
        if (size - consumed < 4) ret = OB_INVALID_DATA;
        else {
          const uint32_t length = load_u32(data + consumed);
          consumed += 4;
          if (length > 16U * 1024U * 1024U || length > size - consumed) ret = OB_SIZE_OVERFLOW;
          else {
            cell.string_value_.assign(reinterpret_cast<const char *>(data + consumed), length);
            consumed += length;
          }
        }
        break;
      }
      case PT_FIXED_LEN_BYTE_ARRAY:
        if (schema.type_length_ <= 0 || static_cast<size_t>(schema.type_length_) > size - consumed) {
          ret = OB_INVALID_DATA;
        } else {
          cell.string_value_.assign(reinterpret_cast<const char *>(data + consumed), schema.type_length_);
          consumed += schema.type_length_;
        }
        break;
      default: ret = OB_NOT_SUPPORTED; break;
    }
  }
  return ret;
}

int decompress_page(const int32_t codec, const uint8_t *data, const size_t compressed_size,
                    const size_t uncompressed_size, std::vector<uint8_t> &output)
{
  int ret = OB_SUCCESS;
  if (PC_UNCOMPRESSED == codec) {
    if (compressed_size != uncompressed_size) ret = OB_INVALID_DATA;
    else output.assign(data, data + compressed_size);
  } else if (PC_SNAPPY == codec) {
    ret = snappy_decompress(data, compressed_size, uncompressed_size, output);
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

} // namespace

class ObParquetReader::Impl
{
public:
  Impl() : fd_(-1), file_size_(0), path_(), meta_(), columns_(), projected_columns_(),
           row_group_idx_(0), row_idx_(0), decoded_() {}
  ~Impl() { close(); }
  void close()
  {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    file_size_ = 0;
    meta_ = FileMeta();
    columns_.clear();
    projected_columns_.clear();
    decoded_.clear();
    row_group_idx_ = 0;
    row_idx_ = 0;
  }
  int load_row_group();
  int decode_column(const RowGroupMeta &group, const size_t column_idx,
                    std::vector<ObFileCell> &values);

  int fd_;
  int64_t file_size_;
  std::string path_;
  FileMeta meta_;
  std::vector<ObFileColumnSchema> columns_;
  std::vector<bool> projected_columns_;
  size_t row_group_idx_;
  int64_t row_idx_;
  std::vector<std::vector<ObFileCell> > decoded_;
};

int ObParquetReader::Impl::load_row_group()
{
  int ret = OB_SUCCESS;
  row_idx_ = 0;
  decoded_.assign(columns_.size(), std::vector<ObFileCell>());
  if (row_group_idx_ >= meta_.row_groups_.size()) {
    ret = OB_ITER_END;
  } else {
    const RowGroupMeta &group = meta_.row_groups_[row_group_idx_];
    for (size_t i = 0; OB_SUCC(ret) && i < columns_.size(); ++i) {
      if (!projected_columns_[i]) {
        // The row group cursor is driven by metadata; no bytes from this column
        // chunk are read when the column is not referenced by the SQL plan.
      } else if (OB_FAIL(decode_column(group, i, decoded_[i]))) {
        LOG_WARN("failed to decode parquet column", K(ret), K(i), K(row_group_idx_));
      } else if (decoded_[i].size() != static_cast<size_t>(group.num_rows_)) {
        ret = OB_INVALID_DATA;
      }
    }
  }
  return ret;
}

int ObParquetReader::Impl::decode_column(const RowGroupMeta &group, const size_t column_idx,
                                         std::vector<ObFileCell> &values)
{
  int ret = OB_SUCCESS;
  values.clear();
  if (column_idx >= group.columns_.size() || column_idx + 1 >= meta_.schema_.size()) {
    ret = OB_INVALID_DATA;
  } else {
    const ColumnMeta &column = group.columns_[column_idx];
    const SchemaElement &schema = meta_.schema_[column_idx + 1];
    int64_t offset = column.dictionary_page_offset_ >= 0
                   ? std::min(column.dictionary_page_offset_, column.data_page_offset_)
                   : column.data_page_offset_;
    const int64_t chunk_end = offset + column.total_compressed_size_;
    std::vector<ObFileCell> dictionary;
    if (offset < 4 || column.total_compressed_size_ < 0 || chunk_end < offset
        || chunk_end > file_size_ || column.num_values_ != group.num_rows_) {
      ret = OB_INVALID_DATA;
    }
    while (OB_SUCC(ret) && offset < chunk_end
           && values.size() < static_cast<size_t>(column.num_values_)) {
      const size_t available = static_cast<size_t>(chunk_end - offset);
      const size_t header_probe = std::min<size_t>(available, 1024U * 1024U);
      std::vector<uint8_t> header_bytes(header_probe);
      PageHeader header;
      size_t header_size = 0;
      if (OB_FAIL(pread_all(fd_, offset, header_bytes.data(), header_bytes.size()))) {
      } else if (!parse_page_header(header_bytes.data(), header_bytes.size(), header, header_size)) {
        ret = OB_INVALID_DATA;
      } else if (header.compressed_size_ > 64 * 1024 * 1024
                 || header.uncompressed_size_ > 64 * 1024 * 1024
                 || header.num_values_ > 1024 * 1024
                 || header_size > available
                 || static_cast<size_t>(header.compressed_size_) > available - header_size) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        std::vector<uint8_t> payload(static_cast<size_t>(header.compressed_size_));
        if (!payload.empty()
            && OB_FAIL(pread_all(fd_, offset + header_size, payload.data(), payload.size()))) {
        } else {
          offset += static_cast<int64_t>(header_size + payload.size());
          if (PP_INDEX_PAGE == header.type_) {
            // Index pages do not contain row values.
          } else if (PP_DICTIONARY_PAGE == header.type_) {
            std::vector<uint8_t> plain;
            size_t consumed = 0;
            if ((PE_PLAIN != header.encoding_ && PE_PLAIN_DICTIONARY != header.encoding_)
                || !dictionary.empty()) {
              ret = OB_NOT_SUPPORTED;
            } else if (OB_FAIL(decompress_page(column.codec_, payload.data(), payload.size(),
                                               header.uncompressed_size_, plain))) {
            } else if (OB_FAIL(decode_plain_values(plain.data(), plain.size(), header.num_values_,
                                                   schema, dictionary, consumed))) {
            } else if (consumed != plain.size()) {
              ret = OB_INVALID_DATA;
            }
          } else if (PP_DATA_PAGE == header.type_ || PP_DATA_PAGE_V2 == header.type_) {
            const bool v2 = PP_DATA_PAGE_V2 == header.type_;
            std::vector<uint8_t> page;
            const uint8_t *definition_data = nullptr;
            size_t definition_size = 0;
            const uint8_t *encoded_values = nullptr;
            size_t encoded_values_size = 0;
            if (!v2) {
              if (OB_FAIL(decompress_page(column.codec_, payload.data(), payload.size(),
                                          header.uncompressed_size_, page))) {
              } else {
                definition_data = page.data();
                definition_size = page.size();
              }
            } else if (header.repetition_length_ != 0
                       || header.definition_length_ + header.repetition_length_ > header.compressed_size_
                       || header.definition_length_ + header.repetition_length_ > header.uncompressed_size_) {
              ret = OB_NOT_SUPPORTED;
            } else {
              definition_data = payload.data() + header.repetition_length_;
              definition_size = header.definition_length_;
              const size_t value_compressed_size = payload.size() - header.repetition_length_
                                                - header.definition_length_;
              const size_t value_uncompressed_size = header.uncompressed_size_
                                                   - header.repetition_length_
                                                   - header.definition_length_;
              if (header.is_compressed_) {
                if (OB_FAIL(decompress_page(column.codec_,
                                            payload.data() + header.repetition_length_
                                                           + header.definition_length_,
                                            value_compressed_size, value_uncompressed_size, page))) {
                } else {
                  encoded_values = page.data();
                  encoded_values_size = page.size();
                }
              } else if (value_compressed_size != value_uncompressed_size) {
                ret = OB_INVALID_DATA;
              } else {
                encoded_values = payload.data() + header.repetition_length_ + header.definition_length_;
                encoded_values_size = value_compressed_size;
              }
            }

            std::vector<uint32_t> levels;
            size_t level_bytes = 0;
            const bool optional = 1 == schema.repetition_;
            if (OB_SUCC(ret) && optional) {
              if (!v2 && PE_RLE != header.definition_encoding_) {
                ret = OB_NOT_SUPPORTED;
              } else if (OB_FAIL(decode_levels(definition_data, definition_size,
                                               header.num_values_, !v2, levels, level_bytes))) {
              }
            } else if (OB_SUCC(ret)) {
              levels.assign(static_cast<size_t>(header.num_values_), 1);
            }
            if (OB_SUCC(ret) && !v2) {
              if (level_bytes > page.size()) ret = OB_INVALID_DATA;
              else {
                encoded_values = page.data() + level_bytes;
                encoded_values_size = page.size() - level_bytes;
              }
            }
            size_t present_count = 0;
            for (size_t i = 0; OB_SUCC(ret) && i < levels.size(); ++i) {
              if (levels[i] > 1) ret = OB_INVALID_DATA;
              else if (1 == levels[i]) ++present_count;
            }
            if (OB_SUCC(ret) && v2
                && present_count != static_cast<size_t>(header.num_values_ - header.num_nulls_)) {
              ret = OB_INVALID_DATA;
            }
            std::vector<ObFileCell> present_values;
            size_t consumed = 0;
            if (OB_SUCC(ret) && PE_PLAIN == header.encoding_) {
              ret = decode_plain_values(encoded_values, encoded_values_size, present_count,
                                        schema, present_values, consumed);
            } else if (OB_SUCC(ret)
                       && (PE_RLE_DICTIONARY == header.encoding_
                           || PE_PLAIN_DICTIONARY == header.encoding_)) {
              if (0 == present_count) {
                // An all-null page may legally use dictionary encoding without
                // containing either dictionary values or dictionary indices.
              } else if (dictionary.empty() || 0 == encoded_values_size) {
                ret = OB_INVALID_DATA;
              } else {
                const uint8_t bit_width = encoded_values[0];
                std::vector<uint32_t> indices;
                if (OB_FAIL(decode_hybrid(encoded_values + 1, encoded_values_size - 1,
                                          bit_width, present_count, indices))) {
                } else {
                  for (size_t i = 0; OB_SUCC(ret) && i < indices.size(); ++i) {
                    if (indices[i] >= dictionary.size()) ret = OB_INVALID_DATA;
                    else present_values.push_back(dictionary[indices[i]]);
                  }
                }
              }
            } else if (OB_SUCC(ret)) {
              ret = OB_NOT_SUPPORTED;
            }
            if (OB_SUCC(ret) && PE_PLAIN == header.encoding_ && consumed != encoded_values_size) {
              ret = OB_INVALID_DATA;
            }
            if (OB_SUCC(ret)) {
              size_t present_idx = 0;
              for (size_t i = 0; i < levels.size(); ++i) {
                if (0 == levels[i]) values.push_back(ObFileCell());
                else values.push_back(present_values[present_idx++]);
              }
            }
          } else {
            ret = OB_NOT_SUPPORTED;
          }
        }
      }
    }
    if (OB_SUCC(ret) && values.size() != static_cast<size_t>(column.num_values_)) {
      ret = OB_INVALID_DATA;
    }
  }
  return ret;
}

ObParquetReader::ObParquetReader() : impl_(new (std::nothrow) Impl()) {}
ObParquetReader::~ObParquetReader() {}

int ObParquetReader::infer_schema(const std::string &path,
                                  std::vector<ObFileColumnSchema> &columns,
                                  int64_t &row_count)
{
  int ret = OB_SUCCESS;
  int fd = -1;
  struct stat st;
  FileMeta meta;
  if (OB_FAIL(open_verified(path, 0, 0, -1, -1, fd))) {
    LOG_WARN("failed to open parquet file", K(ret));
  } else if (0 != fstat(fd, &st)) {
    ret = OB_IO_ERROR;
  } else if (OB_FAIL(read_file_metadata(fd, st.st_size, meta))) {
    LOG_WARN("failed to read parquet metadata", K(ret));
  } else if (OB_FAIL(map_schema(meta, columns))) {
    LOG_WARN("unsupported parquet schema", K(ret));
  } else {
    row_count = meta.num_rows_;
  }
  if (fd >= 0) ::close(fd);
  return ret;
}

int ObParquetReader::open(const std::string &path,
                          const std::vector<ObFileColumnSchema> &columns,
                          const uint64_t expected_device,
                          const uint64_t expected_inode,
                          const int64_t expected_file_size,
                          const int64_t expected_modified_time_ns,
                          const std::vector<bool> &projected_columns)
{
  int ret = OB_SUCCESS;
  if (!impl_) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    impl_->close();
    impl_->path_ = path;
    impl_->columns_ = columns;
    impl_->projected_columns_ = projected_columns;
    if (projected_columns.size() != columns.size()) {
      ret = OB_INVALID_ARGUMENT;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(open_verified(path, expected_device, expected_inode, expected_file_size,
                              expected_modified_time_ns, impl_->fd_))) {
      LOG_WARN("failed to open verified parquet file", K(ret));
    } else {
      struct stat st;
      if (0 != fstat(impl_->fd_, &st)) {
        ret = OB_IO_ERROR;
      } else {
        impl_->file_size_ = st.st_size;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(read_file_metadata(impl_->fd_, impl_->file_size_, impl_->meta_))) {
      LOG_WARN("failed to read parquet metadata", K(ret));
    } else {
      std::vector<ObFileColumnSchema> actual_columns;
      if (OB_FAIL(map_schema(impl_->meta_, actual_columns))) {
      } else if (actual_columns.size() != columns.size()) {
        ret = OB_SCHEMA_EAGAIN;
      } else {
        for (size_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
          if (actual_columns[i].source_name_ != columns[i].source_name_
              || actual_columns[i].type_ != columns[i].type_) {
            ret = OB_SCHEMA_EAGAIN;
          }
        }
      }
    }
  }
  if (OB_FAIL(ret) && impl_) impl_->close();
  return ret;
}

int ObParquetReader::get_next_row(std::vector<ObFileCell> &cells)
{
  int ret = OB_SUCCESS;
  if (!impl_ || impl_->fd_ < 0) {
    ret = OB_NOT_INIT;
  } else {
    while (OB_SUCC(ret)
           && (impl_->decoded_.empty()
               || impl_->row_idx_ >= impl_->meta_.row_groups_[impl_->row_group_idx_].num_rows_)) {
      if (!impl_->decoded_.empty()) {
        ++impl_->row_group_idx_;
      }
      if (impl_->row_group_idx_ >= impl_->meta_.row_groups_.size()) {
        ret = OB_ITER_END;
      } else if (OB_FAIL(impl_->load_row_group())) {
        LOG_WARN("failed to load parquet row group", K(ret), K(impl_->row_group_idx_));
      }
    }
    if (OB_SUCC(ret)) {
      cells.assign(impl_->columns_.size(), ObFileCell());
      for (size_t i = 0; i < impl_->columns_.size(); ++i) {
        if (impl_->projected_columns_[i]) {
          cells[i] = impl_->decoded_[i][impl_->row_idx_];
        }
      }
      ++impl_->row_idx_;
    }
  }
  return ret;
}

int ObParquetReader::rescan()
{
  int ret = OB_SUCCESS;
  if (!impl_ || impl_->fd_ < 0) ret = OB_NOT_INIT;
  else {
    impl_->row_group_idx_ = 0;
    impl_->row_idx_ = 0;
    impl_->decoded_.clear();
  }
  return ret;
}

void ObParquetReader::close()
{
  if (impl_) impl_->close();
}

} // namespace sql
} // namespace oceanbase
