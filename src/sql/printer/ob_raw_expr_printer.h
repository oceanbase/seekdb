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

#ifndef OCEANBASE_SQL_RESOLVER_EXPR_OB_RAW_EXPR_PRINTER_H_
#define OCEANBASE_SQL_RESOLVER_EXPR_OB_RAW_EXPR_PRINTER_H_

#include "sql/resolver/expr/ob_raw_expr.h"
#include "lib/string/ob_sql_string.h"
#include "common/ob_smart_call.h"
#include "sql/ob_sql_utils.h"
namespace oceanbase
{
namespace common
{
class ObTimeZoneInfo;
}
namespace sql
{
class ObRawExprPrinter
{
#define LEN_AND_PTR(str) (str.length()), (str.ptr())
#define _DATA_PRINTF(...) databuff_printf(buf_, buf_len_, *pos_, __VA_ARGS__)
#define DATA_PRINTF(...)                                           \
  do {                                                              \
    if (OB_SUCC(ret)) {                                       \
      if (OB_ISNULL(buf_) || OB_ISNULL(pos_)) {                     \
        ret = OB_ERR_UNEXPECTED;                                     \
        LOG_WARN("buf_ or pos_ is null", K(ret));                   \
      } else if (OB_FAIL(_DATA_PRINTF(__VA_ARGS__))) {               \
        LOG_WARN("fail to print", K(ret));                            \
      }                                                                \
    }                                                                   \
  } while(0)                                                             \

#define PRINT_IDENT(ident_str)                                                        \
  do {                                                                                \
    if (OB_SUCC(ret) && OB_FAIL(ObSQLUtils::print_identifier(buf_, buf_len_, (*pos_), \
                                                             print_params_.cs_type_,  \
                                                             ident_str,               \
                                                             false))) {               \
      LOG_WARN("fail to print ident str", K(ret), K(ident_str));                      \
    }                                                                                 \
  } while(0)

#define CONVERT_CHARSET_FOR_RPINT(alloc, input_str)                                   \
  do {                                                                                \
    if (OB_SUCC(ret) && OB_FAIL(ObCharset::charset_convert(allocator,                 \
                                                           input_str,                 \
                                                           CS_TYPE_UTF8MB4_BIN,       \
                                                           print_params_.cs_type_,    \
                                                           input_str))) {             \
      LOG_WARN("fail to gen ident str", K(ret), K(input_str));                        \
    }                                                                                 \
  } while(0)

#define PRINT_EXPR(expr)                              \
  do {                                                 \
    if (OB_SUCCESS == ret && OB_FAIL(SMART_CALL(print(expr)))) {    \
      LOG_WARN("fail to print expr", K(ret));            \
    }                                                     \
  } while(0)                                               \

#define PRINT_BOOL_EXPR(expr)                              \
  do {                                                 \
    if (OB_SUCCESS == ret && OB_FAIL(SMART_CALL(print_bool_expr(expr)))) {    \
      LOG_WARN("fail to print expr", K(ret));            \
    }                                                     \
  } while(0)                                               \

#define SET_SYMBOL_IF_EMPTY(str)  \
  do {                             \
    if (0 == symbol.length()) {     \
      symbol = str;                  \
    }                                 \
  } while (0)                          \

#define PRINT_QUOT                    \
  do {                                \
    DATA_PRINTF("`");               \
  } while (0);

#define PRINT_QUOT_WITH_SPACE \
  DATA_PRINTF(" ");           \
  PRINT_QUOT;

#define PRINT_IDENT_WITH_QUOT(ident_str)  \
  do {                                    \
    PRINT_QUOT;                           \
    PRINT_IDENT(ident_str);               \
    PRINT_QUOT;                           \
  } while (0)
// cast function uses these two macros in the parse phase, but they are defined in sql_parse_tab.c
// cast function functionality is incomplete, will not be modified before beta, define here first
// TODO@nijia.nj
#define BINARY_COLLATION 63
#define INVALID_COLLATION 0

public:
  ObRawExprPrinter();
  ObRawExprPrinter(char *buf, int64_t buf_len, int64_t *pos, ObSchemaGetterGuard *schema_guard,
                   common::ObObjPrintParams print_params = common::ObObjPrintParams(),
                   const ParamStore *param_store = NULL);
  virtual ~ObRawExprPrinter();

  void init(char *buf, int64_t buf_len, int64_t *pos, ObSchemaGetterGuard *schema_guard,
            ObObjPrintParams print_params, const ParamStore *param_store = NULL);
  // stmt will contain several exprs, to avoid repeated instantiation, here expr is passed as a parameter to do_print
  int do_print(ObRawExpr *expr, ObStmtScope scope, bool only_column_namespace = false, bool print_cte = false);
private:
  int print_bool_expr(ObRawExpr *expr);
  int print_select_expr(ObRawExpr *expr);
  int print(ObRawExpr *expr);

  int print(ObConstRawExpr *expr);
  int print(ObQueryRefRawExpr *expr);
  int print(ObColumnRefRawExpr *expr);
  int print(ObOpRawExpr *expr);
  int print(ObCaseOpRawExpr *expr);
  int print(ObSetOpRawExpr *expr);
  int print(ObAggFunRawExpr *expr);
  int print(ObSysFunRawExpr *expr);
  int print_translate(ObSysFunRawExpr *expr);
  int print(ObUDFRawExpr *expr);
  int print(ObWinFunRawExpr *expr);
  int print(ObPseudoColumnRawExpr *expr);
  int print(ObMatchFunRawExpr *expr);

  int print_date_unit(ObRawExpr *expr);
  int print_get_format_unit(ObRawExpr *expr);
  int print_cast_type(ObRawExpr *expr);
  int print_json_expr(ObSysFunRawExpr *expr);
  int print_json_value(ObSysFunRawExpr *expr);
  int print_json_query(ObSysFunRawExpr *expr);
  int print_json_object_star(ObSysFunRawExpr *expr);
  int print_json_exists(ObSysFunRawExpr *expr);
  int print_json_equal(ObSysFunRawExpr *expr);
  int print_json_array(ObSysFunRawExpr *expr);
  int print_json_mergepatch(ObSysFunRawExpr *expr);
  int print_json_return_type(ObRawExpr *expr);
  int print_is_json(ObSysFunRawExpr *expr);
  int print_json_object(ObSysFunRawExpr *expr);
  int print_ora_json_arrayagg(ObAggFunRawExpr *expr);
  int print_ora_json_objectagg(ObAggFunRawExpr *expr);
  int print_partition_exprs(ObWinFunRawExpr *expr);
  int print_order_items(ObWinFunRawExpr *expr);
  int print_window_clause(ObWinFunRawExpr *expr);
  int print_st_asmvt(ObAggFunRawExpr *expr);
  int print_array_agg_expr(ObAggFunRawExpr *expr);
  int print_array_map(ObSysFunRawExpr *expr, const char *func_name);

  int inner_print_fun_params(ObSysFunRawExpr &expr);

  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprPrinter);
private:
  // data members
  char *buf_;
  int64_t buf_len_;
  // avoid to update pos_ between different printers(mainly ObRawExprPrinter
  // and ObSelectStmtPrinter), we definite pointer of pos_ rather than object
  int64_t *pos_;
  ObStmtScope scope_;
  bool only_column_namespace_;
  const common::ObTimeZoneInfo *tz_info_;
  ObObjPrintParams print_params_;
  const ParamStore *param_store_;
  ObSchemaGetterGuard *schema_guard_;
  bool print_cte_;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_SQL_RESOLVER_EXPR_OB_RAW_EXPR_PRINTER_H_
