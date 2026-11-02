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

#include "ob_expr_read_csv.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/config/ob_server_config.h"
#include <sstream>
#include <algorithm>

namespace oceanbase
{
namespace sql
{

ObExprReadCSV::ObExprReadCSV(common::ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_READ_CSV, "read_csv", 1, 3)
{}

ObExprReadCSV::~ObExprReadCSV()
{}

int ObExprReadCSV::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types_array,
                                      int64_t param_num,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  
  if (OB_UNLIKELY(param_num < 1 || param_num > 3)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid parameter number", K(param_num), K(ret));
  } else if (OB_UNLIKELY(!types_array[FILE_PATH_IDX].is_string_type())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file path must be string type", K(ret));
  } else {
    // Return type is dynamic based on CSV schema
    // For now, return JSON array as placeholder
    type.set_json();
    type.set_collation_type(common::CS_TYPE_BINARY);
  }
  
  return ret;
}

int ObExprReadCSV::eval_read_csv(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *file_path_datum = nullptr;
  
  if (OB_FAIL(expr.args_[FILE_PATH_IDX]->eval(ctx, file_path_datum))) {
    LOG_WARN("failed to eval file path", K(ret));
  } else if (OB_ISNULL(file_path_datum) || file_path_datum->is_null()) {
    res.set_null();
  } else {
    common::ObString file_path = file_path_datum->get_string();
    
    // Check if file SQL is enabled
    ObSQLSessionInfo *session = ctx.exec_ctx_.get_sql_session_info();
    bool enable_file_sql = false;
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is null", K(ret));
    } else if (OB_FAIL(session->get_sys_variable(common::OB_SV_ENABLE_FILE_SQL, enable_file_sql))) {
      LOG_WARN("failed to get enable_file_sql", K(ret));
    } else if (!enable_file_sql) {
      ret = OB_ERR_FILE_SQL_DISABLED;
      LOG_USER_ERROR(OB_ERR_FILE_SQL_DISABLED, "read_csv");
    } else {
      // Check secure_file_priv
      common::ObString secure_path;
      if (OB_FAIL(session->get_sys_variable(common::OB_SV_SECURE_FILE_PRIV, secure_path))) {
        LOG_WARN("failed to get secure_file_priv", K(ret));
      } else if (!FileScanUtils::is_valid_path(file_path, secure_path)) {
        ret = OB_ERR_FILE_PATH_NOT_ALLOWED;
        LOG_USER_ERROR(OB_ERR_FILE_PATH_NOT_ALLOWED, file_path);
      } else {
        // Read CSV file
        FileScanOption option;
        option.format_ = FileFormat::CSV;
        
        // Parse optional parameters
        if (expr.args_.count() > HAS_HEADER_IDX) {
          ObDatum *has_header_datum = nullptr;
          if (OB_FAIL(expr.args_[HAS_HEADER_IDX]->eval(ctx, has_header_datum))) {
            LOG_WARN("failed to eval has_header", K(ret));
          } else if (!has_header_datum->is_null()) {
            option.has_header_ = has_header_datum->get_bool();
          }
        }
        
        if (OB_SUCC(ret) && expr.args_.count() > DELIMITER_IDX) {
          ObDatum *delimiter_datum = nullptr;
          if (OB_FAIL(expr.args_[DELIMITER_IDX]->eval(ctx, delimiter_datum))) {
            LOG_WARN("failed to eval delimiter", K(ret));
          } else if (!delimiter_datum->is_null()) {
            common::ObString delimiter = delimiter_datum->get_string();
            if (delimiter.length() > 0) {
              option.field_delimiter_ = delimiter.ptr()[0];
            }
          }
        }
        
        if (OB_SUCC(ret)) {
          // Create scanner and read data
          ObCSVFileScanner scanner;
          if (OB_FAIL(scanner.init(file_path, option))) {
            LOG_WARN("failed to init CSV scanner", K(file_path), K(ret));
          } else {
            // For now, return a simple JSON representation
            // TODO: Implement proper table function that returns rows
            common::ObString result_str("{\"status\": \"success\", \"file\": \"");
            result_str.append(file_path);
            result_str.append("\"}");
            
            if (OB_FAIL(res.set_string(result_str))) {
              LOG_WARN("failed to set result", K(ret));
            }
            
            scanner.close();
          }
        }
      }
    }
  }
  
  return ret;
}

int ObExprReadCSV::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  rt_expr.eval_func_ = ObExprReadCSV::eval_read_csv;
  return ret;
}

// ObCSVFileScanner implementation
ObCSVFileScanner::ObCSVFileScanner()
  : has_initialized_(false),
    row_count_(0)
{}

ObCSVFileScanner::~ObCSVFileScanner()
{
  close();
}

