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

#include "sql/engine/file_source/file_parquet_reader.h"
#include "sql/engine/file_source/file_parquet_format.h"
#include "sql/engine/file_source/file_source_util.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace oceanbase {
namespace sql {
namespace file_source {

namespace {

const char *const kParquetMagic = "PAR1";

// thrift compact protocol field types
const int8_t kTypeBooleanTrue = 1;

// parquet physical types
const int32_t kTypeBoolean = 0;
const int32_t kTypeInt32 = 1;
const int32_t kTypeInt64 = 2;
const int32_t kTypeInt96 = 3;
const int32_t kTypeFloat = 4;
const int32_t kTypeDouble = 5;
const int32_t kTypeByteArray = 6;
const int32_t kTypeFixedLenByteArray = 7;

// parquet page types
const int32_t kPageTypeData = 0;
const int32_t kPageTypeIndex = 1;
const int32_t kPageTypeDictionary = 2;
const int32_t kPageTypeDataV2 = 3;

// parquet encodings
const int32_t kEncodingPlain = 0;
const int32_t kEncodingPlainDictionary = 2;
const int32_t kEncodingRle = 3;
const int32_t kEncodingRleDictionary = 8;

// converted types
const int32_t kConvertedDate = 6;
const int32_t kConvertedTimestampMillis = 9;
const int32_t kConvertedTimestampMicros = 10;

// logical type kinds
const int32_t kLogicalDate = 6;
const int32_t kLogicalTimestamp = 8;

int32_t bit_width(int64_t max_value)
{
  int32_t width = 0;
  while ((1LL << width) <= max_value) {
    ++width;
  }
  return width;
}

/// Read a byte range of a file.
bool read_file_range(FILE *file, int64_t offset, int64_t length, std::vector<unsigned char> &buffer)
{
  if (offset < 0 || length < 0) {
    return false;
  }
  buffer.assign(static_cast<size_t>(length), 0);
  if (0 == length) {
    return true;
  }
  if (0 != ::fseek(file, static_cast<long>(offset), SEEK_SET)) {
    return false;
  }
  const size_t read = std::fread(&buffer[0], 1, static_cast<size_t>(length), file);
  return read == static_cast<size_t>(length);
}

/// Parsed page header.
struct PageHeaderInfo
{
  int32_t page_type = 0;
  int32_t uncompressed_page_size = 0;
  int32_t compressed_page_size = 0;
  int32_t num_values = 0;
  int32_t num_nulls = 0;
  int32_t encoding = 0;
  int32_t definition_levels_byte_length = 0;
  int32_t repetition_levels_byte_length = 0;
  bool is_compressed = true;
};

/// Read the schema element of the parquet schema.
bool read_schema_element(ThriftCompactReader &reader, ParquetSchemaNode &node)
{
  reader.push_struct();
  while (true) {
    int16_t field_id = 0;
    int8_t type = 0;
    bool is_stop = false;
    if (!reader.read_field_begin(field_id, type, is_stop)) {
      reader.pop_struct();
      return false;
    }
    if (is_stop) {
      reader.pop_struct();
      return true;
    }
    switch (field_id) {
      case 1: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        node.type = value;
        break;
      }
      case 2: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        node.type_length = value;
        break;
      }
      case 3: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        node.repetition_type = value;
        break;
      }
      case 4: {
        std::string value;
        if (!reader.read_binary(value)) {
          return false;
        }
        node.name = value;
        break;
      }
      case 5: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        node.num_children = value;
        break;
      }
      case 6: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        node.converted_type = value;
        break;
      }
      case 10: {
        // the logical type is a union, the field id selects the member
        int16_t member_id = 0;
        int8_t member_type = 0;
        bool member_stop = false;
        reader.push_struct();
        if (!reader.read_field_begin(member_id, member_type, member_stop)) {
          reader.pop_struct();
          return false;
        }
        if (!member_stop) {
          node.logical_type_kind = member_id;
          if (!reader.skip_field(member_type)) {
            reader.pop_struct();
            return false;
          }
        }
        reader.pop_struct();
        break;
      }
      default: {
        if (!reader.skip_field(type)) {
          return false;
        }
        break;
      }
    }
  }
}

