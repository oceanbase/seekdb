/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef SEEKDB_SQL_ENGINE_EXPR_PLUGIN_FUNCTION_EXPR_H_
#define SEEKDB_SQL_ENGINE_EXPR_PLUGIN_FUNCTION_EXPR_H_

#include <memory>

#include "seekdb/plugin/sql_catalog.h"
#include "share/rc/ob_module_provider.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class PluginFunctionExpr final : public ObFuncExprOperator
{
public:
  static constexpr const char *SQL_DISPATCH_NAME = "__seekdb_plugin_function";

  explicit PluginFunctionExpr(common::ObIAllocator &allocator);

  int calc_result_typeN(ObExprResType &type,
                        ObExprResType *arguments,
                        int64_t argument_count,
                        common::ObExprTypeCtx &type_context) const override;
  bool need_rt_ctx() const override { return true; }
  int cg_expr(ObExprCGCtx &cg_context,
              const ObRawExpr &raw_expression,
              ObExpr &runtime_expression) const override;

  static int evaluate(const ObExpr &expression,
                      ObEvalCtx &context,
                      common::ObDatum &result);

private:
  class RuntimeContext final : public ObExprOperatorCtx
  {
  public:
    RuntimeContext() : initialized_(false), binding_() {}

    bool initialized_;
    seekdb_plugin_sql_binding_v1_t binding_;
  };
};

class PluginTableFunctionExpr final : public ObFuncExprOperator
{
public:
  static constexpr const char *SQL_DISPATCH_NAME =
      "__seekdb_plugin_table_function";

  explicit PluginTableFunctionExpr(common::ObIAllocator &allocator);

  int calc_result_typeN(ObExprResType &type,
                        ObExprResType *arguments,
                        int64_t argument_count,
                        common::ObExprTypeCtx &type_context) const override;
  bool need_rt_ctx() const override { return true; }
  int cg_expr(ObExprCGCtx &cg_context,
              const ObRawExpr &raw_expression,
              ObExpr &runtime_expression) const override;

  static int resolve_binding(const ObRawExpr &expression,
                             seekdb_plugin_sql_binding_v1_t &binding);
  static int fetch_row(const ObExpr &expression,
                       ObEvalCtx &context,
                       const common::ObIArray<ObExpr *> &columns);
  static int rescan(const ObExpr &expression, ObEvalCtx &context);
  static int close(const ObExpr &expression, ObEvalCtx &context);

private:
  static int evaluate(const ObExpr &expression,
                      ObEvalCtx &context,
                      common::ObDatum &result);

  class RuntimeContext final : public ObExprOperatorCtx
  {
  public:
    RuntimeContext() : initialized_(false), binding_(), cursor_() {}

    bool initialized_;
    seekdb_plugin_sql_binding_v1_t binding_;
    std::unique_ptr<share::IPluginTableCursor> cursor_;
  };
};

} // namespace sql
} // namespace oceanbase

#endif // SEEKDB_SQL_ENGINE_EXPR_PLUGIN_FUNCTION_EXPR_H_