int ObCSVFileScanner::init(const common::ObString &file_path, const FileScanOption &option)
{
  int ret = OB_SUCCESS;
  
  if (has_initialized_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("scanner already initialized", K(ret));
  } else {
    file_.open(file_path.ptr());
    if (!file_.is_open()) {
      ret = OB_FILE_NOT_EXIST;
      LOG_WARN("failed to open file", K(file_path), K(ret));
    } else {
      option_ = option;
      
      // Infer schema
      if (OB_FAIL(FileScanUtils::infer_schema_from_csv(file_path, option, schema_))) {
        LOG_WARN("failed to infer schema", K(ret));
      } else {
        has_initialized_ = true;
        row_count_ = 0;
        
        // Skip header line if present
        if (option.has_header_) {
          std::string line;
          std::getline(file_, line);
        }
      }
    }
  }
  
  return ret;
}

int ObCSVFileScanner::get_next_row(common::ObIArray<common::ObObj> &row)
{
  int ret = OB_SUCCESS;
  
  if (!has_initialized_) {
    ret = OB_NOT_INIT;
    LOG_WARN("scanner not initialized", K(ret));
  } else {
    std::string line;
    if (std::getline(file_, line)) {
      common::ObSEArray<common::ObString, 16> fields;
      if (OB_FAIL(parse_line(line, fields))) {
        LOG_WARN("failed to parse line", K(ret));
      } else {
        row.reset();
        for (int64_t i = 0; OB_SUCC(ret) && i < fields.count() && i < schema_.count(); i++) {
          common::ObObj obj;
          common::ObObjType type;
          if (OB_FAIL(infer_type(fields.at(i), type))) {
            LOG_WARN("failed to infer type", K(ret));
          } else {
            // Convert based on schema type
            if (schema_.at(i).type_ == common::ObInt64Type) {
              obj.set_int(std::stoll(fields.at(i).ptr()));
            } else if (schema_.at(i).type_ == common::ObDoubleType) {
              obj.set_double(std::stod(fields.at(i).ptr()));
            } else if (schema_.at(i).type_ == common::ObBoolType) {
              std::string lower_val = fields.at(i).ptr();
              std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), ::tolower);
              obj.set_bool(lower_val == "true");
            } else {
              obj.set_varchar(fields.at(i));
            }
            
            if (OB_FAIL(row.push_back(obj))) {
              LOG_WARN("failed to push back obj", K(ret));
            }
          }
        }
        row_count_++;
      }
    } else {
      ret = OB_ITER_END;
    }
  }
  
  return ret;
}

int ObCSVFileScanner::parse_line(const std::string &line, common::ObIArray<common::ObString> &fields)
{
  int ret = OB_SUCCESS;
  fields.reset();
  
  std::stringstream ss(line);
  std::string field;
  bool in_quotes = false;
  std::string current_field;
  
  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    
    if (c == option_.quote_char_) {
      if (in_quotes && i + 1 < line.size() && line[i + 1] == option_.quote_char_) {
        // Escaped quote
        current_field += c;
        i++;
      } else {
        in_quotes = !in_quotes;
      }
    } else if (c == option_.field_delimiter_ && !in_quotes) {
      fields.push_back(common::ObString(current_field.c_str()));
      current_field.clear();
    } else if (c == option_.escape_char_ && i + 1 < line.size()) {
      current_field += line[++i];
    } else {
      current_field += c;
    }
  }
  
  // Add last field
  fields.push_back(common::ObString(current_field.c_str()));
  
  return ret;
}

int ObCSVFileScanner::infer_type(const common::ObString &value, common::ObObjType &type)
{
  int ret = OB_SUCCESS;
  
  if (value.empty() || value == "NULL") {
    type = common::ObNullType;
  } else {
    // Try integer
    try {
      size_t pos;
      std::stoll(value.ptr(), &pos);
      if (pos == value.length()) {
        type = common::ObInt64Type;
        return ret;
      }
    } catch (...) {}
    
    // Try double
    try {
      size_t pos;
      std::stod(value.ptr(), &pos);
      if (pos == value.length()) {
        type = common::ObDoubleType;
        return ret;
      }
    } catch (...) {}
    
    // Check boolean
    std::string lower_val = value.ptr();
    std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), ::tolower);
    if (lower_val == "true" || lower_val == "false") {
      type = common::ObBoolType;
      return ret;
    }
    
    // Default to varchar
    type = common::ObVarcharType;
  }
  
  return ret;
}

int ObCSVFileScanner::get_schema(common::ObIArray<ColumnMeta> &schema)
{
  int ret = OB_SUCCESS;
  
  if (!has_initialized_) {
    ret = OB_NOT_INIT;
    LOG_WARN("scanner not initialized", K(ret));
  } else {
    schema.reset();
    for (int64_t i = 0; OB_SUCC(ret) && i < schema_.count(); i++) {
      if (OB_FAIL(schema.push_back(schema_.at(i)))) {
        LOG_WARN("failed to push back schema", K(ret));
      }
    }
  }
  
  return ret;
}

void ObCSVFileScanner::reset()
{
  if (file_.is_open()) {
    file_.clear();
    file_.seekg(0);
    
    // Skip header if present
    if (option_.has_header_) {
      std::string line;
      std::getline(file_, line);
    }
    
    row_count_ = 0;
  }
}

void ObCSVFileScanner::close()
{
  if (file_.is_open()) {
    file_.close();
  }
  has_initialized_ = false;
}

} // namespace sql
} // namespace oceanbase
