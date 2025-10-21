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

#ifndef _OB_RAW_EXPR_DEDUCE_TYPE_H
#define _OB_RAW_EXPR_DEDUCE_TYPE_H 1
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_type_demotion.h"
#include "lib/container/ob_iarray.h"
#include "lib/udt/ob_collection_type.h"
#include "common/ob_accuracy.h"
#include "share/ob_i_sql_expression.h"
#include "ob_raw_expr_util.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObRawExprDeduceType: public ObRawExprVisitor
{
public:
  ObRawExprDeduceType(const ObSQLSessionInfo *my_session,
                      ObRawExprFactory *expr_factory,
                      bool solidify_session_vars,
                      const ObLocalSessionVar *local_vars,
                      int64_t local_vars_id,
                      ObRawExprTypeDemotion &type_demotion)
    : ObRawExprVisitor(),
      my_session_(my_session),
      alloc_(),
      expr_factory_(expr_factory),
      my_local_vars_(local_vars),
      local_vars_id_(local_vars_id),
      solidify_session_vars_(solidify_session_vars),
      type_demotion_(type_demotion)
  {}
  virtual ~ObRawExprDeduceType()
  {
    alloc_.reset();
  }
  int deduce(ObRawExpr &expr);
  /// interface of ObRawExprVisitor
  virtual int visit(ObConstRawExpr &expr);
  virtual int visit(ObVarRawExpr &expr);
  virtual int visit(ObOpPseudoColumnRawExpr &expr);
  virtual int visit(ObExecParamRawExpr &expr);
  virtual int visit(ObQueryRefRawExpr &expr);
  virtual int visit(ObColumnRefRawExpr &expr);
  virtual int visit(ObOpRawExpr &expr);
  virtual int visit(ObCaseOpRawExpr &expr);
  virtual int visit(ObAggFunRawExpr &expr);
  virtual int visit(ObSysFunRawExpr &expr);
  virtual int visit(ObSetOpRawExpr &expr);
  virtual int visit(ObAliasRefRawExpr &expr);
  virtual int visit(ObWinFunRawExpr &expr);
  virtual int visit(ObPseudoColumnRawExpr &expr);
  virtual int visit(ObUDFRawExpr &expr);
  virtual int visit(ObPlQueryRefRawExpr &expr);
  virtual int visit(ObMatchFunRawExpr &expr);

  int add_implicit_cast(ObOpRawExpr &parent,
                        const ObIExprResTypes &input_types,
                        const ObCastMode &cast_mode);
  int add_implicit_cast(ObCaseOpRawExpr &parent,
                        const ObIExprResTypes &input_types,
                        const ObCastMode &cast_mode);
  int add_implicit_cast(ObAggFunRawExpr &parent,
                        const ObExprResType &res_type,
                        const ObCastMode &cast_mode);

  int check_type_for_case_expr(ObCaseOpRawExpr &case_expr, common::ObIAllocator &alloc);
  static bool skip_cast_expr(const ObRawExpr &parent, const int64_t child_idx);

private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprDeduceType);
  // function members
  int push_back_types(const ObRawExpr *param_expr, ObIExprResTypes &types);
  int calc_result_type(ObNonTerminalRawExpr &expr, ObIExprResTypes &types,
                       common::ObCastMode &cast_mode, int32_t row_dimension);
  int calc_result_type_with_const_arg(
    ObNonTerminalRawExpr &expr,
    ObIExprResTypes &types,
    common::ObExprTypeCtx &type_ctx,
    ObExprOperator *op,
    ObExprResType &result_type,
    int32_t row_dimension);
  int check_expr_param(ObOpRawExpr &expr);
  int check_row_param(ObOpRawExpr &expr);
  int check_param_expr_op_row(ObRawExpr *param_expr, int64_t column_count);
  int visit_left_param(ObRawExpr &expr);
  // Observe the number of parameters of the right operator, since we need to know the number of parameters of the left operator, so pass in the root operator
  int visit_right_param(ObOpRawExpr &expr);
  int64_t get_expr_output_column(const ObRawExpr &expr);
  int get_row_expr_param_type(const ObRawExpr &expr, ObIExprResTypes &types);
  int deduce_type_visit_for_special_func(int64_t param_index, const ObRawExpr &expr, ObIExprResTypes &types);
  // init udf expr
  int init_normal_udf_expr(ObNonTerminalRawExpr &expr, ObExprOperator *op);
  // get agg udf result type
  int set_agg_udf_result_type(ObAggFunRawExpr &expr);

  int set_agg_group_concat_result_type(ObAggFunRawExpr &expr, ObExprResType &result_type);
  int set_json_agg_result_type(ObAggFunRawExpr &expr, ObExprResType& result_type, bool &need_add_cast);
  int set_asmvt_result_type(ObAggFunRawExpr &expr, ObExprResType& result_type);
  int set_rb_result_type(ObAggFunRawExpr &expr, ObExprResType& result_type);
  int set_rb_calc_result_type(ObAggFunRawExpr &expr, ObExprResType& result_type);
  int set_rb_cardinality_result_type(ObAggFunRawExpr &expr, ObExprResType& result_type);
  int set_agg_json_array_result_type(ObAggFunRawExpr &expr, ObExprResType &result_type);

  int set_agg_min_max_result_type(ObAggFunRawExpr &expr, ObExprResType &result_type,
                                  bool &need_add_cast);
  int set_agg_regr_result_type(ObAggFunRawExpr &expr, ObExprResType &result_type);  
  int set_array_agg_result_type(ObAggFunRawExpr &expr, ObExprResType& result_type);

  // helper functions for add_implicit_cast
  int add_implicit_cast_for_op_row(ObRawExpr *&child_ptr,
                                   const common::ObIArray<ObExprResType> &input_types,
                                   const ObCastMode &cast_mode);
  // try add cast expr on subquery stmt's output && update column types.
  int add_implicit_cast_for_subquery(ObQueryRefRawExpr &expr,
                                     const common::ObIArray<ObExprResType> &input_types,
                                     const ObCastMode &cast_mode);
  // try add cast expr above %child_idx child,
  // %input_type.get_calc_meta() is the destination type!
  template<typename RawExprType>
  int try_add_cast_expr(RawExprType &parent, int64_t child_idx,
                        const ObExprResType &input_type, const ObCastMode &cast_mode);

  // try add cast expr above %expr , set %new_expr to &expr if no cast added.
  // %input_type.get_calc_meta() is the destination type!
  int try_add_cast_expr_above_for_deduce_type(ObRawExpr &expr, ObRawExpr *&new_expr,
                                              const ObExprResType &input_type,
                                              const ObCastMode &cm);
  int check_group_aggr_param(ObAggFunRawExpr &expr);
  int check_group_rank_aggr_param(ObAggFunRawExpr &expr);
  int check_median_percentile_param(ObAggFunRawExpr &expr);
  int add_median_percentile_implicit_cast(ObAggFunRawExpr &expr,
                                          const ObCastMode& cast_mode,
                                          const bool keep_type);
  int add_group_aggr_implicit_cast(ObAggFunRawExpr &expr, const ObCastMode& cast_mode);
  int adjust_cast_as_signed_unsigned(ObSysFunRawExpr &expr);

  bool ignore_scale_adjust_for_decimal_int(const ObItemType expr_type);
  int try_replace_casts_with_questionmarks_ora(ObRawExpr *row_expr);

  int try_replace_cast_with_questionmark_ora(ObRawExpr &parent, ObRawExpr *cast_expr, int param_idx);
  int set_extra_calc_type_info(ObRawExpr &expr, const ObExprResType &res_type);
  OB_INLINE bool is_lob_param_conversion_exempt(const ObItemType expr_type) {
    return T_FUN_COLUMN_CONV == expr_type
           || T_OP_OUTPUT_PACK == expr_type
           || T_FUN_SYS_REMOVE_CONST == expr_type;
  };
