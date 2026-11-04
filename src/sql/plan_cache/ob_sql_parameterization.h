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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_SQL_PARAMETERIZATION_
#define OCEANBASE_SQL_PLAN_CACHE_OB_SQL_PARAMETERIZATION_

#include "lib/hash/ob_link_hashmap.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_bit_set.h"
#include "lib/container/ob_vector.h"
#include "lib/container/ob_se_array.h"
#include "lib/utility/ob_print_utils.h"
#include "common/object/ob_object.h"
#include "sql/syntax.h"
#include "sql/plan_cache/fast_parse.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/plan_cache/ob_id_manager_allocator.h"
#include "sql/plan_cache/ob_plan_cache_util.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace sql
{
typedef common::LinkHashValue<common::ObString> ParameterizationHashValue;


struct SqlInfo: public ParameterizationHashValue
{
  SqlInfo &operator=(const SqlInfo &that)
  {
    total_ = that.total_;
    not_param_index_ = that.not_param_index_;
    neg_param_index_ = that.neg_param_index_;
    param_charset_type_ = that.param_charset_type_;
    sql_traits_ = that.sql_traits_;
    last_active_time_ = that.last_active_time_;
    hit_count_ = that.hit_count_;
    trans_from_minus_index_ = that.trans_from_minus_index_;
    must_be_positive_index_ = that.must_be_positive_index_;
    ps_not_param_offsets_ = that.ps_not_param_offsets_;
    fixed_param_idx_ = that.fixed_param_idx_;
    no_check_type_offsets_ = that.no_check_type_offsets_;
    token_actions_ = that.token_actions_;
    ps_need_parameterized_ = that.ps_need_parameterized_;
    need_check_fp_ = that.need_check_fp_;
    fmt_int_or_ch_decint_idx_ = that.fmt_int_or_ch_decint_idx_;
    return *this;
  }
  void destroy() {}
  int64_t total_;
  common::ObBitSet<> not_param_index_;
  common::ObBitSet<> neg_param_index_;
  common::ObBitSet<> trans_from_minus_index_;
  common::ObBitSet<> must_be_positive_index_; // record which constants must be positive
  common::ObBitSet<> fmt_int_or_ch_decint_idx_;
  common::ObSEArray<common::ObCharsetType, 16> param_charset_type_;
  ObSqlTraits sql_traits_;

  int64_t last_active_time_;
  uint64_t hit_count_;

  common::ObSEArray<int64_t, 32> ps_not_param_offsets_; //used for ps mode not param
  common::ObSEArray<int64_t, 32> fixed_param_idx_;
  common::ObSEArray<int64_t, 32> no_check_type_offsets_;
  common::ObSEArray<peg::ParameterAction, 32> token_actions_;
  // Check whether the ps parameter can be a parameterized template.
  // this is the optimization of the following statement hitting the same plan_cache:
  // prepare stmt from 'select * from t1 where c1 = 1 and c2 = 1';
  // prepare stmt from 'select * from t1 where c1 = 1 and c2 = ?';
  // prepare stmt from 'select * from t1 where c1 = ? and c2 = ?';
  // but if the SQL statement contains "is null" or "is not null" or "no wait 1", etc.
  // this optimization is not done
  // because this will cause the parameterized SQL to report a syntax error in the parser.
  // such as prepare stmt from 'select * from t1 where c1 = ? and c2 is null';
  bool ps_need_parameterized_;
  bool need_check_fp_;

  SqlInfo();
};

class ParsedSql;

enum SQL_EXECUTION_MODE
{
  INVALID_MODE = -1,
  TEXT_MODE,
  PS_PREPARE_MODE,
  PS_EXECUTE_MODE,
  PL_PREPARE_MODE,
  PL_EXECUTE_MODE,
  MAX_EXECUTION_MODE
};

class ObSqlParameterization
{
public:
  static const int64_t SQL_PARAMETERIZATION_BUCKET_NUM = 1L << 20;
  static const int64_t NO_VALUES = -1;        // indicates no values()
  static const int64_t VALUE_LIST_LEVEL = 0;  // indicates the T_VALUE_LIST level in parse
  static const int64_t VALUE_VECTOR_LEVEL = 1;//indicates the T_VALUE_VECTOR level in parse
  static const int64_t ASSIGN_LIST_LEVEL = 0;
  static const int64_t ASSIGN_ITEM_LEVEL = 1;

  ObSqlParameterization() {}
  virtual ~ObSqlParameterization() {}
  static int fast_parser(common::ObIAllocator &allocator,
                         const FPContext &fp_ctx,
                         const common::ObString &sql,
                         ObFastParserResult &fp_result);

  static int transform_syntax_tree(common::ObIAllocator &allocator,
                                   const ObSQLSessionInfo &session,
                                   const ObIArray<ObPCParam *> *raw_params,
                                   const ParsedSql &parsed,
                                   SqlInfo &sql_info,
                                   ParamStore &params,
                                   ParameterizationResult &bindings,
                                   SelectItemParamInfoMap *select_item_param_infos,
                                   bool is_transform_outline,
                                   SQL_EXECUTION_MODE execution_mode = INVALID_MODE,
                                   bool is_from_pl = false,
                                   common::ObIArray<common::ObString> *user_variables = nullptr);
  static int raw_fast_parameterize_sql(common::ObIAllocator &allocator,
                                       const ObSQLSessionInfo &session,
                                       const common::ObString &sql,
                                       common::ObString &no_param_sql,
                                       common::ObIArray<ObPCParam *> &raw_params,
                                       ParseMode parse_mode = FP_MODE);
  static int check_and_generate_param_info(const common::ObIArray<ObPCParam *> &raw_params,
                                           const SqlInfo &not_param_info,
                                           common::ObIArray<ObPCParam *> &special_param_info);
  static int formalize_fast_parameter_sql(ObIAllocator &allocator, const ObString &src_sql,
                                          ObString &dest_sql, ObIArray<ObPCParam *> &raw_params,
                                          const FPContext &fp_ctx);
  static int formalize_sql_filter_hint(ObIAllocator &allocator,
                                       const ObString &src_sql,
                                       ObString &dest_sql,
                                       ObIArray<ObPCParam *> &raw_params);
  static int formalize_sql_text(ObIAllocator &allocator, const ObString &src_sql,
                                ObString &fmt_sql, const SqlInfo &sql_info,
                                const FPContext &fp_ctx);
  static int construct_sql(const common::ObString &no_param_sql,
                           common::ObIArray<ObPCParam *> &not_params,
                           char *buf,
                           int32_t buf_len,
                           int32_t &pos);
  static int try_format_in_expr(const common::ObString &con_sql,
                           char *buf,
                           int32_t buf_len,
                           int32_t &pos,
                           bool& can_format);
  static int parameterize_syntax_tree(common::ObIAllocator &allocator,
                                      bool is_transform_outline,
                                      ObPlanCacheCtx &pc_ctx,
                                      const ParsedSql &parsed,
                                      ParamStore &params);
  static int gen_special_param_info(SqlInfo &sql_info, ObPlanCacheCtx &pc_ctx);
  static int gen_ps_not_param_var(const ObIArray<int64_t> &offsets,
                                  ParamStore &params,
                                  ObPlanCacheCtx &pc_ctx);
  static int construct_no_check_type_params(const ObIArray<int64_t> &no_check_type_offsets,
                                            ParamStore &params);
  static SQL_EXECUTION_MODE get_sql_execution_mode(ObPlanCacheCtx &pc_ctx);
  static bool is_prepare_mode(SQL_EXECUTION_MODE mode);
  static bool is_execute_mode(SQL_EXECUTION_MODE mode);
  static bool is_text_mode(SQL_EXECUTION_MODE mode);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSqlParameterization);
};

}
}

#endif /* _OB_SQL_PARAMETERIZATION_H */