/// Read a column chunk of a row group.
bool read_column_chunk(ThriftCompactReader &reader, ParquetColumnChunk &chunk)
{
  reader.push_struct();
  while (true) {
    int16_t field_id = 0;
    int8_t type = 0;
    bool is_stop = false;
    if (!reader.read_field_begin(field_id, type, is_stop)) {
      reader.pop_struct();
      return false;
    }
    if (is_stop) {
      reader.pop_struct();
      return true;
    }
    if (2 == field_id) {
      int64_t value = 0;
      if (!reader.read_int64(value)) {
        return false;
      }
      chunk.file_offset = value;
      continue;
    }
    if (3 == field_id) {
      // the column meta data
      reader.push_struct();
      while (true) {
        int16_t meta_id = 0;
        int8_t meta_type = 0;
        bool meta_stop = false;
        if (!reader.read_field_begin(meta_id, meta_type, meta_stop)) {
          reader.pop_struct();
          return false;
        }
        if (meta_stop) {
          break;
        }
        switch (meta_id) {
          case 1: {
            int32_t value = 0;
            if (!reader.read_int32(value)) {
              return false;
            }
            break;
          }
          case 2: {
            int8_t elem_type = 0;
            uint32_t count = 0;
            if (!reader.read_list_begin(elem_type, count)) {
              return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
              int32_t value = 0;
              if (!reader.read_int32(value)) {
                return false;
              }
              chunk.encodings.push_back(value);
            }
            break;
          }
          case 3: {
            int8_t elem_type = 0;
            uint32_t count = 0;
            if (!reader.read_list_begin(elem_type, count)) {
              return false;
            }
            for (uint32_t i = 0; i < count; ++i) {
              std::string value;
              if (!reader.read_binary(value)) {
                return false;
              }
              if (i + 1 == count) {
                chunk.path_in_schema = value;
              }
            }
            break;
          }
          case 4: {
            int32_t value = 0;
            if (!reader.read_int32(value)) {
              return false;
            }
            chunk.codec = value;
            break;
          }
          case 5: {
            int64_t value = 0;
            if (!reader.read_int64(value)) {
              return false;
            }
            chunk.num_values = value;
            break;
          }
          case 6: {
            int64_t value = 0;
            if (!reader.read_int64(value)) {
              return false;
            }
            chunk.total_uncompressed_size = value;
            break;
          }
          case 7: {
            int64_t value = 0;
            if (!reader.read_int64(value)) {
              return false;
            }
            chunk.total_compressed_size = value;
            break;
          }
          case 9: {
            int64_t value = 0;
            if (!reader.read_int64(value)) {
              return false;
            }
            chunk.data_page_offset = value;
            break;
          }
          case 10:
          case 12:
          case 13:
          case 14:
          case 15:
          case 16:
          case 17:
          case 18: {
            if (!reader.skip_field(meta_type)) {
              return false;
            }
            break;
          }
          default: {
            if (!reader.skip_field(meta_type)) {
              reader.pop_struct();
              return false;
            }
            break;
          }
        }
      }
      reader.pop_struct();
      continue;
    }
    if (!reader.skip_field(type)) {
      return false;
    }
  }
}

/// Read a row group of the file metadata.
bool read_row_group(ThriftCompactReader &reader, ParquetRowGroup &row_group)
{
  reader.push_struct();
  while (true) {
    int16_t field_id = 0;
    int8_t type = 0;
    bool is_stop = false;
    if (!reader.read_field_begin(field_id, type, is_stop)) {
      reader.pop_struct();
      return false;
    }
    if (is_stop) {
      reader.pop_struct();
      return true;
    }
    if (1 == field_id) {
      int8_t elem_type = 0;
      uint32_t count = 0;
      if (!reader.read_list_begin(elem_type, count)) {
        return false;
      }
      for (uint32_t i = 0; i < count; ++i) {
        ParquetColumnChunk chunk;
        if (!read_column_chunk(reader, chunk)) {
          return false;
        }
        row_group.columns.push_back(chunk);
      }
      continue;
    }
    if (3 == field_id) {
      int64_t value = 0;
      if (!reader.read_int64(value)) {
        return false;
      }
      row_group.num_rows = value;
      continue;
    }
    if (2 == field_id) {
      int64_t value = 0;
      if (!reader.read_int64(value)) {
        return false;
      }
      row_group.total_byte_size = value;
      continue;
    }
    if (!reader.skip_field(type)) {
      return false;
    }
  }
}

