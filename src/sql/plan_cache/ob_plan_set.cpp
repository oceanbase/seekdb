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

#define USING_LOG_PREFIX SQL_PC

#include "ob_plan_set.h"
#include "sql/plan_cache/ob_pcv_set.h"

using namespace oceanbase;
using namespace common;
using namespace oceanbase::sql;
using namespace oceanbase::transaction;
using namespace share;
using namespace share::schema;
using namespace pl;
namespace oceanbase
{
namespace sql
{
  const ObPCUserVarMeta ObPlanSet::UNKNOWN_VAR_DEFAULT_META = ObPCUserVarMeta(PRECISION_UNKNOWN_YET,
                                                                              ObVarcharType,
                                                                              CS_LEVEL_IMPLICIT,
                                                                              CS_TYPE_BINARY);
  ObPlanSet::~ObPlanSet()
  {
    // Make sure destory planset before destory pre calculable expression.
    if (OB_ISNULL(pre_cal_expr_handler_))
    {
      // have no pre calculable expression, do nothing
    } else {
    int64_t ref_cnt = pre_cal_expr_handler_->dec_ref_cnt();
    if (ref_cnt == 0) {
      common::ObIAllocator* alloc = pre_cal_expr_handler_->pc_alloc_;
      pre_cal_expr_handler_->~PreCalcExprHandler();
      alloc->free(pre_cal_expr_handler_);
      pre_cal_expr_handler_ = NULL;
    }
  }
}

int ObPlanSet::get_variable_meta(const ObSQLSessionInfo *session_info, const ObString &var_name,
                                 ObPCUserVarMeta &meta)
{
  int ret = OB_SUCCESS;
  ObSessionVariable sess_var;
  if (OB_FAIL(session_info->get_user_variable(var_name, sess_var))) {
    if (ret == OB_ERR_USER_VARIABLE_UNKNOWN) {
      meta = UNKNOWN_VAR_DEFAULT_META;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get user variable", K(ret), K(var_name));
    }
  } else {
    meta.parse_from_variable(sess_var);
  }
  return ret;
}

//used for get plan
int ObPlanSet::match_params_info(const ParamStore *params,
                                 ObPlanCacheCtx &pc_ctx,
                                 bool &is_same)
{
  int ret = OB_SUCCESS;
  is_same = true;
  ObExecContext &exec_ctx = pc_ctx.exec_ctx_;
  ObSessionVariable sess_var;
  bool is_sql = is_sql_planset();
  if (OB_ISNULL(params)) {
    is_same = true;
  } else if (params->count() > params_info_.count()) {
    is_same = false;
  } else {
    // Match the original parameters
    int64_t N = params->count();
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < N; ++i) {
      if (OB_FAIL(match_param_info(params_info_.at(i),
                                   params->at(i),
                                   is_same,
                                   is_sql))) {
      }
    }
    // Match related user session variables
    // Here should first compare related_user_var_names_ with variables inside session_info, then perform pre-calculation
    // Otherwise it will lead to session var type change, unable to match the plan. Example as follows:
    // eg:   SQL                   ParamStore
    //     1. set @a := 1;
    //     2. select @a;            int obj
    //     3. set @a := '1';
    //     4. select @a;            varchar obj
    //     5. select @a;            int obj(because it fills the pre-calculated result when matching sql2 plan)
    //     Result: sql5 cannot match sql4's plan
    //     Reason: sql5 matches sql2's plan by pre-calculating first, obtaining int obj, then comparing related_user_var_names_
    //           Found sess_var in session_info_ as varchar, matching failed.
    //           sql5 rematch sql4's plan, since it has already been pre-calculated, no further calculation is performed, so ParamStore
    //           inside is still int obj, while Obj in params_info_ is varchar, matching failed.
    //
    //     So it should be changed to compare related_user_var_names with sess_var first, then perform pre-calculation
    //     would not result in matching with precomputed results of the previous plan in ParamStore
    if (OB_SUCC(ret) && is_same && related_user_var_names_.count() > 0) {
      if (related_user_var_names_.count() != related_user_sess_var_metas_.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("related_user_var_names and related_user_sess_vars should have the same size",
                 K(ret), K(related_user_var_names_.count()), K(related_user_sess_var_metas_.count()));
      } else if (OB_ISNULL(pc_ctx.sql_ctx_.session_info_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null",
                 K(ret), K(pc_ctx.sql_ctx_.session_info_));
      } else {
        ObSQLSessionInfo *session_info = pc_ctx.sql_ctx_.session_info_;
        ObPCUserVarMeta tmp_meta;
        for (int64_t i = 0 ; OB_SUCC(ret) && is_same && i < related_user_var_names_.count(); i++) {
          if (OB_FAIL(get_variable_meta(pc_ctx.sql_ctx_.session_info_,
                related_user_var_names_.at(i), tmp_meta))) {
          } else {
            is_same = (related_user_sess_var_metas_.at(i) == tmp_meta);
          }
        }
      }
    }

    //pre calculate
    if (OB_SUCC(ret) && is_same) {
      ObPhysicalPlanCtx *plan_ctx = exec_ctx.get_physical_plan_ctx();
      ObSQLSessionInfo *session = exec_ctx.get_my_session();

      if (OB_ISNULL(session)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null session", K(ret));
      } else if (OB_ISNULL(plan_ctx)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null plan context", K(ret));
      } else if (fetch_cur_time_ && FALSE_IT(plan_ctx->set_cur_time(
                                ObClockGenerator::getClock(), *session))) {
        // never reach
      } else if (FALSE_IT(plan_ctx->set_last_trace_id(session->get_last_trace_id()))) {
      } else if (params->count() != params_info_.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param info count is different", K(params_info_), K(*params), K(ret));
      } else {
        /* check calculable expr constraints*/
        DLIST_FOREACH(pre_calc_con, all_pre_calc_constraints_) {
          if (OB_FAIL(ObPlanCacheObject::check_pre_calc_cons(is_ignore_stmt_,
                                                             is_same,
                                                             *pre_calc_con,
                                                             exec_ctx))) {
          } else if (!is_same) {
            break;
          }
        }
      }
    }
    // Match true/false, at this time the flag_ in params is the initial value, and cannot be used directly.
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < params->count(); ++i) {
      if (OB_FAIL(match_param_bool_value(params_info_.at(i),
                                         params->at(i),
                                         is_same))) {
      }
    } //for end

    // check const constraint
    if (OB_SUCC(ret) && is_same) {
      OC( (match_constraint)(*params, is_same) );
    }

    if (OB_SUCC(ret) && is_same) {
      if (OB_FAIL(match_multi_stmt_info(*params, multi_stmt_rowkey_pos_, is_same))) {
      } else if (!is_same) {
        ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      }
    }
    if (OB_FAIL(ret)) {
      is_same = false;
    }
  }
  return ret;
}