private:
  const sql::ObSQLSessionInfo *my_session_;
  common::ObArenaAllocator alloc_;
  ObRawExprFactory *expr_factory_;
  //deduce with current session vars if solidify_session_vars_ is true,
  //otherwise deduce with my_local_vars_ if my_local_vars_ is not null
  const ObLocalSessionVar *my_local_vars_;
  int64_t local_vars_id_;
  bool solidify_session_vars_;
  ObRawExprTypeDemotion &type_demotion_;
  // data members
};

template<typename RawExprType>
int ObRawExprDeduceType::try_add_cast_expr(RawExprType &parent,
                                           int64_t child_idx,
                                           const ObExprResType &input_type,
                                           const ObCastMode &cast_mode)
{
  int ret = OB_SUCCESS;
  ObRawExpr *child_ptr = NULL;
  if (OB_ISNULL(my_session_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "my_session_ is NULL", K(ret));
  } else if (OB_UNLIKELY(parent.get_param_count() <= child_idx)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "child_idx is invalid", K(ret), K(parent.get_param_count()), K(child_idx));
  } else if (OB_ISNULL(child_ptr = parent.get_param_expr(child_idx))) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "child_ptr raw expr is NULL", K(ret));
  } else {
    ObRawExpr *new_expr = NULL;
  // This function was originally defined in CPP, [because of the effect of UNITY merging compilation units, it compiled successfully, but the implementation of template code needs to be defined in the header file], therefore, closing UNITY led to observer failing to compile
  // To solve the compilation problem after closing UNITY, move it to the header file
  // But this function uses OZ, CK macros, these two macros internal log print used LOG_WARN, require must define USING_LOG_PREFIX
  // Since this is a header file, this will lead to very tricky problems:
  // 1. If USING_LOG_PREFIX is not defined before this header file, it must be redefined (but defining the macro in the header file will cause pollution)
  // 2. If USING_LOG_PREFIX is newly defined in this file, it needs to be cleaned up to prevent pollution from spreading to other .h and cpp files
  // Therefore here we check if USING_LOG_PREFIX is already defined, if it is defined then we abandon redefining it (this means logs are not always printed with the "SQL_RESV" identifier), and also define a special identifier
  // If a special identifier is found, execute macro cleanup actions during preprocessing
  // The entire logic is quite tricky, is to minimize code logic changes, code owner needs to refactor this logic later
#ifndef USING_LOG_PREFIX
#define MARK_MACRO_DEFINED_BY_OB_RAW_EXPR_DEDUCE_TYPE_H
#define USING_LOG_PREFIX SQL_RESV
#endif
    OZ(try_add_cast_expr_above_for_deduce_type(*child_ptr, new_expr, input_type,
                                               cast_mode));
    CK(NULL != new_expr);
    if (OB_SUCC(ret) && child_ptr != new_expr) { // cast expr added
      ObObjTypeClass ori_tc = ob_obj_type_class(child_ptr->get_data_type());
      ObObjTypeClass expect_tc = ob_obj_type_class(input_type.get_calc_type());
      if (T_FUN_UDF == parent.get_expr_type()
          && ObNumberTC == ori_tc
          && ObLobTC == expect_tc) {
        // oracle mode can not cast number to text, but mysql mode can
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        SQL_RESV_LOG(WARN, "cast to lob type not allowed", K(ret));
      }

      // for consistent with mysql, if const cast as json, should regard as scalar, don't need parse
      if (ObStringTC == ori_tc && ObJsonTC == expect_tc && IS_JSON_COMPATIBLE_OP(parent.get_expr_type())) {
        uint64_t extra = new_expr->get_cast_mode();
        new_expr->set_cast_mode(CM_SET_SQL_AS_JSON_SCALAR(extra));
      }
      OZ(parent.replace_param_expr(child_idx, new_expr));
#ifdef MARK_MACRO_DEFINED_BY_OB_RAW_EXPR_DEDUCE_TYPE_H
#undef USING_LOG_PREFIX
#endif
      if (OB_FAIL(ret) && my_session_->is_varparams_sql_prepare()) {
        ret = OB_SUCCESS;
        SQL_RESV_LOG(DEBUG, "ps prepare phase ignores type deduce error");
      }
      //add local vars to cast expr
      if (OB_SUCC(ret)) {
        if (solidify_session_vars_) {
          // do nothing
        } else if (OB_INVALID_INDEX_INT64 != local_vars_id_){
          new_expr->set_local_session_var_id(local_vars_id_);
        }
      }
    }
  }
  return ret;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_RAW_EXPR_DEDUCE_TYPE_H */