/// Read the page header of a page.
bool read_page_header(ThriftCompactReader &reader, PageHeaderInfo &header)
{
  reader.push_struct();
  while (true) {
    int16_t field_id = 0;
    int8_t type = 0;
    bool is_stop = false;
    if (!reader.read_field_begin(field_id, type, is_stop)) {
      reader.pop_struct();
      return false;
    }
    if (is_stop) {
      reader.pop_struct();
      return true;
    }
    switch (field_id) {
      case 1: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        header.page_type = value;
        break;
      }
      case 2: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        header.uncompressed_page_size = value;
        break;
      }
      case 3: {
        int32_t value = 0;
        if (!reader.read_int32(value)) {
          return false;
        }
        header.compressed_page_size = value;
        break;
      }
      case 5: {
        // the data page header of a v1 page
        reader.push_struct();
        while (true) {
          int16_t page_id = 0;
          int8_t page_type = 0;
          bool page_stop = false;
          if (!reader.read_field_begin(page_id, page_type, page_stop)) {
            reader.pop_struct();
            return false;
          }
          if (page_stop) {
            break;
          }
          if (1 == page_id) {
            int32_t value = 0;
            if (!reader.read_int32(value)) {
              return false;
            }
            header.num_values = value;
            continue;
          }
          if (2 == page_id) {
            int32_t value = 0;
            if (!reader.read_int32(value)) {
              return false;
            }
            header.encoding = value;
            continue;
          }
          if (!reader.skip_field(page_type)) {
            reader.pop_struct();
            return false;
          }
        }
        reader.pop_struct();
        break;
      }
      case 7: {
        // the dictionary page header
        reader.push_struct();
        while (true) {
          int16_t page_id = 0;
          int8_t page_type = 0;
          bool page_stop = false;
          if (!reader.read_field_begin(page_id, page_type, page_stop)) {
            reader.pop_struct();
            return false;
          }
          if (page_stop) {
            break;
          }
          if (1 == page_id) {
            int32_t value = 0;
            if (!reader.read_int32(value)) {
              return false;
            }
            header.num_values = value;
            continue;
          }
          if (2 == page_id) {
            int32_t value = 0;
            if (!reader.read_int32(value)) {
              return false;
            }
            header.encoding = value;
            continue;
          }
          if (!reader.skip_field(page_type)) {
            reader.pop_struct();
            return false;
          }
        }
        reader.pop_struct();
        break;
      }
      case 8: {
        // the data page header of a v2 page
        reader.push_struct();
        while (true) {
          int16_t page_id = 0;
          int8_t page_type = 0;
          bool page_stop = false;
          if (!reader.read_field_begin(page_id, page_type, page_stop)) {
            reader.pop_struct();
            return false;
          }
          if (page_stop) {
            break;
          }
          switch (page_id) {
            case 1: {
              int32_t value = 0;
              if (!reader.read_int32(value)) {
                return false;
              }
              header.num_values = value;
              break;
            }
            case 2: {
              int32_t value = 0;
              if (!reader.read_int32(value)) {
                return false;
              }
              header.num_nulls = value;
              break;
            }
            case 4: {
              int32_t value = 0;
              if (!reader.read_int32(value)) {
                return false;
              }
              header.encoding = value;
              break;
            }
            case 5: {
              int32_t value = 0;
              if (!reader.read_int32(value)) {
                return false;
              }
              header.definition_levels_byte_length = value;
              break;
            }
            case 6: {
              int32_t value = 0;
              if (!reader.read_int32(value)) {
                return false;
              }
              header.repetition_levels_byte_length = value;
              break;
            }
            case 7: {
              // a boolean is encoded in the type of the field header
              header.is_compressed = (kTypeBooleanTrue == page_type);
              break;
            }
            default: {
              if (!reader.skip_field(page_type)) {
                reader.pop_struct();
                return false;
              }
              break;
            }
          }
        }
        reader.pop_struct();
        break;
      }
      default: {
        if (!reader.skip_field(type)) {
          return false;
        }
        break;
      }
    }
  }
}