int ObPlanSet::copy_param_flag_from_param_info(ParamStore *params)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("params is null", K(ret));
  } else if (params->count() != params_info_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("params is null", K(ret), KPC(params), K(params_info_));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < params->count(); ++i) {
    params->at(i).set_param_flag(params_info_.at(i).flag_);
  }
  return ret;
}
// Match parameter type information
int ObPlanSet::match_param_info(const ObParamInfo &param_info,
                                const ObObjParam &param,
                                bool &is_same,
                                bool is_sql_planset) const
{
  int ret = OB_SUCCESS;
  is_same = true;
  // extend type must be checked
  // insert into t values (1)
  // insert into t values (:0)
  // two sql have the same key `insert into t values (?)`
  // but they have complete different plans
  if (param_info.flag_.need_to_check_type_ || need_match_all_params_) {
    if (param.get_param_meta().get_type() != param.get_type()) {
      LOG_TRACE("differ in match param info",
                K(param.get_param_meta().get_type()),
                K(param.get_type()));
    }

    if (param.get_collation_type() != param_info.col_type_
        && !(param.get_param_meta().is_ext() || param.is_collection_sql_type())) {
      is_same = false;
    } else if (param.get_param_meta().get_type() != param_info.type_) {
      is_same = false;
    } else if (ob_is_enumset_inner_tc(param.get_param_meta().get_type())) { // since enunset_inner type param will mock expr use current param, can not resue plan
      is_same = false;
    } else if (param.is_collection_sql_type()) {
      if (param_info.is_typed_null_value_) {
        is_same = false;
      } else {
        uint64_t udt_id_param = param.get_accuracy().get_accuracy();
        uint64_t udt_id_info = static_cast<uint64_t>(param_info.ext_real_type_) << 32 
                             | static_cast<uint32_t>(param_info.col_type_);
        is_same = (udt_id_info == udt_id_param) ? true : false;
      }
    } else if (param.is_ext_sql_array()) {
      ObDataType data_type;
      if (!param_info.flag_.need_to_check_extend_type_) {
        // do nothing
      } else if (OB_FAIL(ObSQLUtils::get_ext_obj_data_type(param, data_type))) {
      } else if (data_type.get_obj_type() == ObDecimalIntType) {
        is_same = param_info.ext_real_type_ == ObDecimalIntType
                  && data_type.get_scale() == param_info.scale_
                  && match_decint_precision(param_info, data_type.get_precision());
      } else if (data_type.get_scale() == param_info.scale_ &&
                 data_type.get_obj_type() == param_info.ext_real_type_) {
        is_same = true;
      } else {
        is_same = false;
      }
    } else if (param.get_param_meta().is_ext()) {
      if (!param_info.flag_.need_to_check_extend_type_) {
        // do nothing
      } else {
        uint64_t udt_id_param = param.get_accuracy().get_accuracy();
        uint64_t udt_id_info = static_cast<uint64_t>(param_info.ext_real_type_) << 32 
                             | static_cast<uint32_t>(param_info.col_type_);
        is_same = (udt_id_info == udt_id_param) ? true : false;
      }
      LOG_DEBUG("ext match param info", K(param.get_accuracy()), K(param_info), K(is_same), K(ret));
    } else if (param_info.is_typed_null_value_ && !param.is_null()) {
      is_same = false;
    } else if (ObSQLUtils::is_typed_null_with_normal_type(param)
               && !param_info.is_typed_null_value_) { // Typed nulls can only match plans with the same type of nulls.
      is_same = false;
    } else if (param_info.flag_.is_boolean_ != param.is_boolean()) { //bool type not match int type
      is_same = false;
    } else {
      // number params in point and st_point can ignore scale check to share plancache
      // please refrer to ObSqlParameterization::is_ignore_scale_check
      is_same = param_info.flag_.ignore_scale_check_
                ? true
                : (param.get_scale() == param_info.scale_);
      is_same = is_same && match_decint_precision(param_info, param.get_precision());
    }
  }
  return ret;
}
// Match true/false parameter
int ObPlanSet::match_param_bool_value(const ObParamInfo &param_info,
                                      const ObObjParam &param,
                                      bool &is_same) const
{
  int ret = OB_SUCCESS;
  is_same = true;
  bool vec_param_same = true;
  bool first_val = true;
  if (param_info.flag_.need_to_check_bool_value_) {
    bool is_value_true = false;
    if (OB_FAIL(ObObjEvaluator::is_true(param, is_value_true))) {
    } else if (is_value_true != param_info.flag_.expected_bool_value_) {
      is_same = false;
    }
  }

  return ret;
}

