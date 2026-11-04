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
#include "sql/engine/ob_file_table_iterator.h"
#include "lib/utility/utility.h"
#include "common/session/ob_session_context.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprReadCSV::ObExprReadCSV(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_READ_CSV,
                         N_READ_CSV,
                         MORE_THAN_ZERO,
                         VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprReadCSV::~ObExprReadCSV()
{
}

int ObExprReadCSV::calc_result_typeN(ObExprResType &type,
                                     ObExprResType *types_array,
                                     int64_t param_num,
                                     common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  // First parameter must be string (file path)
  if (!ob_is_string_tc(types_array[0].get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP, "read_csv", "file path must be string");
  } else {
    types_array[0].set_calc_type(ObVarcharType);
    types_array[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  }

  // Optional parameters: has_header (bool), delimiter (char/string), sample_size (int)
  for (int64_t i = 1; i < param_num && OB_SUCC(ret); i++) {
    if (ob_is_string_tc(types_array[i].get_type())) {
      types_array[i].set_calc_type(ObVarcharType);
      types_array[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (ob_is_integer_type(types_array[i].get_type())) {
      types_array[i].set_calc_type(ObIntType);
    } else if (ob_is_bool(types_array[i].get_type())) {
      types_array[i].set_calc_type(ObTinyIntType);
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP, "read_csv", "invalid parameter type");
    }
  }

  // Return type is a virtual table - represented as JSON array of objects
  if (OB_SUCC(ret)) {
    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
  }
  return ret;
}

int ObExprReadCSV::eval_read_csv(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObDatum *file_path_datum = NULL;
  ObString file_path;
  CSVOptions csv_opts;

  // Check enable_file_sql session variable
  ObSQLSessionInfo* session = ctx.exec_ctx_.get_my_session();
  if (NULL == session) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Session is NULL", K(ret));
  } else {
    // Default: file SQL is disabled. Can be enabled via session variable
    bool enable_file_sql = false;
    // TODO: integrate with system variable when available
    if (!enable_file_sql) {
      ret = OB_PRIV_CHECK_FAIL;
      LOG_USER_ERROR(OB_PRIV_CHECK_FAIL, "file SQL is disabled. This feature requires enable_file_sql=ON");
      res.set_null();
    }
  }

  // Evaluate file path parameter
  if (OB_SUCC(ret) && OB_FAIL(expr.eval_param_value(ctx, file_path_datum))) {
    LOG_WARN("Failed to evaluate file path parameter", K(ret));
  } else if (file_path_datum->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_csv", "file path cannot be null");
    res.set_null();
  } else {
    file_path = file_path_datum->get_string();
    if (file_path.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "read_csv", "file path cannot be empty");
      res.set_null();
    }
  }

  // Parse optional parameters
  if (OB_SUCC(ret)) {
    int64_t param_count = expr.arg_cnt_;

    // Parameter 2: has_header (boolean, default true)
    if (param_count > 1) {
      ObDatum* header_datum = NULL;
      if (OB_FAIL(expr.eval_param_value(ctx, header_datum, 1))) {
        LOG_WARN("Failed to evaluate has_header parameter", K(ret));
      } else if (!header_datum->is_null()) {
        csv_opts.has_header = (header_datum->get_int() != 0);
      }
    }

    // Parameter 3: delimiter (string, default ',')
    if (param_count > 2 && OB_SUCC(ret)) {
      ObDatum* delim_datum = NULL;
      if (OB_FAIL(expr.eval_param_value(ctx, delim_datum, 2))) {
        LOG_WARN("Failed to evaluate delimiter parameter", K(ret));
      } else if (!delim_datum->is_null()) {
        ObString delim_str = delim_datum->get_string();
        if (!delim_str.empty()) {
          csv_opts.delimiter = delim_str[0];
        }
      }
    }

    // Parameter 4: quote_char (string, default '"')
    if (param_count > 3 && OB_SUCC(ret)) {
      ObDatum* quote_datum = NULL;
      if (OB_FAIL(expr.eval_param_value(ctx, quote_datum, 3))) {
        LOG_WARN("Failed to evaluate quote_char parameter", K(ret));
      } else if (!quote_datum->is_null()) {
        ObString quote_str = quote_datum->get_string();
        if (!quote_str.empty()) {
          csv_opts.quote_char = quote_str[0];
        }
      }
    }

    // Parameter 5: sample_size (int, default -1 for all rows)
    if (param_count > 4 && OB_SUCC(ret)) {
      ObDatum* sample_datum = NULL;
      if (OB_FAIL(expr.eval_param_value(ctx, sample_datum, 4))) {
        LOG_WARN("Failed to evaluate sample_size parameter", K(ret));
      } else if (!sample_datum->is_null()) {
        csv_opts.sample_size = sample_datum->get_int();
      }
    }
  }

  // Create and initialize file table iterator
  ObFileTableIterator* file_iter = NULL;
  if (OB_SUCC(ret)) {
    file_iter = OB_NEWx(ObFileTableIterator, &(ctx.get_allocator()), ctx.get_allocator());
    if (NULL == file_iter) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to allocate file iterator", K(ret));
    } else if (OB_FAIL(file_iter->init(file_path, csv_opts, ctx.get_allocator()))) {
      LOG_WARN("Failed to initialize file iterator", K(ret), K(file_path));
      OB_DELETEx(ObFileTableIterator, file_iter);
      file_iter = NULL;
    }
  }

  // Collect all rows into JSON array
  if (OB_SUCC(ret)) {
    ObSEArray<ObString, 1024> row_jsons;
    const ObNewRow* row = NULL;
    int64_t row_count = 0;

    // Get column information
    int64_t col_count = file_iter->get_column_count();
    ObSEArray<ObString, 16> col_names;
    ObSEArray<ObObjType, 16> col_types;

    for (int64_t i = 0; i < col_count && OB_SUCC(ret); i++) {
      ObString col_name;
      ObObjType col_type;
      if (OB_FAIL(file_iter->get_column_name(i, col_name))) {
        LOG_WARN("Failed to get column name", K(ret), K(i));
      } else if (OB_FAIL(col_names.push_back(col_name))) {
        LOG_WARN("Failed to store column name", K(ret));
      } else if (OB_FAIL(file_iter->get_column_type(i, col_type))) {
        LOG_WARN("Failed to get column type", K(ret), K(i));
      } else if (OB_FAIL(col_types.push_back(col_type))) {
        LOG_WARN("Failed to store column type", K(ret));
      }
    }

    // Iterate through all rows
    while (OB_SUCC(ret) && OB_SUCC(file_iter->get_next_row(row))) {
      if (NULL == row) {
        break;
      }

      // Build JSON object for this row: {"col1": val1, "col2": val2, ...}
      ObString json_obj = "{";
      for (int64_t i = 0; i < col_count && OB_SUCC(ret); i++) {
        if (i > 0) {
          json_obj += ",";
        }

        // Add column name
        json_obj += "\"";
        json_obj += col_names[i];
        json_obj += "\":";

        // Add value based on type
        const ObObj& cell = row->cells_[i];
        if (cell.is_null()) {
          json_obj += "null";
        } else {
          switch (col_types[i]) {
            case ObIntType: {
              int64_t val = cell.get_int();
              char buf[32];
              int len = snprintf(buf, sizeof(buf), "%ld", val);
              json_obj += ObString(len, buf);
              break;
            }
            case ObDoubleType: {
              double val = cell.get_double();
              char buf[64];
              int len = snprintf(buf, sizeof(buf), "%.17g", val);
              json_obj += ObString(len, buf);
              break;
            }
            case ObTinyIntType: {
              int8_t val = cell.get_tinyint();
              json_obj += (val != 0) ? "true" : "false";
              break;
            }
            case ObVarcharType:
            default: {
              ObString val = cell.get_varchar();
              json_obj += "\"";
              // Escape special characters in JSON string
              for (int64_t j = 0; j < val.length(); j++) {
                char c = val[j];
                if (c == '"') {
                  json_obj += "\\\"";
                } else if (c == '\\') {
                  json_obj += "\\\\";
                } else if (c == '\n') {
                  json_obj += "\\n";
                } else if (c == '\r') {
                  json_obj += "\\r";
                } else if (c == '\t') {
                  json_obj += "\\t";
                } else {
                  json_obj += ObString(1, &c);
                }
              }
              json_obj += "\"";
              break;
            }
          }
        }
      }
      json_obj += "}";

      if (OB_SUCC(ret) && OB_FAIL(row_jsons.push_back(json_obj))) {
        LOG_WARN("Failed to add row JSON", K(ret));
      }
      row_count++;
    }

    // Build final JSON array: [row1, row2, ...]
    if (OB_SUCC(ret)) {
      ObString result_json = "[";
      for (int64_t i = 0; i < row_jsons.count(); i++) {
        if (i > 0) {
          result_json += ",";
        }
        result_json += row_jsons[i];
      }
      result_json += "]";

      // Set result
      res.set_json(result_json);
    }

    OB_DELETEx(ObFileTableIterator, file_iter);
  }

  if (OB_FAIL(ret) && NULL != file_iter) {
    OB_DELETEx(ObFileTableIterator, file_iter);
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

} // namespace sql
} // namespace oceanbase