/// Decode the values of a data page.
///
/// `dictionary` holds the values of the dictionary page when the page uses a
/// dictionary encoding.
bool decode_data_page(const unsigned char *page,
                       size_t page_size,
                       const PageHeaderInfo &header,
                       bool is_v2,
                       const ParquetColumnDesc &column,
                       const std::vector<std::string> &dictionary,
                       std::vector<std::string> &values,
                       std::vector<bool> &nulls,
                       std::string &error)
{
  const int32_t max_def_level = column.max_def_level;
  const int32_t max_rep_level = column.max_rep_level;
  std::vector<int32_t> definition_levels;
  size_t payload_pos = 0;
  size_t payload_size = page_size;
  if (is_v2) {
    payload_pos = static_cast<size_t>(header.repetition_levels_byte_length)
                  + static_cast<size_t>(header.definition_levels_byte_length);
    if (payload_pos > page_size) {
      error = "the definition levels of the parquet page are truncated";
      return false;
    }
    payload_size = page_size - payload_pos;
    if (max_def_level > 0 && header.definition_levels_byte_length > 0) {
      const unsigned char *def_data = page + header.repetition_levels_byte_length;
      size_t consumed = 0;
      if (!decode_rle_hybrid(def_data,
                             static_cast<size_t>(header.definition_levels_byte_length),
                             bit_width(max_def_level),
                             header.num_values,
                             definition_levels,
                             consumed)) {
        error = "failed to decode the definition levels of the parquet page";
        return false;
      }
    }
  } else {
    size_t pos = 0;
    if (max_rep_level > 0) {
      if (pos + 4 > page_size) {
        error = "the repetition levels of the parquet page are truncated";
        return false;
      }
      uint32_t length = 0;
      std::memcpy(&length, page + pos, 4);
      pos += 4;
      std::vector<int32_t> repetition_levels;
      size_t consumed = 0;
      if (!decode_rle_hybrid(page + pos, length, bit_width(max_rep_level), header.num_values, repetition_levels,
                             consumed)) {
        error = "failed to decode the repetition levels of the parquet page";
        return false;
      }
      pos += length;
    }
    if (max_def_level > 0) {
      if (pos + 4 > page_size) {
        error = "the definition levels of the parquet page are truncated";
        return false;
      }
      uint32_t length = 0;
      std::memcpy(&length, page + pos, 4);
      pos += 4;
      size_t consumed = 0;
      if (!decode_rle_hybrid(page + pos, length, bit_width(max_def_level), header.num_values, definition_levels,
                             consumed)) {
        error = "failed to decode the definition levels of the parquet page";
        return false;
      }
      pos += length;
    }
    payload_pos = pos;
    payload_size = page_size - pos;
  }
  // the number of values that are not null
  int64_t present = header.num_values;
  if (max_def_level > 0 && !definition_levels.empty()) {
    present = 0;
    for (size_t i = 0; i < definition_levels.size() && i < static_cast<size_t>(header.num_values); ++i) {
      if (definition_levels[i] >= max_def_level) {
        ++present;
      }
    }
  }
  const bool dictionary_encoded =
      (kEncodingPlainDictionary == header.encoding || kEncodingRleDictionary == header.encoding);
  std::vector<std::string> decoded;
  if (dictionary_encoded) {
    if (dictionary.empty()) {
      error = "the parquet page uses a dictionary encoding but the file has no dictionary page";
      return false;
    }
    const unsigned char *index_data = page + payload_pos;
    size_t index_size = payload_size;
    size_t pos = 0;
    if (!is_v2) {
      if (pos + 4 > index_size) {
        error = "the dictionary indices of the parquet page are truncated";
        return false;
      }
      uint32_t length = 0;
      std::memcpy(&length, index_data + pos, 4);
      pos += 4;
      index_data += 4;
      index_size = length;
    }
    if (index_size < 1) {
      error = "the dictionary indices of the parquet page are truncated";
      return false;
    }
    const int32_t width = index_data[0];
    std::vector<int32_t> indices;
    size_t consumed = 0;
    if (!decode_rle_hybrid(index_data + 1, index_size - 1, width, present, indices, consumed)) {
      error = "failed to decode the dictionary indices of the parquet page";
      return false;
    }
    for (int64_t i = 0; i < present; ++i) {
      const int32_t index = indices[static_cast<size_t>(i)];
      if (index < 0 || static_cast<size_t>(index) >= dictionary.size()) {
        error = "the dictionary index of the parquet page is out of range";
        return false;
      }
      decoded.push_back(dictionary[static_cast<size_t>(index)]);
    }
  } else if (kEncodingPlain == header.encoding) {
    if (!decode_plain_values(page + payload_pos,
                              payload_size,
                              column.type,
                              column.type_length,
                              column.converted_type,
                              present,
                              decoded)) {
      error = "failed to decode the plain encoded values of the parquet page";
      return false;
    }
  } else {
    error = "the parquet page uses the unsupported encoding " + std::to_string(header.encoding);
    return false;
  }
  // interleave the decoded values with the null flags of the page
  size_t value_index = 0;
  for (int64_t i = 0; i < header.num_values; ++i) {
    const bool is_null = (!definition_levels.empty()
                           && static_cast<size_t>(i) < definition_levels.size()
                           && definition_levels[static_cast<size_t>(i)] < max_def_level);
    if (is_null) {
      values.push_back(std::string());
      nulls.push_back(true);
      continue;
    }
    if (value_index < decoded.size()) {
      values.push_back(decoded[value_index++]);
    } else {
      values.push_back(std::string());
    }
    nulls.push_back(false);
  }
  return true;
}

}  // namespace

