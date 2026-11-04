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

#ifndef OCEANBASE_SQL_OB_RAW_EXPR_RESOLVER_IMPL_H
#define OCEANBASE_SQL_OB_RAW_EXPR_RESOLVER_IMPL_H
#include "sql/resolver/ob_column_ref.h"
#include "ob_raw_expr_resolver.h"

namespace oceanbase
{
namespace sql
{
struct LiteralToken;
struct InListRows;

class ObRawExprResolverImpl: public ObRawExprResolver
{
public:
  explicit ObRawExprResolverImpl(ObExprResolveContext &ctx);
  virtual ~ObRawExprResolverImpl() {}
  bool is_contains_assignment() { return is_contains_assignment_; }
  void set_contains_assignment(bool value) { is_contains_assignment_ = value; }
  int resolve(const peg::Node &node,
      ObRawExpr *&expr,
      ObIArray<ObQualifiedName> &columns,
      ObIArray<ObVarInfo> &sys_vars,
      ObIArray<ObSubQueryInfo> &sub_query_info,
      ObIArray<ObAggFunRawExpr*> &aggr_exprs,
      ObIArray<ObWinFunRawExpr*> &win_exprs,
      ObIArray<ObUDFInfo> &udf_info,
      ObIArray<ObOpRawExpr*> &op_exprs,
      ObIArray<ObUserVarIdentRawExpr*> &user_var_exprs,
      ObIArray<ObInListInfo> &inlist_infos,
      ObIArray<ObMatchFunRawExpr*> &match_exprs);
  int recursive_resolve(const peg::Node &node, ObRawExpr *&expr, bool is_root_expr = false);
  static int check_sys_func(ObQualifiedName &q_name, bool &is_sys_func);
  static int check_pl_udf(ObQualifiedName &q_name,
      const ObSQLSessionInfo *session_info,
      ObSchemaChecker *schema_checker,
      pl::ObPLBlockNS *secondary_namespace,
      pl::ObProcType &proc_type);
  int check_name_type(
      ObQualifiedName &q_name, ObStmtScope scope, AccessNameType &type);
  int resolve_func_node_of_obj_access_idents(const peg::Node &node, ObQualifiedName &name);
  int check_first_node(const peg::Node &node);
  int resolve_window_spec(const peg::Node &specification, ObWinFunRawExpr &window);
  static bool should_not_contain_window_clause(const ObItemType func_type);
private:
  DISALLOW_COPY_AND_ASSIGN(ObRawExprResolverImpl);
  int get_opposite_string(const common::ObString &orig_string, common::ObString &new_string, common::ObIAllocator &allocator);
  int try_negate_const(ObRawExpr *&expr,
      const int64_t neg_cnt,
      int64_t &remain_neg_cnt);
  int do_recursive_resolve(const peg::Node &node,
      ObRawExpr *&expr, bool is_root_expr = false);
  ObString source_text(const peg::Node &node) const;
  int process_row(const peg::Node &node, ObRawExpr *&expr);
  int publish_qualified_name(ObQualifiedName &name,
      int64_t child_start, ObRawExpr *&expr);
  int process_unresolved_function(const peg::Node &node, ObRawExpr *&expr);
  int check_pl_variable(ObQualifiedName &q_name, bool &is_pl_var);
  int mock_enum_type_info(common::ObIAllocator &allocator, ObString &string, uint64_t idx, ObIArray<common::ObString> &type_info);
  int mock_set_type_info(common::ObIAllocator &allocator, ObString &string, uint64_t element_val, ObIArray<common::ObString> &type_info);
  int resolve_cast_collation(const ObString &charset_name, ObCollationType &collation);
  int build_cast_argument(const ObRawExprResType &target,
      const ObString &charset_name, ObRawExpr *&expr);
  int build_implicit_literal(const LiteralToken &literal, ObRawExpr *&expr);
  int process_odbc_escape(const peg::Node &node, ObRawExpr *&expr);
  int process_datatype_or_questionmark(const peg::Node &node, ObRawExpr *&expr);
  int process_system_variable_node(const peg::Node &node, ObRawExpr *&expr);
  int process_char_charset_node(const peg::Node &node, ObRawExpr *&expr);
  int process_vector_func_node(const peg::Node &node, ObRawExpr *&expr);
  int set_geo_func_name(ObSysFunRawExpr *func_expr,
      const ObItemType func_type);
  int process_geo_func_node(const peg::Node &node, ObRawExpr *&expr);
  int build_lambda_parameter(int64_t index, ObRawExpr *&expr);
  int extract_var_exprs(ObRawExpr *expr, ObIArray<ObVarRawExpr *> &var_exprs);
  int check_lambda_params_duplicated(const peg::Node &lambda);
  int process_lambda_func_node(const peg::Node &node, ObRawExpr *&expr);
  int process_array_map_func_node(const peg::Node &node, ObRawExpr *&expr);
  int process_left_value_node(const peg::Node &node, ObRawExpr *&expr);
  int process_column_ref_node(const peg::Node &node, ObRawExpr *&expr, bool allow_star = false);
  int process_user_assignment(const peg::Node &node, ObRawExpr *&expr);
  int process_any_or_all_node(const peg::Node &node, ObRawExpr *&expr);
  int process_user_var_node(const peg::Node &node, ObRawExpr *&expr);
  int process_not_node(const peg::Node &node, ObRawExpr *&expr);
  int process_pos_or_neg_node(const peg::Node &node, ObRawExpr *&expr);
  int process_logical_node(const peg::Node &node, bool is_root_expr, ObRawExpr *&expr);
  int process_operator_node(const peg::Node &node, ObRawExpr *&expr);
  int convert_any_or_all_expr(ObRawExpr *&expr,
      bool &happened);
  int process_is_or_is_not_node(const peg::Node &node, ObRawExpr *&expr);
  int process_regexp_or_not_regexp_node(const peg::Node &node, ObRawExpr *&expr);
  int process_between_node(const peg::Node &node, ObRawExpr *&expr);
  int transform_between_expr(
      ObRawExpr **btw_params, ObRawExpr *&expr, const bool is_not_btw);
  int add_params_to_op_expr(
      ObRawExpr *op_param_1, ObRawExpr *op_param_2, ObOpRawExpr *op_expr);
  int process_like_node(const peg::Node &node, ObRawExpr *&expr);
  int process_in_or_not_in_node(const peg::Node &node,
      const bool is_root_expr,
      ObRawExpr *&expr,
      const InListRows *merged_rows = nullptr);
  int resolve_right_branch_of_in_op(const peg::Node &node,
      const ObItemType op_type,
      const ObRawExpr *left_expr,
      const bool is_root_condition,
      ObRawExpr *&right_expr,
      const InListRows *merged_rows);
  int process_case_node(const peg::Node &node, ObRawExpr *&expr);
  int process_sub_query_node(const peg::Node &node, ObRawExpr *&expr);
  int process_agg_node(const peg::Node &node, ObRawExpr *&expr);
  int process_timestamp_node(const peg::Node &node, ObRawExpr *&expr);
  int process_collation_node(const peg::Node &node, ObRawExpr *&expr);
  int process_if_node(const peg::Node &node, ObRawExpr *&expr);
  int process_isnull_node(const peg::Node &node, ObRawExpr *&expr);
  int process_call_param_node(const peg::Node &node, ObRawExpr *&expr);
  int process_sqlerrm_node(const peg::Node &node, ObRawExpr *&expr);
  int process_sqlcode_node(const peg::Node &node, ObRawExpr *&expr);
  int process_json_query_node(const peg::Node &node, ObRawExpr *&expr);
  int process_json_value_node(const peg::Node &node, ObRawExpr *&expr);
  int cast_accuracy_check(const ObRawExprResType &target, const ObString &input);
  int process_cast(const peg::Node &node, ObRawExpr *&expr);
  int build_cast_type(const peg::Node &node,
      const ObRawExprResType &target, ObRawExpr *&expr);
  int process_fun_sys_node(const peg::Node &node,
      ObRawExpr *&expr,
      const bool is_root_expr);
  int process_sys_func_params(ObSysFunRawExpr &func_expr, int current_columns_count);
  int resolve_weight_string_options(const peg::Node &node, ObSysFunRawExpr &function);
  int resolve_stored_function(const peg::Node &call, ObUDFInfo &udf_info);
  int process_match_against(const peg::Node &node, ObRawExpr *&expr);
  int process_match_score(ObRawExpr *&expr);
  static int not_int_check(const ObRawExpr *expr);
  static int not_row_check(const ObRawExpr *expr);
  static int param_not_row_check(const ObRawExpr *expr);
  int process_window_function_node(const peg::Node &node, ObRawExpr *&expr);
  int process_window_agg_node(const ObItemType func_type,
      ObWinFunRawExpr *win_func,
      ObRawExpr *agg_node_expr,
      ObRawExpr *&expr);
  int process_frame_node(const peg::Node &node, ObFrame &frame);
  int process_bound_node(const peg::Node &node, const bool is_upper, Bound &bound);
  int check_and_canonicalize_window_expr(ObRawExpr *expr);
  int process_internal_sys_function_node(const peg::Node &node,
      ObRawExpr *&expr, ObItemType type);
  int check_internal_function(const ObString &name);
  bool is_win_expr_valid_scope(ObStmtScope scope) const
  {
    return scope == T_FIELD_LIST_SCOPE || scope == T_NAMED_WINDOWS_SCOPE || scope == T_ORDER_SCOPE;
  }
  ObExprResolveContext &ctx_;
  bool is_contains_assignment_;
  const common::ObIArray<common::ObString> *lambda_parameters_;
};
} // namespace sql
} // namespace oceanbase
#endif