int ObPlanSet::match_multi_stmt_info(const ParamStore &params,
                                     const ObIArray<int64_t> &multi_stmt_rowkey_pos,
                                     bool &is_match)
{
  int ret = OB_SUCCESS;
  is_match = false;
  if (multi_stmt_rowkey_pos.empty()) {
    is_match = true;
  } else {
    // check all rowkey are different
    int64_t stmt_count = 0;
    ObSEArray<const ObObj*, 16> binding_data;
    for (int64_t i = 0; OB_SUCC(ret) && i < multi_stmt_rowkey_pos.count(); i++) {
      int64_t pos = multi_stmt_rowkey_pos.at(i);
      if (OB_UNLIKELY(pos < 0 || pos >= params.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected array pos",K(pos), K(params.count()), K(ret));
      } else if (OB_UNLIKELY(!params.at(pos).is_ext_sql_array())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected type", K(params.at(pos)), K(ret));
      } else {
        const ObSqlArrayObj *array_params = reinterpret_cast<const ObSqlArrayObj*>(
                                                  params.at(pos).get_ext());
        if (OB_ISNULL(array_params)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected null", KPC(array_params), K(ret));
        } else if (OB_FAIL(binding_data.push_back(array_params->data_))) {
        } else if (i == 0) {
          stmt_count = array_params->count_;
        } else if (OB_UNLIKELY(stmt_count != array_params->count_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected stmt count", K(ret));
        } else { /*do nothing*/ }
      }
    }
    if (OB_SUCC(ret)) {
      is_match = true;
      HashKey hash_key;
      UniqueHashSet unique_ctx;
      if (OB_FAIL(unique_ctx.create(stmt_count))) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && is_match && i < stmt_count; i++) {
          hash_key.reuse();
          for (int64_t j = 0; OB_SUCC(ret) && j < binding_data.count(); j++) {
            ret = hash_key.rowkey_.push_back(binding_data.at(j)[i]);
          }
          if (OB_SUCC(ret)) {
            ret = unique_ctx.exist_refactored(hash_key);
            if (OB_HASH_EXIST == ret) {
              ret = OB_SUCCESS;
              is_match = false;
              if (REACH_TIME_INTERVAL(10000000)) {
                LOG_INFO("batched multi-stmt does not have the same rowkey", K(i),
                    K(hash_key));
              }
            } else if (OB_HASH_NOT_EXIST == ret) {
              if (OB_FAIL(unique_ctx.set_refactored(hash_key))) {
              }
            } else {
              LOG_WARN("check rowkey distinct failed", K(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}
// Determine whether all parameters in the same column across multiple groups are true/false, and return the first parameter that is true/false

/*//Determine if each obj in the array parameter of param store is always true/false*/
//int ObPlanSet::check_array_bind_same_bool_param(const Ob2DArray<ObParamInfo,
                                                //OB_MALLOC_BIG_BLOCK_SIZE,
                                                //ObWrapperAllocator, false> &param_infos,
                                                //const ParamStore &param_store,
                                                //bool &same_bool_param)
//{
  //int ret = OB_SUCCESS;
  //bool first_val = false;
  //same_bool_param = true;
  //for (int64_t i = 0; OB_SUCC(ret) && same_bool_param && i < param_store.count(); ++i) {
    //if (param_infos.at(i).flag_.need_to_check_bool_value_
        //&& param_store.at(i).is_ext()) {
      ////Check the result of each group of parameters is true/false
      //if (OB_FAIL(check_vector_param_same_bool(param_store.at(i),
                                               //first_val,
                                               //same_bool_param))) {
        //LOG_WARN("fail to check vector param same bool", K(ret));
      //}
    //}
  //} //for end

  //return ret;
/*}*/

//used for add plan
int ObPlanSet::match_params_info(const Ob2DArray<ObParamInfo,
                                         OB_MALLOC_BIG_BLOCK_SIZE,
                                         ObWrapperAllocator, false> &infos,
                                 const ObPlanCacheCtx &pc_ctx,
                                 bool &is_same)
{
  int ret = OB_SUCCESS;
  is_same = true;
  ObSQLSessionInfo *session_info = pc_ctx.sql_ctx_.session_info_;
  if (OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null session_info", K(ret));
  } else if (infos.count() != params_info_.count()) {
    is_same = false;
  } else {
    int64_t N = infos.count();
    for (int64_t i = 0; is_same && i < N; ++i) {
      if (true == is_same
          && (params_info_.at(i).flag_.need_to_check_type_ || need_match_all_params_)) {
        if (infos.at(i).type_ != params_info_.at(i).type_
            || infos.at(i).scale_ != params_info_.at(i).scale_
            || infos.at(i).col_type_ != params_info_.at(i).col_type_
            || (params_info_.at(i).flag_.need_to_check_extend_type_
                && infos.at(i).ext_real_type_ != params_info_.at(i).ext_real_type_)
            || (params_info_.at(i).flag_.is_boolean_ != infos.at(i).flag_.is_boolean_)
            || !match_decint_precision(params_info_.at(i), infos.at(i).precision_)) {
          is_same = false;
        }
      }
      if (true == is_same && params_info_.at(i).flag_.need_to_check_bool_value_) {
        if (infos.at(i).flag_.expected_bool_value_
            != params_info_.at(i).flag_.expected_bool_value_) {
          is_same = false;
        }
      }
    }

    if (is_same && related_user_var_names_.count() > 0) {
      if (related_user_var_names_.count() != pc_ctx.sql_ctx_.related_user_var_names_.count()) {
        is_same = false;
      } else {
        int64_t CNT = related_user_var_names_.count();
        ObPCUserVarMeta tmp_meta;
        for (int64_t i = 0; OB_SUCC(ret) && is_same && i < CNT; i++) {
          if (related_user_var_names_.at(i) != pc_ctx.sql_ctx_.related_user_var_names_.at(i)) {
            is_same = false;
          } else if (OB_FAIL(get_variable_meta(pc_ctx.sql_ctx_.session_info_,
                      related_user_var_names_.at(i), tmp_meta))) {
          } else {
            is_same = (related_user_sess_var_metas_.at(i) == tmp_meta);
          }
        }
      }
    }
    if (OB_SUCC(ret) && is_same) {
      if (OB_FAIL(ObPlanCacheObject::match_pre_calc_cons(all_pre_calc_constraints_, pc_ctx,
                                                         is_ignore_stmt_, is_same))) {
      } else if (!is_same) {
      }
    }

    if (is_sql_planset() && OB_SUCC(ret) && is_same) {
      CK( OB_NOT_NULL(pc_ctx.exec_ctx_.get_physical_plan_ctx()) );
      if (OB_SUCC(ret)) {
        const ParamStore &params = pc_ctx.exec_ctx_.get_physical_plan_ctx()->get_param_store();
        OC( (match_constraint)(params, is_same));
        OC( (match_cons)(pc_ctx, is_same));
      }
    }
  }
  if (OB_FAIL(ret)) {
    is_same = false;
  }
  return ret;
}

bool ObPlanSet::can_skip_params_match()
{
  bool can_skip = true;
  for (int64_t i = 0; can_skip && i < params_info_.count(); i++) {
    if (params_info_.at(i).flag_.need_to_check_type_) {
      can_skip = false;
    }
  }
  if (can_skip) {
    if (!all_plan_const_param_constraints_.empty() ||
        !all_possible_const_param_constraints_.empty() ||
        !all_equal_param_constraints_.empty() ||
        all_pre_calc_constraints_.get_size() != 0) {
      can_skip = false;
      LOG_DEBUG("print can't skip", K(can_skip), K(all_plan_const_param_constraints_.empty()),
      K(all_possible_const_param_constraints_.empty()),
      K(all_equal_param_constraints_.empty()),
      K(all_pre_calc_constraints_.get_size()));
    }
  }
  return can_skip;
}

bool ObPlanSet::can_delay_init_datum_store()
{
  bool can_delay = true;
  if (all_pre_calc_constraints_.get_size() != 0) {
    can_delay = false;
  }
  return can_delay;
}

void ObPlanSet::reset()
{
  ObDLinkBase<ObPlanSet>::reset();
  plan_cache_value_ = NULL;
  params_info_.reset();
  stmt_type_ = stmt::T_NONE;
  fetch_cur_time_ = false;
  is_ignore_stmt_ = false;
  //is_wise_join_ = false;
  related_user_var_names_.reset();
  related_user_sess_var_metas_.reset();

  all_possible_const_param_constraints_.reset();
  all_plan_const_param_constraints_.reset();
  all_equal_param_constraints_.reset();
  all_pre_calc_constraints_.reset();
  can_skip_params_match_ = false;
  can_delay_init_datum_store_ = false;
  alloc_.reset();
}

ObPlanCache *ObPlanSet::get_plan_cache() const
{
  ObPlanCache *pc = NULL;
  if (NULL == plan_cache_value_
      || NULL == plan_cache_value_->get_pcv_set()
      || NULL == plan_cache_value_->get_pcv_set()->get_plan_cache()) {
    pc = NULL;
  } else {
    pc = plan_cache_value_->get_pcv_set()->get_plan_cache();
  }
  return pc;
}

int ObPlanSet::remove_cache_obj_entry(const ObCacheObjID obj_id)
{
  int ret = OB_SUCCESS;
  ObPlanCache *pc = NULL;
  ObPCVSet *pcv_set = NULL;
  if (OB_ISNULL(get_plan_cache_value())
     || OB_ISNULL(pcv_set = get_plan_cache_value()->get_pcv_set())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(pcv_set));
  } else if (NULL == (pc = get_plan_cache())) {
    LOG_WARN("invalid argument", K(pc));
  } else if (OB_FAIL(pcv_set->remove_cache_obj_entry(obj_id))) {
  } else if (OB_FAIL(pc->remove_cache_obj_stat_entry(obj_id))) {
  }
  return ret;
}

int ObPlanSet::init_new_set(const ObPlanCacheCtx &pc_ctx,
                            const ObPlanCacheObject &plan,
                            common::ObIAllocator* pc_alloc_)
{
  int ret = OB_SUCCESS;
  ObPlanCache *pc = nullptr;
  const ObSqlCtx &sql_ctx = pc_ctx.sql_ctx_;
  const ObSQLSessionInfo *session_info = sql_ctx.session_info_;
  if (OB_ISNULL(pc = get_plan_cache()) || OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null plan cache or session info", K(ret), K(pc), K(session_info));
  } else {
    
    alloc_.set_ctx_id(ObCtxIds::PLAN_CACHE_CTX_ID);
  }
  if (OB_SUCC(ret)) {
    char *buf = NULL;
    ObString var_name;
    ObSessionVariable sess_var;

    fetch_cur_time_ = plan.get_fetch_cur_time();
    stmt_type_ = plan.get_stmt_type();
    is_ignore_stmt_ = plan.is_ignore();
    //add param info
    params_info_.reset();
    if (OB_FAIL(init_pre_calc_exprs(plan, pc_alloc_))) {
    } else if (OB_FAIL(params_info_.reserve(plan.get_params_info().count()))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < plan.get_params_info().count(); ++i) {
      if (OB_FAIL(params_info_.push_back(plan.get_params_info().at(i)))) {
      }
    }
    need_match_all_params_ = sql_ctx.need_match_all_params_;

    // add user session vars if necessary
    CK( OB_NOT_NULL(sql_ctx.session_info_) );
    if (OB_SUCC(ret) && sql_ctx.related_user_var_names_.count() > 0) {
      related_user_var_names_.reset();
      related_user_var_names_.set_allocator(&alloc_);
      related_user_sess_var_metas_.reset();
      related_user_sess_var_metas_.set_allocator(&alloc_);

      int64_t N = sql_ctx.related_user_var_names_.count();
      OZ( related_user_var_names_.init(N), N );
      OZ( related_user_sess_var_metas_.init(N), N );
      for (int64_t i = 0; OB_SUCC(ret) && i < sql_ctx.related_user_var_names_.count(); i++) {
        buf = (char *)alloc_.alloc(sql_ctx.related_user_var_names_.at(i).length());
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate memory",
                   K(ret), K(sql_ctx.related_user_var_names_.at(i).length()));
        } else {
          MEMCPY(buf, sql_ctx.related_user_var_names_.at(i).ptr(), sql_ctx.related_user_var_names_.at(i).length());
          var_name.assign_ptr(buf, sql_ctx.related_user_var_names_.at(i).length());
          OC( (related_user_var_names_.push_back)(var_name) );
        }
      }

      ObPCUserVarMeta tmp_meta;
      for (int64_t i = 0 ; OB_SUCC(ret) && i < related_user_var_names_.count(); i++) {
        if (OB_FAIL(get_variable_meta(pc_ctx.sql_ctx_.session_info_,
              related_user_var_names_.at(i), tmp_meta))) {
        }
        OC( (related_user_sess_var_metas_.push_back)(tmp_meta) );
      }

      if (OB_FAIL(ret)) {
        related_user_var_names_.reset();
        related_user_sess_var_metas_.reset();
      }
    }

    // init const param constraints
    ObPlanSetType ps_t = get_plan_set_type_by_cache_obj_type(plan.get_ns());
    if (PST_PRCD == ps_t) {
        // pl does not have any const param constraint
        all_possible_const_param_constraints_.reset();
        all_plan_const_param_constraints_.reset();
        all_equal_param_constraints_.reset();
        all_pre_calc_constraints_.reset();
    } else if (PST_SQL_CRSR == ps_t) {
      // otherwise it should not be empty
      CK( OB_NOT_NULL(sql_ctx.all_plan_const_param_constraints_),
          OB_NOT_NULL(sql_ctx.all_possible_const_param_constraints_),
          OB_NOT_NULL(sql_ctx.all_equal_param_constraints_),
          OB_NOT_NULL(sql_ctx.all_pre_calc_constraints_));
      OZ( (set_const_param_constraint)(*sql_ctx.all_plan_const_param_constraints_, false) );
      OZ( (set_const_param_constraint)(*sql_ctx.all_possible_const_param_constraints_, true) );
      OZ( (set_equal_param_constraint)(*sql_ctx.all_equal_param_constraints_) );
      OZ( (set_pre_calc_constraint(*sql_ctx.all_pre_calc_constraints_)));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get an unexpected plan set type", K(ps_t), K(plan.get_ns()));
    }

    // initialize multi_stmt rowkey pos
    if (OB_SUCC(ret) && sql_ctx.multi_stmt_rowkey_pos_.count() > 0) {
      if (OB_FAIL(multi_stmt_rowkey_pos_.init(sql_ctx.multi_stmt_rowkey_pos_.count()))) {
      } else if (OB_FAIL(append(multi_stmt_rowkey_pos_, sql_ctx.multi_stmt_rowkey_pos_))) {
      } else { /*do nothing*/ }
    }

    if (OB_SUCC(ret) && sql_ctx.is_do_insert_batch_opt()) {
      can_skip_params_match_ = can_skip_params_match();
      can_delay_init_datum_store_ = can_delay_init_datum_store();
    }
  }

 return ret;
}

int ObPlanSet::set_const_param_constraint(ObIArray<ObPCConstParamInfo> &const_param_constraint,
                                          const bool is_all_constraint)
{
  int ret = OB_SUCCESS;
  ConstParamConstraint &cons_array = (is_all_constraint ?
                                      all_possible_const_param_constraints_ : all_plan_const_param_constraints_);
  cons_array.reset();
  cons_array.set_allocator(&alloc_);

  if (const_param_constraint.count() > 0) {
    if (OB_FAIL(cons_array.prepare_allocate(const_param_constraint.count()))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < const_param_constraint.count(); i++) {
        ObPCConstParamInfo &tmp_info = cons_array.at(i);
        tmp_info = const_param_constraint.at(i);
        if (tmp_info.const_idx_.count() <= 0 ||
            tmp_info.const_params_.count() <= 0 ||
            tmp_info.const_idx_.count() != tmp_info.const_params_.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected const param info", K(tmp_info.const_idx_), K(tmp_info.const_params_));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < tmp_info.const_params_.count(); i++) {
            if (tmp_info.const_params_.at(i).need_deep_copy()) {
              const ObObj &src_obj = tmp_info.const_params_.at(i);
              int64_t deep_cp_size = tmp_info.const_params_.at(i).get_deep_copy_size();
              int64_t pos = 0;
              char *tmp_buf = NULL;

              if (OB_ISNULL(tmp_buf = (char *)alloc_.alloc(deep_cp_size))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                LOG_WARN("failed to allocate mem", K(ret));
              } else if (OB_FAIL(tmp_info.const_params_.at(i).deep_copy(src_obj, tmp_buf, deep_cp_size, pos))) {
              } else if (pos != deep_cp_size) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("deep copy went wrong", K(ret));
              } else {
                // do nothing
              }
            }
          } // for end
        }
      } // for end
    }
  }

  if (OB_FAIL(ret)) {
    cons_array.reset();
  }
  return ret;
}

int ObPlanSet::set_equal_param_constraint(common::ObIArray<ObPCParamEqualInfo> &equal_param_constraint)
{
  int ret = OB_SUCCESS;
  all_equal_param_constraints_.reset();
  all_equal_param_constraints_.set_allocator(&alloc_);
  if (equal_param_constraint.empty()) {
    //do nothing
  } else if (OB_FAIL(all_equal_param_constraints_.init(equal_param_constraint.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < equal_param_constraint.count(); ++i) {
    ObPCParamEqualInfo &equal_info = equal_param_constraint.at(i);
    if (equal_info.first_param_idx_ < 0 || equal_info.second_param_idx_ < 0 ||
        equal_info.first_param_idx_ > params_info_.count() ||
        equal_info.second_param_idx_ > params_info_.count() ||
        equal_info.first_param_idx_ == equal_info.second_param_idx_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get invalid equal param constraint", K(ret), K(equal_info));
    } else if (OB_FAIL(all_equal_param_constraints_.push_back(equal_info))) {
    }
  }
  return ret;
}

// adds pre calc constraint
int ObPlanSet::set_pre_calc_constraint(common::ObDList<ObPreCalcExprConstraint> &pre_calc_cons)
{
  int ret = OB_SUCCESS;
  ObPreCalcExprConstraint *pre_calc_constraint = NULL;
  void *cons_buf = NULL;
  DLIST_FOREACH(cur_cons, pre_calc_cons) {
    if (OB_ISNULL(cons_buf = alloc_.alloc(sizeof(ObPreCalcExprConstraint)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret));
    } else {
      pre_calc_constraint = new(cons_buf)ObPreCalcExprConstraint(alloc_);
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(pre_calc_constraint->assign(*cur_cons, alloc_))) {
    } else if (OB_UNLIKELY(!all_pre_calc_constraints_.add_last(pre_calc_constraint))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to add element to dlist", K(ret));
    }
  }
  return ret;
}

// match actually constraint
int ObPlanSet::match_cons(const ObPlanCacheCtx &pc_ctx, bool &is_matched)
{
  int ret = OB_SUCCESS;
  ObIArray<ObPCConstParamInfo> *param_cons = pc_ctx.sql_ctx_.all_plan_const_param_constraints_;
  ObIArray<ObPCConstParamInfo> *possible_param_cons =
                                        pc_ctx.sql_ctx_.all_possible_const_param_constraints_;
  ObIArray<ObPCParamEqualInfo> *equal_cons = pc_ctx.sql_ctx_.all_equal_param_constraints_;
  is_matched = true;

  if (OB_ISNULL(param_cons) ||
      OB_ISNULL(possible_param_cons) ||
      OB_ISNULL(equal_cons)) {
    is_matched = false;
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(param_cons), K(possible_param_cons), K(equal_cons));
  } else if (param_cons->count() != all_plan_const_param_constraints_.count() ||
             possible_param_cons->count() != all_possible_const_param_constraints_.count() ||
             equal_cons->count() != all_equal_param_constraints_.count()) {
    is_matched = false;
  } else {
    for (int64_t i=0; is_matched && i < all_plan_const_param_constraints_.count(); i++) {
      is_matched = (all_plan_const_param_constraints_.at(i)==param_cons->at(i));
    }
    for (int64_t i=0; is_matched && i < all_possible_const_param_constraints_.count(); i++) {
      is_matched = (all_possible_const_param_constraints_.at(i)==possible_param_cons->at(i));
    }
    for (int64_t i=0; is_matched && i < all_equal_param_constraints_.count(); i++) {
      is_matched = (all_equal_param_constraints_.at(i)==equal_cons->at(i));
    }
  }

  return ret;
}
// Constant constraint check logic:
// 1. all_plan_const_param_constraints_ is not empty, check if the constraints of all_plan_const_param_constraints_ are satisfied,
//    Satisfy then hit plan_set, otherwise not hit;
// 2. Otherwise, check all possible constant constraints, if one of the constraints is satisfied, then a new plan needs to be generated, that is, it does not hit, otherwise it hits
// 3. Check if the parameter constraints that require equality are satisfied
int ObPlanSet::match_constraint(const ParamStore &params, bool &is_matched)
{
  int ret = OB_SUCCESS;
  is_matched = true;

  if (all_plan_const_param_constraints_.count() > 0) { // check all_plan_const_param_constraints_ first
    for (int64_t i = 0; is_matched && OB_SUCC(ret) && i < all_plan_const_param_constraints_.count(); i++) {
      const ObPCConstParamInfo &const_param_info = all_plan_const_param_constraints_.at(i);
      CK( const_param_info.const_idx_.count() > 0,
          const_param_info.const_params_.count() > 0,
          const_param_info.const_idx_.count() == const_param_info.const_params_.count() );

      for (int64_t j = 0; is_matched && OB_SUCC(ret) && j < const_param_info.const_idx_.count(); j++) {
        const int64_t param_idx = const_param_info.const_idx_.at(j);
        const ObObj &const_param = const_param_info.const_params_.at(j);
        if (param_idx >= params.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get an unexpected param index", K(ret), K(param_idx), K(params.count()));
        } else if (const_param.is_invalid_type() ||
                   params.at(param_idx).is_invalid_type()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected invalid type",
                   K(ret), K(const_param.get_type()), K(params.at(param_idx).get_type()));
        } else if (!const_param.can_compare(params.at(param_idx)) ||
                   0 != const_param.compare(params.at(param_idx))) {
          LOG_TRACE("not matched const param", K(const_param), K(params.at(param_idx)));
          is_matched = false;
        } else {
          // do nothing
        }
      }
    }
  } else if (all_possible_const_param_constraints_.count() > 0) {
    // check if possible generated column exists
    for (int64_t i = 0; is_matched && OB_SUCC(ret) && i < all_possible_const_param_constraints_.count(); i++) {
      bool match_const = true;
      const ObPCConstParamInfo &const_param_info = all_possible_const_param_constraints_.at(i);
      CK( const_param_info.const_idx_.count() > 0,
          const_param_info.const_params_.count() > 0,
          const_param_info.const_idx_.count() == const_param_info.const_params_.count() );
      for (int64_t j = 0; match_const && OB_SUCC(ret) && j < const_param_info.const_idx_.count(); j++) {
        const int64_t param_idx = const_param_info.const_idx_.at(j);
        const ObObj &const_param = const_param_info.const_params_.at(j);
        if (param_idx >= params.count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get an unexpected param index", K(ret), K(param_idx), K(params.count()));
        } else if (const_param.is_invalid_type() ||
                   params.at(param_idx).is_invalid_type()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected invalid type",
                   K(ret), K(const_param.get_type()), K(params.at(param_idx).get_type()));
        } else if (!const_param.can_compare(params.at(param_idx)) ||
                   0 != const_param.compare(params.at(param_idx))) {
          match_const = false;
        } else {
          // do nothing
        }
      }
      if (match_const) {
        LOG_TRACE("matched const param constraint", K(params), K(all_possible_const_param_constraints_.at(i)));
        is_matched = false; // matching one of the constraint, need to generated new plan
      }
    }
  } else {
    // do nothing
  }

  for (int64_t i = 0; is_matched && OB_SUCC(ret) && i < all_equal_param_constraints_.count(); ++i) {
    int64_t first_idx = all_equal_param_constraints_.at(i).first_param_idx_;
    int64_t second_idx = all_equal_param_constraints_.at(i).second_param_idx_;
    common::ObObjParam param1 = params.at(first_idx);
    common::ObObjParam param2 = params.at(second_idx);
    param1.set_collation_type(CS_TYPE_BINARY);
    param2.set_collation_type(CS_TYPE_BINARY);

    if (OB_UNLIKELY(first_idx < 0 || first_idx >= params.count() ||
                    second_idx < 0 || second_idx >= params.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("param index is invalid", K(ret), K(params.count()), K(first_idx), K(second_idx));
    } else if (!all_equal_param_constraints_.at(i).use_abs_cmp_ &&
               param1.can_compare(param2) &&
               param1.get_collation_type() == param2.get_collation_type()) {
      is_matched = (0 == param1.compare(param2));
    } else if (all_equal_param_constraints_.at(i).use_abs_cmp_) {
      /*
       * for plan like: select ? from t1 group by grouping sets (c1, ?);
       * if  plan has absequal constraint,
       * then both "select -1 from t1 group by grouping sets (c1, 1);"
       *       and "select 1 from t1 group by grouping sets (c1, 1);"
       * will hit the plan.
       * but "select -2 from t1 group by grouping sets (c1, 1);" won't hit the plan.
       */
      if (param1.is_number() && param2.is_number()) {
        is_matched = (0 == param1.get_number().abs_compare(param2.get_number()));
      } else if (param1.is_double() && param2.is_double()) {
        is_matched = (0 == param1.get_double() + param2.get_double()) ||
                     (param1.get_double() == param2.get_double());
      } else if (param1.is_float() && param2.is_float()) {
        is_matched = (0 == param1.get_float() + param2.get_float()) ||
                     (param1.get_float() == param2.get_float());
      } else if (param1.is_decimal_int() && param2.is_decimal_int()) {
        is_matched = wide::abs_equal(param1, param2);
      } else if (param1.can_compare(param2) &&
                 param1.get_collation_type() == param2.get_collation_type()) {
        is_matched = (0 == param1.compare(param2));
      }
    }
    if (OB_SUCC(ret) && !is_matched) {
      is_matched = false;
    }
  }

  if (OB_FAIL(ret)) {
    is_matched = false;
  }

  return ret;
}

int ObPlanSet::init_pre_calc_exprs(const ObPlanCacheObject &phy_plan,
                                   common::ObIAllocator* pc_alloc_)
{
  int ret = OB_SUCCESS;
  void *buf = NULL;

  if (phy_plan.get_pre_calc_frames().get_size() == 0) {
    // have no pre calculable expression, not initialize pre calculable expression handle.
    pre_cal_expr_handler_ = NULL;
  } else if (OB_ISNULL(pc_alloc_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("plan cache allocator has not been initialized.");
  } else if(OB_ISNULL( buf = pc_alloc_->alloc(sizeof(PreCalcExprHandler)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory.", K(ret));
  } else {
    pre_cal_expr_handler_ = new(buf)PreCalcExprHandler();
    pre_cal_expr_handler_->init(pc_alloc_);
    buf = NULL;
    common::ObIAllocator& pre_expr_alloc = (pre_cal_expr_handler_->alloc_);

    if (OB_ISNULL(buf = pre_expr_alloc.alloc(sizeof(common::ObDList<ObPreCalcExprFrameInfo>)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory.", K(ret));
    } else {
      pre_cal_expr_handler_->pre_calc_frames_ =
                              new (buf)common::ObDList<ObPreCalcExprFrameInfo>;

      common::ObDList<ObPreCalcExprFrameInfo>* pre_calc_frames =
                                                pre_cal_expr_handler_->pre_calc_frames_;
      ObPreCalcExprFrameInfo *pre_calc_frame = NULL;
      void *frame_buf = NULL;
      DLIST_FOREACH(frame, phy_plan.get_pre_calc_frames()) {
        if (OB_ISNULL(frame_buf = pre_expr_alloc.alloc(sizeof(ObPreCalcExprFrameInfo)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate memory", K(ret));
        } else if (FALSE_IT(pre_calc_frame = new(frame_buf)ObPreCalcExprFrameInfo(
                                                                     pre_expr_alloc))) {
          // do nothing
        } else if (OB_FAIL(pre_calc_frame->assign(*frame, pre_expr_alloc))) {
        } else if (OB_UNLIKELY(!pre_calc_frames->add_last(pre_calc_frame))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to add element to dlist", K(ret));
        } else {
          frame_buf = NULL;
          pre_calc_frame = NULL;
        }
      }
    }
  }
  return ret;
}


int ObSqlPlanSet::add_cache_obj(ObPlanCacheObject &cache_object,
                                ObPlanCacheCtx &pc_ctx,
                                int &add_ret)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!cache_object.is_sql_crsr())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("cache_object type is invalid", K(cache_object.get_ns()));
  } else {
    ret = add_plan(static_cast<ObPhysicalPlan&>(cache_object), pc_ctx);
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(pre_cal_expr_handler_)) {
    // have no pre-calculable expression, do nothing
  } else {
    cache_object.set_pre_calc_expr_handler(pre_cal_expr_handler_);
    cache_object.inc_pre_expr_ref_count();
    // planset, handle, plan, ref_cnt(val)
    LOG_INFO("add pre calculable expression.", KP(this),
                                               KP(cache_object.get_pre_calc_expr_handler()),
                                               KP(&cache_object),
                                               K(cache_object.get_pre_expr_ref_count()));
  }
  add_ret = ret;
  return ret;
}

int ObSqlPlanSet::add_plan(ObPhysicalPlan &plan,
                           ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  ObSqlCtx &sql_ctx = pc_ctx.sql_ctx_;
  //DASTableLocList table_locs(pc_ctx.exec_ctx_.get_allocator());
  ObArray<ObCandiTableLoc> candi_table_locs;
  ObPhyPlanType plan_type = OB_PHY_PLAN_UNINITIALIZED;
  if (OB_ISNULL(plan_cache_value_) ||
      OB_ISNULL(pc_ctx.exec_ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_PC_LOG(WARN, "invalid argument", KP(plan_cache_value_), K(ret));
  } else if (OB_FAIL(get_phy_locations(sql_ctx.get_partition_infos(),
                                       //table_locs,
                                       candi_table_locs))) {
  } else {
    // do nothing
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    if (pc_ctx.exec_ctx_.get_physical_plan_ctx()->get_or_expand_transformed()) {
      need_try_plan_ |= TRY_PLAN_OR_EXPAND;
    }
    if (plan.get_is_late_materialized()) {
      need_try_plan_ |= TRY_PLAN_LATE_MAT;
    }
    if (plan.has_uncertain_local_operator()) {
      need_try_plan_ |= TRY_PLAN_UNCERTAIN;
    }
    if (plan.contain_index_location()) {
      need_try_plan_ |= TRY_PLAN_INDEX;
    }
    plan_type = plan.get_plan_type();
    if (OB_SUCC(ret)) {
      switch(plan_type) {
      case OB_PHY_PLAN_LOCAL:{
        if (is_multi_stmt_plan()) {
          if (NULL != array_binding_plan_) {
            ret = OB_SQL_PC_PLAN_DUPLICATE;
          } else {
            array_binding_plan_ = &plan;
          }
        } else {
          is_single_table_ = (1 == sql_ctx.get_partition_info_count());
          if (OB_FAIL(add_physical_plan(OB_PHY_PLAN_LOCAL, pc_ctx, plan))) {
          }
        }
      } break;
      case OB_PHY_PLAN_DISTRIBUTED: {
        is_single_table_ = (1 == sql_ctx.get_partition_info_count());
        if (OB_FAIL(add_physical_plan(OB_PHY_PLAN_DISTRIBUTED, pc_ctx, plan))) {
        } else {
        }
      } break;
      default:
        ret = OB_ERR_UNEXPECTED;
        SQL_PC_LOG(WARN, "unknown plan type", K(plan_type), K(ret));
        break;
      }
    }
  }
  return ret;
}

int ObSqlPlanSet::init_new_set(const ObPlanCacheCtx &pc_ctx,
                               const ObPlanCacheObject &plan,
                               common::ObIAllocator* pc_malloc_)
{
  int ret = OB_SUCCESS;
  const ObSqlCtx &sql_ctx = pc_ctx.sql_ctx_;
  need_try_plan_ = 0;
  const ObSQLSessionInfo *session_info = sql_ctx.session_info_;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null plan cache or session info", K(ret), K(session_info));
  } else if (OB_ISNULL(pc_malloc_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("pc_allocator has not been initialized.", K(ret));
  } else if (OB_FAIL(ObPlanSet::init_new_set(pc_ctx, plan, pc_malloc_))) {
  } else if (OB_FAIL(table_locations_.prepare_allocate_and_keep_count(sql_ctx.get_partition_info_count(),
                                                        *plan_cache_value_->get_pcv_set()->get_allocator()))) {
  } else if (OB_FAIL(dist_plans_.init(this))) {
  } else {
    //if (pc_ctx.sql_ctx_.multi_stmt_rowkey_pos_.empty()) {
      //for (int64_t i = 0; i < plan.get_params_info().count(); ++i) {
        //if (ObExtendType == plan.get_params_info().at(i).type_) {
          //has_array_binding_ = true;
        //}
      //}
    //}
    for (int64_t i = 0; !is_contain_virtual_table_ && i < plan.get_dependency_table().count(); i++) {
      const ObSchemaObjVersion &schema_obj = plan.get_dependency_table().at(i);
      if (TABLE_SCHEMA == schema_obj.get_schema_type()
          && is_virtual_table(schema_obj.object_id_)) {
        is_contain_virtual_table_ = true;
      }
    } // for end
    for (int64_t i = 0; !is_contain_inner_table_ && i < plan.get_dependency_table().count(); i++) {
      const ObSchemaObjVersion &schema_obj = plan.get_dependency_table().at(i);
      if (is_inner_table(schema_obj.object_id_)) {
        is_contain_inner_table_ = true;
      }
    } // for end
  }

  bool contain_index_location = false;
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (NS_CRSR != plan.get_ns()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected cache object type", K(ret), K(plan.get_ns()));
  } else {
    const ObPhysicalPlan &sql_plan = dynamic_cast<const ObPhysicalPlan &>(plan);
    enable_inner_part_parallel_exec_ = sql_plan.get_px_dop() > 1;
    contain_index_location = sql_plan.contain_index_location();
  }
  if (OB_SUCC(ret) && (!contain_index_location || is_multi_stmt_plan())) {
    const ObTablePartitionInfoArray &partition_infos = sql_ctx.get_partition_infos();
    int64_t N = partition_infos.count();
    //copy table location
    for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
      if (NULL == partition_infos.at(i)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(table_locations_.push_back(partition_infos.at(i)->get_table_location()))) {
      } else if (is_all_non_partition_
                 && partition_infos.at(i)->get_table_location().is_partitioned()) {
        is_all_non_partition_ = false;
      }
    } // for end
  }

 return ret;
}

int ObSqlPlanSet::select_plan(ObPlanCacheCtx &pc_ctx, ObPlanCacheObject *&cache_obj)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlan *plan = NULL;
  if (OB_ISNULL(plan_cache_value_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("location cache not init", K(plan_cache_value_), K(ret));
  } else {
    if (OB_FAIL(get_plan_special(pc_ctx, plan))) {
      if (OB_SQL_PC_NOT_EXIST == ret) {
      } else {
        LOG_WARN("fail to get plan special", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    cache_obj = plan;
  }
  return ret;
}

int ObSqlPlanSet::add_physical_plan(const ObPhyPlanType plan_type,
                                    ObPlanCacheCtx &pc_ctx,
                                    ObPhysicalPlan &plan)
{
  int ret = OB_SUCCESS;
  if (plan_type != OB_PHY_PLAN_LOCAL && plan_type != OB_PHY_PLAN_DISTRIBUTED) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid plan type", K(ret), K(plan_type));
  } else if (OB_PHY_PLAN_LOCAL == plan_type) {
    if (OB_FAIL(add_local_plan(plan))) {
    }
  } else if (OB_FAIL(dist_plans_.add_plan(plan, pc_ctx))) {
  }
  return ret;
}


int ObSqlPlanSet::try_get_local_plan(ObPlanCacheCtx &pc_ctx,
                                     ObPhysicalPlan *&plan,
                                     bool &get_next)
{
  int ret = OB_SUCCESS;
  plan = NULL;
  get_next = false;
  ObExecContext &exec_ctx = pc_ctx.exec_ctx_;
  ObPhyPlanType real_type = OB_PHY_PLAN_UNINITIALIZED;
  ObSEArray<ObCandiTableLoc, 2> candi_table_locs;
  ObPhysicalPlan *local_plan = get_local_plan();
  if (OB_ISNULL(local_plan)) {
    get_next = true;
  } else {
    pc_ctx.exist_local_plan_ = true;
    if (FALSE_IT(plan = local_plan)) {
    } else if (OB_FAIL(get_plan_type(plan->get_table_locations(),
                                     plan->has_uncertain_local_operator(), pc_ctx, candi_table_locs,
                                     real_type))) {
    } else if (OB_PHY_PLAN_LOCAL != real_type) {
      plan = NULL;
      get_next = true;
    } else if (GCONF._enable_adaptive_auto_dop && plan->get_is_use_auto_dop() && is_single_table_
               && !is_contain_inner_table_ && !plan->stat_.is_inner_) {
      int64_t dop = -1;
      bool is_single_part = false;
      ObAdaptiveAutoDop adaptive_auto_dop(exec_ctx);
      AutoDopHashMap &auto_dop_map = exec_ctx.get_auto_dop_map();
      if (OB_FAIL(adaptive_auto_dop.calculate_table_auto_dop(*plan, auto_dop_map, is_single_part))) {
      } else if (OB_FAIL(auto_dop_map.get_refactored(0, dop))) {
      } else if (dop > 1) {
        plan = NULL;
        get_next = true;
      }
      if (OB_FAIL(ret)) {
        auto_dop_map.clear();
      }
    }
  }
  if (OB_SUCC(ret) && NULL == plan) {
    get_next = true;
  }
  return ret;
}

int ObSqlPlanSet::try_get_dist_plan(ObPlanCacheCtx &pc_ctx,
                                    ObPhysicalPlan *&plan)
{
  int ret = OB_SUCCESS;
  plan = NULL;
  ObExecContext &exec_ctx = pc_ctx.exec_ctx_;
  if (OB_FAIL(dist_plans_.get_plan(pc_ctx, plan))) {
  } else if (plan != NULL) {
    if (GCONF._enable_adaptive_auto_dop && plan->get_is_use_auto_dop() && is_single_table_
        && !is_contain_inner_table_ && !plan->stat_.is_inner_) {
      int64_t dop = -1;
      bool is_single_part = false;
      ObAdaptiveAutoDop adaptive_auto_dop(exec_ctx);
      AutoDopHashMap &auto_dop_map = exec_ctx.get_auto_dop_map();
      if (OB_FAIL(adaptive_auto_dop.calculate_table_auto_dop(*plan, auto_dop_map, is_single_part))) {
      } else if (OB_FAIL(auto_dop_map.get_refactored(0, dop))) {
      } else if (is_single_part && !pc_ctx.exist_local_plan_ && dop <= 1) {
        plan = NULL;
        exec_ctx.set_force_gen_local_plan();
      }
      if (OB_FAIL(ret)) {
        auto_dop_map.clear();
      }
    }
  }
  if (OB_SQL_PC_NOT_EXIST == ret) {
    ret = OB_SUCCESS;
    plan = NULL;
  }
  return ret;
}

int ObSqlPlanSet::get_plan_special(ObPlanCacheCtx &pc_ctx,
                                   ObPhysicalPlan *&plan)
{
  int ret = OB_SUCCESS;
  plan = NULL;
  bool get_next = true;
  ObPhyPlanType real_type = OB_PHY_PLAN_UNINITIALIZED;
  ObSEArray<ObCandiTableLoc, 2> candi_table_locs;
  // try local plan
  if (OB_SUCC(ret) && get_next) {
    if (OB_FAIL(try_get_local_plan(pc_ctx, plan, get_next))) {
    }
  }
  //try dist plan
  if (OB_SUCC(ret) && get_next) {
    if (OB_FAIL(try_get_dist_plan(pc_ctx, plan))) {
    }
  }
  if (OB_SUCC(ret) && nullptr == plan) {
    ret = OB_SQL_PC_NOT_EXIST;
  }
  return ret;
}

int64_t ObSqlPlanSet::get_mem_size()
{
  int64_t plan_set_mem = 0;
  plan_set_mem += get_local_plan_mem_size();
  plan_set_mem += dist_plans_.get_mem_size();
  return plan_set_mem;
}

void ObSqlPlanSet::reset()
{
  is_all_non_partition_ = true;
  need_try_plan_ = 0;
  //has_array_binding_ = false;
  is_contain_virtual_table_ = false;
  is_contain_inner_table_ = false;
  enable_inner_part_parallel_exec_ = false;
  table_locations_.reset();
  if (OB_ISNULL(plan_cache_value_)
      || OB_ISNULL(plan_cache_value_->get_pc_alloc())) {
    //do nothing
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "plan_cache_value or pc allocator is NULL");
  }
  array_binding_plan_ = NULL;
  direct_local_plan_ = NULL;
  local_plans_.reset();
  dist_plans_.reset();
  //local_phy_locations_.reset();
  //partition_key_.reset();
  ObPlanSet::reset();
}

// Get the local tablet locations used by a cached plan.
int ObSqlPlanSet::get_phy_locations(const ObIArray<ObTableLocation> &table_locations,
                                    ObPlanCacheCtx &pc_ctx,
                                    ObIArray<ObCandiTableLoc> &candi_table_locs)
{
  int ret = OB_SUCCESS;
  DAS_CTX(pc_ctx.exec_ctx_).clear_all_location_info();
  if (OB_FAIL(ObPhyLocationGetter::get_phy_locations(table_locations, pc_ctx, candi_table_locs))) {
  } else if (candi_table_locs.empty()) {
    // do nothing.
  } else if (OB_FAIL(ObPhyLocationGetter::build_table_locs(
               pc_ctx.exec_ctx_.get_das_ctx(), table_locations, candi_table_locs))) {
  }
  return ret;
}

// A multi-tablet plan may still use local parallel execution; every tablet location is local.
int ObSqlPlanSet::calc_phy_plan_type_v2(const ObPlanCacheCtx &pc_ctx,
                                        ObPhyPlanType &plan_type)
{
  int ret = OB_SUCCESS;
  ObDASCtx &das_ctx = pc_ctx.exec_ctx_.get_das_ctx();
  const DASTableLocList &table_locs = das_ctx.get_table_loc_list();
  int64_t N = table_locs.size();
  if (0 == N) {
    plan_type = OB_PHY_PLAN_LOCAL;
  } else {
    bool is_all_empty = true;
    bool is_all_single_partition = true;
    FOREACH_X(table_loc, table_locs, is_all_single_partition)
    {
      const DASTabletLocList &tablet_locs = (*table_loc)->get_tablet_locs();
      if (tablet_locs.size() != 0) {
        is_all_empty = false;
      }
      if (tablet_locs.size() > 1) {
        is_all_single_partition = false;
      }
    }

    if (is_all_empty) {
      plan_type = OB_PHY_PLAN_LOCAL;
    } else if (is_all_single_partition) {
      plan_type = OB_PHY_PLAN_LOCAL;
    } else {
      plan_type = OB_PHY_PLAN_DISTRIBUTED;
    }
  }
  return ret;
}


void ObSqlPlanSet::remove_all_plan()
{
  IGNORE_RETURN dist_plans_.remove_all_plan();
}


//add plan used
int ObSqlPlanSet::get_phy_locations(const ObTablePartitionInfoArray &partition_infos,
                                    //ObIArray<ObDASTableLoc> &table_locs,
                                    ObIArray<ObCandiTableLoc> &candi_table_locs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObPhyLocationGetter::get_phy_locations(partition_infos,
                                                     //table_locs,
                                                     candi_table_locs))) {
  } else {/* do nothing */}
  return ret;
}

int ObSqlPlanSet::get_plan_type(const ObIArray<ObTableLocation> &table_locations,
                                const bool is_contain_uncertain_op,
                                ObPlanCacheCtx &pc_ctx,
                                ObIArray<ObCandiTableLoc> &candi_table_locs,
                                ObPhyPlanType &plan_type)
{
  int ret = OB_SUCCESS;
  candi_table_locs.reuse();

  if (OB_FAIL(get_phy_locations(table_locations,
                                pc_ctx,
                                candi_table_locs))) {
  } else if (OB_FAIL(calc_phy_plan_type_v2(pc_ctx,
                                           plan_type))) {
  } else {
    // Lookup operators support pushdown into distributed execution.
    // Select's sql if contains uncertain operator, cannot change type to distributed plan
    if (is_contain_uncertain_op && plan_type != OB_PHY_PLAN_LOCAL
        && stmt::T_SELECT != stmt_type_) {
      plan_type = OB_PHY_PLAN_DISTRIBUTED;
    }
  }

  return ret;
}

ObPhysicalPlan *ObSqlPlanSet::get_local_plan()
{
  return direct_local_plan_;
}


int ObSqlPlanSet::add_local_plan(ObPhysicalPlan &plan)
{
  int ret = OB_SUCCESS;
  if (local_plans_.count() != 0) {
    ret = OB_SQL_PC_PLAN_DUPLICATE;
  } else {
    direct_local_plan_ = &plan;
    if (OB_FAIL(local_plans_.push_back(&plan))) {
      direct_local_plan_ = nullptr;
      LOG_WARN("failed to add local plan", K(ret));
    }
  }
  return ret;
}


int64_t ObSqlPlanSet::get_local_plan_mem_size()
{
  int64_t mem_size = 0;
  for (int64_t i = 0; i < local_plans_.count(); ++i) {
    if (nullptr != local_plans_.at(i)) {
      mem_size += local_plans_.at(i)->get_mem_size();
    }
  }
  return mem_size;
}


bool ObSqlPlanSet::is_sql_planset()
{
  return true;
}

}

bool ObPlanSet::match_decint_precision(const ObParamInfo &param_info, ObPrecision other_prec) const
{
  bool ret = false;
  if (ob_is_decimal_int(param_info.type_) || ob_is_integer_type(param_info.type_)) {
    ret = (param_info.precision_ == other_prec);
  } else if (ob_is_extend(param_info.type_) && ob_is_decimal_int(param_info.ext_real_type_)) {
    ret = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(param_info.precision_)
          == wide::ObDecimalIntConstValue::get_int_bytes_by_precision(other_prec);
  } else {
    // not decimal_int, return true
    ret = true;
  }
  return ret;
}

}