FileParquetReader::FileParquetReader() {}

FileParquetReader::~FileParquetReader() {}

FileSourceError FileParquetReader::open(const std::string &path, const FileReadOptions &options)
{
  path_ = path;
  options_ = options;
  if (!get_file_signature(path_, signature_)) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot stat file '" + path + "'");
  }
  opened_ = true;
  return read_footer();
}

FileSourceError FileParquetReader::check_unchanged() const
{
  FileSignature current;
  if (!get_file_signature(path_, current)) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot stat file '" + path_ + "'");
  }
  if (!(current == signature_)) {
    return FileSourceError(FileErrorCode::FILE_CHANGED, "file '" + path_ + "' changed during the scan");
  }
  return FileSourceError();
}

FileSourceError FileParquetReader::read_footer()
{
  FILE *file = std::fopen(path_.c_str(), "rb");
  if (NULL == file) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot open file '" + path_ + "'");
  }
  FileSourceError result;
  std::vector<unsigned char> tail;
  if (!read_file_range(file, signature_.size - 8, 8, tail) || 0 != std::memcmp(&tail[4], kParquetMagic, 4)) {
    std::fclose(file);
    return FileSourceError(FileErrorCode::PARSE_ERROR, "the file '" + path_ + "' is not a parquet file");
  }
  uint32_t footer_size = 0;
  std::memcpy(&footer_size, &tail[0], 4);
  if (static_cast<int64_t>(footer_size) + 8 > signature_.size) {
    std::fclose(file);
    return FileSourceError(FileErrorCode::PARSE_ERROR, "the footer of '" + path_ + "' is corrupted");
  }
  if (!read_file_range(file, signature_.size - 8 - footer_size, footer_size, footer_)) {
    std::fclose(file);
    return FileSourceError(FileErrorCode::IO_ERROR, "cannot read the footer of '" + path_ + "'");
  }
  std::fclose(file);

  ThriftCompactReader reader(footer_.empty() ? NULL : &footer_[0], footer_.size());
  reader.push_struct();
  while (true) {
    int16_t field_id = 0;
    int8_t type = 0;
    bool is_stop = false;
    if (!reader.read_field_begin(field_id, type, is_stop)) {
      reader.pop_struct();
      return FileSourceError(FileErrorCode::PARSE_ERROR, "the metadata of '" + path_ + "' is corrupted");
    }
    if (is_stop) {
      break;
    }
    if (2 == field_id) {
      // the schema
      int8_t elem_type = 0;
      uint32_t count = 0;
      if (!reader.read_list_begin(elem_type, count)) {
        return FileSourceError(FileErrorCode::PARSE_ERROR, "the schema of '" + path_ + "' is corrupted");
      }
      for (uint32_t i = 0; i < count; ++i) {
        ParquetSchemaNode node;
        if (!read_schema_element(reader, node)) {
          return FileSourceError(FileErrorCode::PARSE_ERROR, "the schema of '" + path_ + "' is corrupted");
        }
        schema_nodes_.push_back(node);
      }
      continue;
    }
    if (3 == field_id) {
      int64_t value = 0;
      if (!reader.read_int64(value)) {
        return FileSourceError(FileErrorCode::PARSE_ERROR, "the metadata of '" + path_ + "' is corrupted");
      }
      num_rows_ = value;
      continue;
    }
    if (4 == field_id) {
      int8_t elem_type = 0;
      uint32_t count = 0;
      if (!reader.read_list_begin(elem_type, count)) {
        return FileSourceError(FileErrorCode::PARSE_ERROR, "the row groups of '" + path_ + "' are corrupted");
      }
      for (uint32_t i = 0; i < count; ++i) {
        ParquetRowGroup row_group;
        if (!read_row_group(reader, row_group)) {
          return FileSourceError(FileErrorCode::PARSE_ERROR, "the row groups of '" + path_ + "' are corrupted");
        }
        row_groups_.push_back(row_group);
      }
      continue;
    }
    if (!reader.skip_field(type)) {
      reader.pop_struct();
      return FileSourceError(FileErrorCode::PARSE_ERROR, "the metadata of '" + path_ + "' is corrupted");
    }
  }
  reader.pop_struct();
  // build the leaf columns of the file
  columns_.clear();
  for (size_t i = 0; i < schema_nodes_.size(); ++i) {
    const ParquetSchemaNode &node = schema_nodes_[i];
    if (node.num_children > 0) {
      continue;
    }
    if (i == 0) {
      // the root element is never a column
      continue;
    }
    ParquetColumnDesc column;
    column.name = node.name;
    column.type = node.type;
    column.type_length = node.type_length;
    column.repetition_type = node.repetition_type;
    column.converted_type = node.converted_type;
    column.logical_type_kind = node.logical_type_kind;
    column.max_def_level = (static_cast<int32_t>(ParquetRepetition::OPTIONAL) == node.repetition_type) ? 1 : 0;
    column.max_rep_level = (static_cast<int32_t>(ParquetRepetition::REPEATED) == node.repetition_type) ? 1 : 0;
    columns_.push_back(column);
  }
  if (columns_.empty()) {
    return FileSourceError(FileErrorCode::PARSE_ERROR, "the file '" + path_ + "' does not contain any column");
  }
  return result;
}

