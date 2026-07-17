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

#ifndef _OB_RAW_EXPR_WRAP_ENUM_SET_H
#define _OB_RAW_EXPR_WRAP_ENUM_SET_H 1
#include "sql/resolver/expr/ob_raw_expr.h"
#include "common/object/ob_obj_type.h"

namespace oceanbase
{
namespace sql
{
class ObDMLStmt;
class ObSelectStmt;
class ObInsertStmt;
class ObSQLSessionInfo;

class ObRawExprWrapEnumSet: public ObRawExprVisitor
{
public:
  ObRawExprWrapEnumSet(ObRawExprFactory &expr_factory, ObSQLSessionInfo *my_session)
    : ObRawExprVisitor(),
      cur_stmt_(nullptr),
      expr_factory_(expr_factory),
      my_session_(my_session)
  {}
  int wrap_enum_set(ObDMLStmt &stmt);
  int analyze_all_expr(ObDMLStmt &stmt);
  int analyze_expr(ObRawExpr *expr);
  int visit(ObConstRawExpr &expr);
  int visit(ObExecParamRawExpr &expr);
  int visit(ObVarRawExpr &expr);
  int visit(ObOpPseudoColumnRawExpr &expr);
  int visit(ObQueryRefRawExpr &expr);
  int visit(ObColumnRefRawExpr &expr);
  int visit(ObOpRawExpr &expr);
  int visit(ObCaseOpRawExpr &expr);
  int visit(ObAggFunRawExpr &expr);
  int visit(ObSysFunRawExpr &expr);
  int visit(ObSetOpRawExpr &expr);
  int visit(ObAliasRefRawExpr &expr);
  int visit(ObWinFunRawExpr &expr);
  int visit(ObPseudoColumnRawExpr &expr);
  int visit(ObPlQueryRefRawExpr &expr);
  int visit(ObMatchFunRawExpr &expr);
  bool skip_child();
private:
  int wrap_type_to_str_if_necessary(ObRawExpr *expr,
                                    common::ObObjType calc_type,
                                    bool is_same_need,
                                    ObSysFunRawExpr *&wrapped_expr);
  int wrap_target_list(ObSelectStmt &stmt);
  int wrap_sub_select(ObInsertStmt &stmt);
  int wrap_value_vector(ObInsertStmt &stmt);
  int wrap_nullif_expr(ObSysFunRawExpr &expr);
  bool can_wrap_type_to_str(const ObRawExpr &expr) const;
  int wrap_param_expr(ObIArray<ObRawExpr*> &param_exprs, ObObjType dest_typ);
  static bool has_enumset_expr_need_wrap(const ObRawExpr &expr);
private:
  ObDMLStmt *cur_stmt_;
  ObRawExprFactory &expr_factory_;
  ObSQLSessionInfo *my_session_;
};

}  // namespace sql
}  // namespace oceanbase
#endif /* _OB_RAW_EXPR_WRAP_ENUM_SET_H */
