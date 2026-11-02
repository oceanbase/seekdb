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

#include "ob_file_scan_utils.h"
#include "lib/file/ob_file.h"
#include "lib/string/ob_string.h"
#include "common/ob_common_utility.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace oceanbase
{
namespace sql
{

int FileScanUtils::infer_schema_from_csv(const common::ObString &file_path,
                                          const FileScanOption &option,
                                          common::ObIArray<ColumnMeta> &schema)
{
  int ret = OB_SUCCESS;
  std::ifstream file(file_path.ptr());
  
  if (!file.is_open()) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("failed to open file", K(file_path), K(ret));
  } else {
    std::string line;
    int64_t line_count = 0;
    common::ObSEArray<common::ObObjType, 16> column_types;
    common::ObSEArray<common::ObString, 16> column_names;
    
    // Read header if present
    if (option.has_header_ && std::getline(file, line)) {
      std::stringstream ss(line);
      std::string col_name;
      while (std::getline(ss, col_name, option.field_delimiter_)) {
        // Remove quotes if present
        if (col_name.size() >= 2 && col_name.front() == option.quote_char_ && 
            col_name.back() == option.quote_char_) {
          col_name = col_name.substr(1, col_name.size() - 2);
        }
        column_names.push_back(common::ObString(col_name.c_str()));
        column_types.push_back(common::ObNullType);
      }
      line_count++;
    }
    
    // Sample rows for type inference
    while (std::getline(file, line) && 
           (option.sample_size_ == -1 || line_count < option.sample_size_ + 1)) {
      if (line.empty()) continue;
      
      std::stringstream ss(line);
      std::string field;
      int64_t col_idx = 0;
      
      while (std::getline(ss, field, option.field_delimiter_) && col_idx < column_types.count()) {
        common::ObObjType inferred_type = common::ObVarcharType;
        
        // Try to infer type
        if (field.empty() || field == "NULL") {
          inferred_type = common::ObNullType;
        } else {
          // Try integer
          try {
            size_t pos;
            int64_t val = std::stoll(field, &pos);
            if (pos == field.size()) {
              inferred_type = common::ObInt64Type;
            }
          } catch (...) {}
          
          // Try double if not integer
          if (inferred_type == common::ObVarcharType) {
            try {
              size_t pos;
              double val = std::stod(field, &pos);
              if (pos == field.size()) {
                inferred_type = common::ObDoubleType;
              }
            } catch (...) {}
          }
          
          // Check boolean
          if (inferred_type == common::ObVarcharType) {
            std::string lower_field = field;
            std::transform(lower_field.begin(), lower_field.end(), lower_field.begin(), ::tolower);
            if (lower_field == "true" || lower_field == "false") {
              inferred_type = common::ObBoolType;
            }
          }
        }
        
        // Widen type if needed
        if (column_types.at(col_idx) == common::ObNullType) {
          column_types.at(col_idx) = inferred_type;
        } else if (inferred_type != common::ObNullType) {
          common::ObObjType widened_type;
          if (OB_FAIL(widen_type(column_types.at(col_idx), inferred_type, widened_type))) {
            LOG_WARN("failed to widen type", K(ret));
          } else {
            column_types.at(col_idx) = widened_type;
          }
        }
        
        col_idx++;
      }
      line_count++;
    }
    
    // Build schema
    for (int64_t i = 0; OB_SUCC(ret) && i < column_names.count(); i++) {
      ColumnMeta meta;
      meta.name_ = column_names.at(i);
      meta.type_ = column_types.at(i) == common::ObNullType ? common::ObVarcharType : column_types.at(i);
      meta.nullable_ = true;
      meta.max_length_ = 0;
      
      if (OB_FAIL(schema.push_back(meta))) {
        LOG_WARN("failed to push back column meta", K(ret));
      }
    }
  }
  
  return ret;
}

int FileScanUtils::infer_schema_from_jsonl(const common::ObString &file_path,
                                            const FileScanOption &option,
                                            common::ObIArray<ColumnMeta> &schema)
{
  int ret = OB_SUCCESS;
  // TODO: Implement JSONL schema inference
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("JSONL schema inference not yet implemented");
  return ret;
}

int FileScanUtils::infer_schema_from_parquet(const common::ObString &file_path,
                                              const FileScanOption &option,
                                              common::ObIArray<ColumnMeta> &schema)
{
  int ret = OB_SUCCESS;
  // TODO: Implement Parquet schema inference
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("Parquet schema inference not yet implemented");
  return ret;
}

int FileScanUtils::widen_type(common::ObObjType type1, common::ObObjType type2, common::ObObjType &result)
{
  int ret = OB_SUCCESS;
  
  if (type1 == type2) {
    result = type1;
  } else if (type1 == common::ObNullType) {
    result = type2;
  } else if (type2 == common::ObNullType) {
    result = type1;
  } else if ((type1 == common::ObInt64Type && type2 == common::ObDoubleType) ||
             (type1 == common::ObDoubleType && type2 == common::ObInt64Type)) {
    result = common::ObDoubleType;
  } else {
    // Default to varchar for incompatible types
    result = common::ObVarcharType;
  }
  
  return ret;
}

bool FileScanUtils::is_valid_path(const common::ObString &path, const common::ObString &secure_path)
{
  if (secure_path.empty()) {
    return false;
  }
  
  // Check if path is within secure_path
  std::string path_str(path.ptr());
  std::string secure_str(secure_path.ptr());
  
  // Simple check: path should start with secure_path
  return path_str.find(secure_str) == 0;
}

int FileScanUtils::check_file_accessible(const common::ObString &file_path)
{
  int ret = OB_SUCCESS;
  std::ifstream file(file_path.ptr());
  
  if (!file.is_open()) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("file is not accessible", K(file_path), K(ret));
  }
  
  return ret;
}

} // namespace sql
} // namespace oceanbase