FileSourceError FileParquetReader::infer_schema(FileSchema &schema)
{
  schema.columns.clear();
  for (size_t i = 0; i < columns_.size(); ++i) {
    const ParquetColumnDesc &column = columns_[i];
    FileColumn file_column;
    file_column.name = column.name;
    file_column.nullable = (static_cast<int32_t>(ParquetRepetition::OPTIONAL) == column.repetition_type);
    switch (column.type) {
      case kTypeBoolean: file_column.type = ColumnType::BOOLEAN; break;
      case kTypeInt32: {
        file_column.type = (kConvertedDate == column.converted_type || kLogicalDate == column.logical_type_kind)
                               ? ColumnType::DATE
                               : ColumnType::BIGINT;
        break;
      }
      case kTypeInt64: {
        file_column.type = (kConvertedTimestampMillis == column.converted_type
                            || kConvertedTimestampMicros == column.converted_type
                            || kLogicalTimestamp == column.logical_type_kind)
                               ? ColumnType::DATETIME
                               : ColumnType::BIGINT;
        break;
      }
      case kTypeFloat:
      case kTypeDouble: file_column.type = ColumnType::DOUBLE; break;
      case kTypeByteArray:
      case kTypeFixedLenByteArray: file_column.type = ColumnType::VARCHAR; break;
      default: file_column.type = ColumnType::VARCHAR; break;
    }
    schema.columns.push_back(file_column);
  }
  schema_ = schema;
  return FileSourceError();
}

