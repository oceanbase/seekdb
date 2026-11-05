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
#include "ob_file_table_iterator.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObFileTableIterator::ObFileTableIterator(ObIAllocator& alloc)
    : allocator_(alloc),
      current_row_idx_(0),
      column_count_(0)
{
}

ObFileTableIterator::~ObFileTableIterator()
{
  close();
}

int ObFileTableIterator::init(const ObString& file_path,
                              const CSVOptions& csv_opts,
                              ObIAllocator& allocator)
{
  int ret = OB_SUCCESS;

  file_path_ = file_path;
  csv_opts_ = csv_opts;
  current_row_idx_ = 0;

  // Validate path
  if (OB_FAIL(ObFileReader::validate_path(file_path))) {
    LOG_WARN("Invalid file path", K(ret), K(file_path));
  } else if (OB_FAIL(ObFileReader::check_file_permissions(file_path))) {
    LOG_WARN("File permission check failed", K(ret), K(file_path));
  } else {
    // Read file content
    ObString content;
    if (OB_FAIL(ObFileReader::read_file(file_path, allocator, content))) {
      LOG_WARN("Failed to read file", K(ret), K(file_path));
    } else {
      file_content_ = content;

      // Parse CSV
      ObSEArray<ObString, 16> rows;
      if (OB_FAIL(ObCSVParser::parse(content, csv_opts, allocator, rows, headers_))) {
        LOG_WARN("Failed to parse CSV", K(ret), K(file_path));
      } else {
        data_rows_ = rows;
        column_count_ = headers_.count();

        // Infer schema
        if (column_count_ > 0 && !data_rows_.empty()) {
          if (OB_FAIL(ObCSVParser::infer_schema(data_rows_, csv_opts, allocator, column_types_))) {
            LOG_WARN("Failed to infer schema", K(ret));
          } else if (column_types_.count() != column_count_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Column count mismatch after inference",
                     K(column_types_.count()), K(column_count_));
          }
        }

        // Initialize row cells
        if (OB_SUCC(ret)) {
          row_cells_.reset();
          for (int64_t i = 0; i < column_count_ && OB_SUCC(ret); i++) {
            ObObj cell;
            cell.set_null();
            if (OB_FAIL(row_cells_.push_back(cell))) {
              LOG_WARN("Failed to initialize row cell", K(ret));
            }
          }
          current_row_.cells_ = row_cells_.array();
          current_row_.count_ = column_count_;
        }
      }
    }
  }

  return ret;
}

int ObFileTableIterator::get_next_row(const ObNewRow*& row)
{
  int ret = OB_ITER_END;

  if (current_row_idx_ >= data_rows_.count()) {
    row = NULL;
    ret = OB_ITER_END;
  } else {
    // Parse current row
    ObSEArray<ObString, 16> fields;
    if (OB_FAIL(ObCSVParser::parse_row(data_rows_[current_row_idx_],
                                       csv_opts_,
                                       allocator_,
                                       fields))) {
      LOG_WARN("Failed to parse row", K(ret), K(current_row_idx_));
    } else if (fields.count() != column_count_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Field count mismatch", K(fields.count()), K(column_count_));
    } else {
      // Convert each field to appropriate type
      for (int64_t i = 0; i < column_count_ && OB_SUCC(ret); i++) {
        ObObj obj;
        if (OB_FAIL(convert_value(fields[i], column_types_[i], obj))) {
          LOG_WARN("Failed to convert value", K(ret), K(i), K(fields[i]));
        } else {
          row_cells_[i] = obj;
        }
      }

      if (OB_SUCC(ret)) {
        row = &current_row_;
        current_row_idx_++;
        ret = OB_SUCCESS;
      }
    }
  }

  return ret;
}

int ObFileTableIterator::convert_value(const ObString& str_val,
                                       ObObjType target_type,
                                       ObObj& obj)
{
  int ret = OB_SUCCESS;

  if (str_val.empty()) {
    obj.set_null();
    return ret;
  }

  switch (target_type) {
    case ObIntType: {
      int64_t val = 0;
      if (str_val.to_int(val)) {
        obj.set_int(val);
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Failed to convert to int", K(str_val));
      }
      break;
    }
    case ObDoubleType: {
      double val = 0.0;
      if (str_val.to_double(val)) {
        obj.set_double(val);
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Failed to convert to double", K(str_val));
      }
      break;
    }
    case ObTinyIntType: {
      // Boolean conversion
      int8_t val = 0;
      if (str_val.case_compare("true") == 0) {
        val = 1;
      } else if (str_val.case_compare("false") == 0) {
        val = 0;
      } else {
        // Try numeric
        int64_t num = 0;
        if (str_val.to_int(num)) {
          val = (num != 0) ? 1 : 0;
        } else {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("Failed to convert to boolean", K(str_val));
        }
      }
      if (OB_SUCC(ret)) {
        obj.set_tinyint(val);
      }
      break;
    }
    case ObVarcharType:
    default: {
      obj.set_varchar(str_val);
      obj.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      break;
    }
  }

  return ret;
}

int ObFileTableIterator::get_column_name(int64_t idx, ObString& name) const
{
  int ret = OB_SUCCESS;
  if (idx < 0 || idx >= headers_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid column index", K(idx), K(headers_.count()));
  } else {
    name = headers_[idx];
  }
  return ret;
}

int ObFileTableIterator::get_column_type(int64_t idx, ObObjType& type) const
{
  int ret = OB_SUCCESS;
  if (idx < 0 || idx >= column_types_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid column index", K(idx), K(column_types_.count()));
  } else {
    type = column_types_[idx];
  }
  return ret;
}

void ObFileTableIterator::reset()
{
  current_row_idx_ = 0;
  row_cells_.reset();
  if (column_count_ > 0) {
    for (int64_t i = 0; i < column_count_; i++) {
      ObObj cell;
      cell.set_null();
      row_cells_.push_back(cell);
    }
    current_row_.cells_ = row_cells_.array();
    current_row_.count_ = column_count_;
  }
}

void ObFileTableIterator::reuse()
{
  reset();
}

void ObFileTableIterator::close()
{
  headers_.reset();
  data_rows_.reset();
  column_types_.reset();
  row_cells_.reset();
  current_row_idx_ = 0;
  column_count_ = 0;
}

} // namespace sql
} // namespace oceanbase