FileSourceError FileParquetReader::decode_row_group(int64_t row_group_index,
                                                    std::vector<std::vector<std::string>> &values,
                                                    std::vector<std::vector<bool>> &nulls) const
{
  if (row_group_index < 0 || static_cast<size_t>(row_group_index) >= row_groups_.size()) {
    return FileSourceError(FileErrorCode::PARSE_ERROR, "the row group does not exist");
  }
  const ParquetRowGroup &row_group = row_groups_[static_cast<size_t>(row_group_index)];
  FILE *file = std::fopen(path_.c_str(), "rb");
  if (NULL == file) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "cannot open file '" + path_ + "'");
  }
  values.clear();
  nulls.clear();
  for (size_t c = 0; c < columns_.size(); ++c) {
    const ParquetColumnDesc &column = columns_[c];
    if (c >= row_group.columns.size()) {
      std::fclose(file);
      return FileSourceError(FileErrorCode::PARSE_ERROR, "the row group does not contain all columns");
    }
    const ParquetColumnChunk &chunk = row_group.columns[c];
    const int64_t start = (chunk.dictionary_page_offset >= 0) ? chunk.dictionary_page_offset : chunk.data_page_offset;
    const int64_t end = chunk.data_page_offset + chunk.total_compressed_size;
    if (end <= start) {
      std::fclose(file);
      return FileSourceError(FileErrorCode::PARSE_ERROR, "the column chunk of '" + path_ + "' is corrupted");
    }
    std::vector<unsigned char> buffer;
    if (!read_file_range(file, start, end - start, buffer)) {
      std::fclose(file);
      return FileSourceError(FileErrorCode::IO_ERROR, "cannot read the column chunk of '" + path_ + "'");
    }
    std::fclose(file);

    std::vector<std::string> dictionary;
    std::vector<std::string> column_values;
    std::vector<bool> column_nulls;
    size_t pos = 0;
    int64_t values_read = 0;
    while (pos < buffer.size() && values_read < chunk.num_values) {
      ThriftCompactReader reader(&buffer[pos], buffer.size() - pos);
      PageHeaderInfo header;
      if (!read_page_header(reader, header)) {
        return FileSourceError(FileErrorCode::PARSE_ERROR, "the page header of '" + path_ + "' is corrupted");
      }
      const size_t header_size = reader.position();
      pos += header_size;
      if (header.compressed_page_size < 0
          || pos + static_cast<size_t>(header.compressed_page_size) > buffer.size()) {
        return FileSourceError(FileErrorCode::PARSE_ERROR, "the page of '" + path_ + "' is truncated");
      }
      std::vector<unsigned char> page(buffer.begin() + static_cast<long>(pos),
                                      buffer.begin() + static_cast<long>(pos + header.compressed_page_size));
      pos += static_cast<size_t>(header.compressed_page_size);
      if (kPageTypeDictionary == header.page_type) {
        // the dictionary page holds the plain encoded dictionary values
        std::vector<unsigned char> plain;
        if (static_cast<int32_t>(ParquetCodec::SNAPPY) == chunk.codec) {
          if (!snappy_uncompress(page.empty() ? NULL : &page[0], page.size(), plain)) {
            return FileSourceError(FileErrorCode::PARSE_ERROR, "the dictionary page of '" + path_ + "' is corrupted");
          }
        } else {
          plain = page;
        }
        if (!decode_plain_values(plain.empty() ? NULL : &plain[0],
                                 plain.size(),
                                 column.type,
                                 column.type_length,
                                 column.converted_type,
                                 header.num_values,
                                 dictionary)) {
          return FileSourceError(FileErrorCode::PARSE_ERROR, "the dictionary page of '" + path_ + "' is corrupted");
        }
        continue;
      }
      if (kPageTypeData != header.page_type && kPageTypeDataV2 != header.page_type) {
        // index pages and unknown pages are ignored
        continue;
      }
      const bool is_v2 = (kPageTypeDataV2 == header.page_type);
      std::vector<unsigned char> payload;
      if (is_v2) {
        const size_t levels_size = static_cast<size_t>(header.repetition_levels_byte_length)
                                   + static_cast<size_t>(header.definition_levels_byte_length);
        if (levels_size > page.size()) {
          return FileSourceError(FileErrorCode::PARSE_ERROR, "the page of '" + path_ + "' is truncated");
        }
        const unsigned char *data = page.data() + levels_size;
        const size_t data_size = page.size() - levels_size;
        if (static_cast<int32_t>(ParquetCodec::SNAPPY) == chunk.codec && header.is_compressed) {
          if (!snappy_uncompress(data, data_size, payload)) {
            return FileSourceError(FileErrorCode::PARSE_ERROR, "the page of '" + path_ + "' is corrupted");
          }
        } else {
          payload.assign(data, data + data_size);
        }
        // rebuild the page with the uncompressed values, the levels are kept
        std::vector<unsigned char> rebuilt;
        rebuilt.reserve(page.size());
        rebuilt.insert(rebuilt.end(), page.begin(), page.begin() + static_cast<long>(levels_size));
        rebuilt.insert(rebuilt.end(), payload.begin(), payload.end());
        std::string error;
        if (!decode_data_page(rebuilt.data(),
                              rebuilt.size(),
                              header,
                              true,
                              column,
                              dictionary,
                              column_values,
                              column_nulls,
                              error)) {
          return FileSourceError(FileErrorCode::PARSE_ERROR, error);
        }
      } else {
        std::vector<unsigned char> payload;
        if (static_cast<int32_t>(ParquetCodec::SNAPPY) == chunk.codec) {
          if (!snappy_uncompress(page.empty() ? NULL : &page[0], page.size(), payload)) {
            return FileSourceError(FileErrorCode::PARSE_ERROR, "the page of '" + path_ + "' is corrupted");
          }
        } else {
          payload = page;
        }
        std::string error;
        if (!decode_data_page(payload.empty() ? NULL : &payload[0],
                              payload.size(),
                              header,
                              false,
                              column,
                              dictionary,
                              column_values,
                              column_nulls,
                              error)) {
          return FileSourceError(FileErrorCode::PARSE_ERROR, error);
        }
      }
      values_read += header.num_values;
    }
    values.push_back(column_values);
    nulls.push_back(column_nulls);
  }
  return FileSourceError();
}

bool FileParquetReader::next_row(FileRow &row, FileSourceError &error)
{
  if (!opened_) {
    error = FileSourceError(FileErrorCode::FILE_NOT_FOUND, "the parquet file was not opened");
    return false;
  }
  while (current_row_ >= current_row_count_) {
    if (current_group_ >= static_cast<int64_t>(row_groups_.size())) {
      error = FileSourceError();
      return false;
    }
    std::vector<std::vector<std::string>> values;
    std::vector<std::vector<bool>> nulls;
    FileSourceError decode_error = decode_row_group(current_group_, values, nulls);
    if (!decode_error.is_ok()) {
      error = decode_error;
      return false;
    }
    current_values_.swap(values);
    current_nulls_.swap(nulls);
    current_row_count_ = row_groups_[static_cast<size_t>(current_group_)].num_rows;
    current_row_ = 0;
    ++current_group_;
  }
  row.values.clear();
  row.nulls.clear();
  for (size_t c = 0; c < columns_.size(); ++c) {
    const size_t index = static_cast<size_t>(current_row_);
    if (index >= current_values_[c].size()) {
      row.values.push_back("");
      row.nulls.push_back(true);
      continue;
    }
    row.values.push_back(current_values_[c][index]);
    row.nulls.push_back(current_nulls_[c][index]);
  }
  ++current_row_;
  error = FileSourceError();
  return true;
}

FileSourceError FileParquetReader::scan(const FileRowCallback &callback)
{
  if (!opened_) {
    return FileSourceError(FileErrorCode::FILE_NOT_FOUND, "the parquet file was not opened");
  }
  int64_t emitted = 0;
  for (size_t g = 0; g < row_groups_.size(); ++g) {
    std::vector<std::vector<std::string>> values;
    std::vector<std::vector<bool>> nulls;
    FileSourceError error = decode_row_group(static_cast<int64_t>(g), values, nulls);
    if (!error.is_ok()) {
      return error;
    }
    const int64_t row_count = row_groups_[g].num_rows;
    FileRow row;
    for (int64_t r = 0; r < row_count; ++r) {
      row.values.clear();
      row.nulls.clear();
      for (size_t c = 0; c < columns_.size(); ++c) {
        const size_t index = static_cast<size_t>(r);
        if (index >= values[c].size()) {
          row.values.push_back("");
          row.nulls.push_back(true);
          continue;
        }
        row.values.push_back(values[c][index]);
        row.nulls.push_back(nulls[c][index]);
      }
      ++emitted;
      if (options_.max_row_count > 0 && emitted > options_.max_row_count) {
        return FileSourceError(FileErrorCode::LIMIT_EXCEEDED, "the row limit of the file scan was exceeded");
      }
      if (!callback(row)) {
        return FileSourceError();
      }
    }
  }
  return FileSourceError();
}

}  // namespace file_source
}  // namespace sql
}  // namespace oceanbase
