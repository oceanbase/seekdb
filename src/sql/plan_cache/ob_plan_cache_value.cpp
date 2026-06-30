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
#include "ob_plan_cache_value.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/plan_cache/ob_pcv_set.h"
#include "share/resource_manager/ob_resource_manager.h"
#include "sql/plan_cache/ob_values_table_compression.h"

using namespace oceanbase::share::schema;
using namespace oceanbase::common;
using namespace oceanbase::pl;

namespace oceanbase
{
namespace sql
{

int PCVSchemaObj::init(const ObTableSchema *schema)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(schema) || OB_ISNULL(inner_alloc_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpected null argument", K(ret), K(schema), K(inner_alloc_));
  } else {
    database_id_ = schema->get_database_id();
    schema_id_ = schema->get_table_id();
    schema_version_ = schema->get_schema_version();
    schema_type_ = TABLE_SCHEMA;
    table_type_ = schema->get_table_type();
    is_tmp_table_ = schema->is_tmp_table();
    is_mv_container_table_ = schema->mv_container_table();
    // copy table name
    char *buf = nullptr;
    const ObString &tname = schema->get_table_name_str();
    if (nullptr == (buf = static_cast<char *>(inner_alloc_->alloc(tname.length())))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret), K(tname.length()));
    } else {
      MEMCPY(buf, tname.ptr(), tname.length());
      table_name_.assign_ptr(buf, tname.length());
    }
  }
  return ret;
}

bool PCVSchemaObj::operator==(const PCVSchemaObj &other) const
{
  bool ret = true;
  if (schema_type_ != other.schema_type_) {
    ret = false;
  } else if (TABLE_SCHEMA == other.schema_type_) {
    ret = true &&
          database_id_ == other.database_id_ &&
          schema_id_ == other.schema_id_ &&
          schema_version_ == other.schema_version_ &&
          table_type_ == other.table_type_;
  } else {
    ret = schema_id_ == other.schema_id_ &&
          schema_version_ == other.schema_version_;
  }
  return ret;
}

int PCVSchemaObj::init_without_copy_name(const ObSimpleTableSchemaV2 *schema)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unexpected null argument", K(ret), K(schema));
  } else {
    database_id_ = schema->get_database_id();
    schema_id_ = schema->get_table_id();
    schema_version_ = schema->get_schema_version();
    schema_type_ = TABLE_SCHEMA;
    table_type_ = schema->get_table_type();
    is_tmp_table_ = schema->is_tmp_table();

    table_name_ = schema->get_table_name_str();
  }
  return ret;
}

int PCVSchemaObj::init_with_version_obj(const ObSchemaObjVersion &schema_obj_version)
{
  int ret = OB_SUCCESS;
  schema_type_ = schema_obj_version.get_schema_type();
  schema_id_ = schema_obj_version.object_id_;
  schema_version_ = schema_obj_version.version_;
  return ret;
}

void PCVSchemaObj::reset()
{
  database_id_ = common::OB_INVALID_ID;
  schema_id_ = common::OB_INVALID_ID;
  schema_type_ = OB_MAX_SCHEMA;
  schema_version_ = 0;
  table_type_ = MAX_TABLE_TYPE;
  is_tmp_table_ = false;
  if (inner_alloc_ != nullptr && table_name_.ptr() != nullptr) {
    inner_alloc_->free(table_name_.ptr());
    table_name_.reset();
    inner_alloc_ = nullptr;
  }
}

PCVSchemaObj::~PCVSchemaObj()
{
  reset();
}

ObPlanCacheValue::ObPlanCacheValue()
  : pcv_set_(NULL),
    pc_alloc_(NULL),
    last_plan_id_(OB_INVALID_ID),
    //use_global_location_cache_(true),
    tenant_schema_version_(OB_INVALID_VERSION),
    sys_schema_version_(OB_INVALID_VERSION),
    outline_state_(),
    outline_params_wrapper_(),
    sessid_(OB_INVALID_ID),
    sess_create_time_(0),
    contain_sys_name_table_(false),
    need_param_(true),
    is_nested_sql_(false),
    is_batch_execute_(false),
    has_dynamic_values_table_(false),
    stored_schema_objs_(pc_alloc_),
    stmt_type_(stmt::T_MAX),
    enable_rich_vector_format_(false),
    switchover_epoch_(OB_INVALID_VERSION)
{
  MEMSET(sql_id_, 0, sizeof(sql_id_));
  MEMSET(format_sql_id_, 0, sizeof(format_sql_id_));
  not_param_index_.set_attr(ObMemAttr("NotParamIdex"));
  neg_param_index_.set_attr(ObMemAttr("NegParamIdex"));
  must_be_positive_idx_.set_attr(ObMemAttr("MustBePosiIdx"));
  not_param_info_.set_attr(ObMemAttr("NotParamInfo"));
  not_param_var_.set_attr(ObMemAttr("NotParamVar"));
  param_charset_type_.set_attr(ObMemAttr("ParamCharsType"));
  fmt_int_or_ch_decint_idx_.set_attr(ObMemAttr("FMTIntPrecIdx"));
}

int ObPlanCacheValue::init(ObPCVSet *pcv_set, const ObILibCacheObject *cache_obj, ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  ObMemAttr mem_attr;
  const ObPlanCacheObject *plan = static_cast<const ObPlanCacheObject*>(cache_obj);
  if (OB_ISNULL(pcv_set) || OB_ISNULL(plan) || OB_ISNULL(pcv_set->get_plan_cache())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(pcv_set), K(plan));
  } else if (FALSE_IT(mem_attr = pcv_set->get_plan_cache()->get_mem_attr())) {
      // do nothing
  } else if (OB_ISNULL(pc_alloc_ = pcv_set->get_allocator())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(pcv_set->get_allocator()));
  } else if (OB_ISNULL(pc_malloc_ = pcv_set->get_pc_allocator())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(pcv_set->get_pc_allocator()));
  } else if (OB_FAIL(outline_params_wrapper_.set_allocator(pc_alloc_, mem_attr))) {
  } else if (OB_NOT_NULL(pc_ctx.exec_ctx_.get_outline_params_wrapper())) {
    if (OB_FAIL(set_outline_params_wrapper(*pc_ctx.exec_ctx_.get_outline_params_wrapper()))) {
    }
  } else if (OB_ISNULL(pc_ctx.sql_ctx_.session_info_) ||
             OB_ISNULL(pc_ctx.sql_ctx_.schema_guard_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
	     K(ret),
	     K(pc_ctx.sql_ctx_.session_info_),
	     K(pc_ctx.sql_ctx_.schema_guard_));
  }

  if (OB_SUCC(ret)) {
    switchover_epoch_ = MTL_GET_SWITCHOVER_EPOCH();
    pcv_set_ = pcv_set;
    //use_global_location_cache_ = !cache_obj->is_contain_virtual_table();
    outline_state_ = plan->get_outline_state();
    sys_schema_version_ = plan->get_sys_schema_version();
    tenant_schema_version_ = plan->get_tenant_schema_version();
    sql_traits_ = pc_ctx.sql_traits_;
    enable_rich_vector_format_ = static_cast<const ObPhysicalPlan *>(plan)->get_use_rich_format();
    stmt_type_ = plan->get_stmt_type();
    need_param_ = plan->need_param();
    is_nested_sql_ = ObSQLUtils::is_nested_sql(&pc_ctx.exec_ctx_);
    is_batch_execute_ = pc_ctx.sql_ctx_.is_batch_params_execute();
    has_dynamic_values_table_ = pc_ctx.exec_ctx_.has_dynamic_values_table();
    MEMCPY(sql_id_, pc_ctx.sql_ctx_.sql_id_, sizeof(pc_ctx.sql_ctx_.sql_id_));
    MEMCPY(format_sql_id_, pc_ctx.sql_ctx_.format_sql_id_, sizeof(pc_ctx.sql_ctx_.format_sql_id_));
    if (OB_FAIL(not_param_index_.add_members2(pc_ctx.not_param_index_))) {
    } else if (OB_FAIL(neg_param_index_.add_members2(pc_ctx.neg_param_index_))) {
    } else if (OB_FAIL(param_charset_type_.assign(pc_ctx.param_charset_type_))) {
    } else if (OB_FAIL(must_be_positive_idx_.add_members2(pc_ctx.must_be_positive_index_))) {
    } else if (OB_FAIL(fmt_int_or_ch_decint_idx_.add_members2(pc_ctx.fmt_int_or_ch_decint_idx_))) {
    } else if (OB_FAIL(set_stored_schema_objs(plan->get_dependency_table(),
                                              pc_ctx.sql_ctx_.schema_guard_))) {
    } else {
      //deep copy special param raw text
      if (PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_) {
        if (OB_FAIL(not_param_var_.assign(pc_ctx.not_param_var_))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < not_param_var_.count(); ++i) {
          if (OB_FAIL(deep_copy_obj(*pc_alloc_,
                                    not_param_var_.at(i).ps_param_,
                                    not_param_var_.at(i).ps_param_))) {
          }
        }
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < pc_ctx.not_param_info_.count(); i++) {
          if (OB_FAIL(not_param_info_.push_back(pc_ctx.not_param_info_.at(i)))) {
          } else if (OB_FAIL(ob_write_string(*pc_alloc_,
                                             not_param_info_.at(i).raw_text_,
                                             not_param_info_.at(i).raw_text_))) {
          }
        } //for end
        if (OB_SUCC(ret)) {
          lib::ob_sort(not_param_info_.begin(), not_param_info_.end(),
              [](NotParamInfo &l, NotParamInfo &r) { return (l.idx_  < r.idx_); });
        }
      }
      //deep copy constructed sql
      if (OB_SUCC(ret)) {
        ObString outline_signature_str;
        ObString outline_format_signature_str;
        if (PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_) {
          outline_signature_str = pc_ctx.raw_sql_;
          outline_format_signature_.reset();
        } else {
          outline_signature_str = pc_ctx.sql_ctx_.bl_key_.constructed_sql_;
          outline_format_signature_str = pc_ctx.sql_ctx_.bl_key_.format_sql_;
        }
        int64_t size1 = outline_signature_str.get_serialize_size();
        int64_t size2 = outline_format_signature_str.get_serialize_size();
        if (0 == size1 || 0 == size2) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          char *buf1 = NULL;
          char *buf2 = NULL;
          int64_t pos_s1 = 0;
          int64_t pos_s2 = 0;
          if (OB_UNLIKELY(NULL == (buf1 = (char *)pc_alloc_->alloc(size1)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to alloc mem", K(ret));
          } else if (OB_UNLIKELY(NULL == (buf2 = (char *)pc_alloc_->alloc(size2)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("fail to alloc mem", K(ret));
          } else if (OB_FAIL(outline_signature_str.serialize(buf1, size1, pos_s1))) {
          } else if (OB_FAIL(outline_format_signature_str.serialize(buf2, size2, pos_s2))) {
          } else {
            outline_signature_.assign_ptr(buf1, static_cast<ObString::obstr_size_t>(pos_s1));
            outline_format_signature_.assign_ptr(buf2, static_cast<ObString::obstr_size_t>(pos_s2));
          }
        }
      }
      // deep copy constructed_sql_, used for baseline;
      if (OB_SUCC(ret)) {
        if ((PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_)
             && OB_FAIL(ob_write_string(*pc_alloc_, pc_ctx.raw_sql_, constructed_sql_))) {
          LOG_WARN("fail to deep copy param raw text", K(ret));
        } else if (!(PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_)
                   && OB_FAIL(ob_write_string(*pc_alloc_,
                                              pc_ctx.sql_ctx_.bl_key_.constructed_sql_,
                                              constructed_sql_))) {
          LOG_WARN("failed to write string", K(ret));
        }
      }
      // deep copy raw_sql if not need to param
      if (OB_SUCC(ret) && !need_param_ &&
              OB_FAIL(ob_write_string(*pc_alloc_, pc_ctx.raw_sql_, raw_sql_))) {
        LOG_WARN("failed to write raw sql", K(ret));
      }

      // set sessid_ if necessary
      if (OB_SUCC(ret)) {
        if (is_contain_tmp_tbl()) {
          // Temporary table behavior depends on the session created by the user, and for remote execution, the remote session id is a temporary session_id
          // Therefore here we should uniformly use master session id, to ensure that the matching plan always uses the user session
          sessid_ = pc_ctx.sql_ctx_.session_info_->get_sid();
          sess_create_time_ = pc_ctx.sql_ctx_.session_info_->get_sess_create_time();
          // Get the table name of the temporary table
          pc_ctx.tmp_table_names_.reset();
          if (OB_FAIL(get_tmp_depend_tbl_names(pc_ctx.tmp_table_names_))) {
          } else {
            // do nothing
          }
        } else { /* do nothing */ }
      }
    }

  }

  return ret;
}

int ObPlanCacheValue::match_all_params_info(ObPlanSet *batch_plan_set,
                                            ObPlanCacheCtx &pc_ctx,
                                            int64_t outline_param_idx,
                                            bool &is_same)
{
  int ret = OB_SUCCESS;
  is_same = true;
  bool is_batched_multi_stmt = pc_ctx.sql_ctx_.is_batch_params_execute();
  ParamStore *params = pc_ctx.fp_result_.cache_params_;
  if (is_batched_multi_stmt) {
    ObArenaAllocator tmp_alloc;
    ParamStore param_store((ObWrapperAllocator(tmp_alloc)));
    int64_t query_cnt = pc_ctx.sql_ctx_.get_batch_params_count();
    ParamStore *ab_params = pc_ctx.ab_params_;
    ObPhysicalPlanCtx *phy_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
    if (OB_ISNULL(ab_params) || OB_ISNULL(phy_ctx)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null ptr unexpected", K(ret), K(phy_ctx), K(ab_params));
    } else if (query_cnt <= 1) {
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      LOG_TRACE("unexpected query count", K(ret), K(query_cnt));
    } else if (batch_plan_set->get_can_skip_params_match()) {
      // can_skip_params_match_ is true, we not need match all params,
      // only call match_params_info once
      param_store.reuse();
      phy_ctx->reset_datum_param_store();
      phy_ctx->set_original_param_cnt(ab_params->count());
      if (OB_FAIL(get_one_group_params(0, *ab_params, param_store))) {
      } else if (OB_FAIL(pc_ctx.fp_result_.cache_params_->assign(param_store))) {
      } else if (OB_FAIL(batch_plan_set->match_params_info(params, pc_ctx, outline_param_idx, is_same))) {
      } else {
        // not match this plan, try match next plan
      }
    } else {
      // pc_ctx.fp_result_.cache_params_  In batch scenarios, cache_params_ should not be null
      // When the first set of parameters cannot match the plan_set parameter requirements,
      // -5787 cannot be returned and the current batch optimization cannot be rolled back.
      for (int64_t i = 0; OB_SUCC(ret) && is_same && i < query_cnt; ++i) {
        param_store.reuse();
        phy_ctx->reset_datum_param_store();
        phy_ctx->set_original_param_cnt(ab_params->count());
        if (OB_FAIL(get_one_group_params(i, *ab_params, param_store))) {
        } else if (OB_FAIL(pc_ctx.fp_result_.cache_params_->assign(param_store))) {
        } else if (OB_FAIL(phy_ctx->init_datum_param_store())) {
        } else if (OB_FAIL(batch_plan_set->match_params_info(params, pc_ctx, outline_param_idx, is_same))) {
        } else if (i != 0 && !is_same) {
          ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
          LOG_TRACE("params is not same type", K(param_store), K(i));
        }
      }
    }

    if (OB_SUCC(ret) && is_same) {
      phy_ctx->reset_datum_param_store();
      phy_ctx->set_original_param_cnt(ab_params->count());
      if (OB_FAIL(batch_plan_set->copy_param_flag_from_param_info(ab_params))) {
      } else if (OB_FAIL(phy_ctx->get_param_store_for_update().assign(*ab_params))) {
      } else if (OB_FAIL(phy_ctx->init_datum_param_store())) {
      }
    }
  } else {
    if (OB_FAIL(batch_plan_set->match_params_info(params, pc_ctx, outline_param_idx, is_same))) {
    }
  }
  return ret;
}

int ObPlanCacheValue::choose_plan(ObPlanCacheCtx &pc_ctx,
                                  const ObIArray<PCVSchemaObj> &schema_array,
                                  ObPlanCacheObject *&plan_out)
{
  int ret = OB_SUCCESS;
  bool is_old_version = false;
  plan_out = NULL;
  ObSQLSessionInfo *session = NULL;
  
  ObPlanCacheObject *plan = NULL;
  int64_t outline_param_idx = 0;
  // Check the version of the view and table involved in this SQL cached in pcv,
  // If not the latest, in the plan cache layer this value will be deleted and the plan will be re-added
  //TODO shengle This copy needs to be handled somehow
  bool need_check_schema = (schema_array.count() != 0);
  int64_t new_switchover_epoch = MTL_GET_SWITCHOVER_EPOCH();
  if (schema_array.count() == 0 && stored_schema_objs_.count() == 0) {
    need_check_schema = true;
  }
  ObBasicSessionInfo::ForceRichFormatStatus orig_rich_format_status = ObBasicSessionInfo::ForceRichFormatStatus::Disable;
  bool orig_phy_ctx_rich_format = false;
  if (stmt::T_NONE == pc_ctx.sql_ctx_.stmt_type_) {
    //sql_ctx_.stmt_type_ != stmt::T_NONE means this calling in nested sql,
    //can't cover the first stmt type in sql context
    pc_ctx.sql_ctx_.stmt_type_ = stmt_type_;
  }
  if (OB_ISNULL(session = pc_ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    SQL_PC_LOG(ERROR, "got session is NULL", K(ret));
  } else if (FALSE_IT(orig_rich_format_status = session->get_force_rich_format_status())) {
  } else if (FALSE_IT(session->set_stmt_type(stmt_type_))) {
  } else if (OB_UNLIKELY(switchover_epoch_ != new_switchover_epoch)) {
    ret = OB_OLD_SCHEMA_VERSION;
    switchover_epoch_ = new_switchover_epoch;
    SQL_PC_LOG(TRACE, "switchover_epoch changed, view or table is old version", KR(ret));
  } else if (OB_FAIL(check_value_version_for_get(pc_ctx.sql_ctx_.schema_guard_,
                                                 need_check_schema,
                                                 schema_array,
                                                 is_old_version))) {
  } else if (true == is_old_version) {
    ret = OB_OLD_SCHEMA_VERSION;
    SQL_PC_LOG(TRACE, "view or table is old version", K(ret));
  }
  if (OB_FAIL(ret)) {
    //do nothing
  } else {
    ParamStore *params = pc_ctx.fp_result_.cache_params_;
    //init param store
    if (pc_ctx.try_get_plan_) {
      // do nothing
    } else if (OB_LIKELY(pc_ctx.sql_ctx_.is_batch_params_execute())) {
      if (OB_FAIL(resolve_multi_stmt_params(pc_ctx))) {
        if (OB_BATCHED_MULTI_STMT_ROLLBACK != ret) {
          LOG_WARN("failed to resolver row params", K(ret));
        }
      }
    } else if (OB_UNLIKELY(pc_ctx.exec_ctx_.has_dynamic_values_table())) {
      if (OB_FAIL(ObValuesTableCompression::resolve_params_for_values_clause(pc_ctx, stmt_type_,
                  not_param_info_, param_charset_type_, neg_param_index_, not_param_index_,
                  must_be_positive_idx_, fmt_int_or_ch_decint_idx_, params))) {
      }
    } else if (OB_FAIL(resolver_params(pc_ctx,
                                       stmt_type_,
                                       param_charset_type_,
                                       neg_param_index_,
                                       not_param_index_,
                                       must_be_positive_idx_,
                                       fmt_int_or_ch_decint_idx_,
                                       pc_ctx.fp_result_.raw_params_,
                                       params))) {
    }
    if (OB_SUCC(ret)) {
      ObPhysicalPlanCtx *phy_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
      if (NULL != phy_ctx) {
        orig_phy_ctx_rich_format = phy_ctx->is_rich_format();
        phy_ctx->set_original_param_cnt(phy_ctx->get_param_store().count());
        phy_ctx->set_rich_format(enable_rich_vector_format_);
        session->set_force_rich_format(enable_rich_vector_format_ ?
                                         ObBasicSessionInfo::ForceRichFormatStatus::FORCE_ON :
                                         ObBasicSessionInfo::ForceRichFormatStatus::FORCE_OFF);
        if (!pc_ctx.try_get_plan_ && OB_FAIL(phy_ctx->init_datum_param_store())) {
          LOG_WARN("fail to init datum param store", K(ret));
        }
      }
    }
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_FAIL(get_outline_param_index(pc_ctx.exec_ctx_,
                                               outline_param_idx))) {
    } else {
      // Here record org_param_count, used at the end of the matching plan, if no match is successful,
      // Then restore the parameters in param store to the current state (plan_set will perform the calculation table match)
      // Expression calculation and add the result to param store) Avoid this failure from causing a retry by the upper layer,
      // param store parameters increase causing error when rematching due to unexpected number of parameters
      int64_t org_param_count = 0;
      if (OB_NOT_NULL(params)) {
        org_param_count = params->count();
      }

      DLIST_FOREACH(plan_set, plan_sets_) {
        plan = NULL;
        bool is_same = false;
        if (OB_FAIL(match_all_params_info(plan_set, pc_ctx, outline_param_idx, is_same))) {
        } else if (!is_same) {        //do nothing
          LOG_TRACE("params info does not match", KPC(params));
        } else {
          if (OB_FAIL(plan_set->select_plan(pc_ctx, plan))) {
            if (OB_SQL_PC_NOT_EXIST == ret) {
              ret = OB_SUCCESS;
            } else {
              SQL_PC_LOG(TRACE, "failed to select plan in plan set", K(ret));
            }
          } else if (NULL != params) {
            if (pc_ctx.sql_ctx_.enable_sql_resource_manage_) {
              uint64_t rule_id = plan_set->resource_map_rule_.get_res_map_rule_id();
              int64_t param_idx = plan_set->resource_map_rule_.get_res_map_rule_param_idx();
              if (plan_set->resource_map_rule_.use_hint_control_resource()
                  || (rule_id != OB_INVALID_ID && param_idx != OB_INVALID_INDEX)) {
                uint64_t final_choosed_group_id = OB_INVALID_ID;
                // 1. check hint first
                if (plan_set->resource_map_rule_.use_hint_control_resource()) {
                } else {
                  // 2. check col res map rule
                  
                  ObString param_text;
                  ObCollationType cs_type = CS_TYPE_INVALID;
                  if (OB_UNLIKELY(param_idx < 0 || param_idx >= params->count())) {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_ERROR("unexpected res map rule param idx", K(ret), K(rule_id), K(param_idx),
                              K(params->count()));
                  } else if (OB_FAIL(session->get_collation_connection(cs_type))) {
                  } else if (OB_FAIL(ObObjCaster::get_obj_param_text(
                               params->at(param_idx), pc_ctx.raw_sql_, pc_ctx.allocator_, cs_type,
                               param_text))) {
                  } else {
                    final_choosed_group_id = 0;
                  }
                }
                // 3.use default resource group if not match any resource group
                // OB_INVALID_ID means current neither
                // resource group specified by hint
                // nor
                // user+param_value column rule
                // is in used
                // get group_id according to current user.
                if (OB_SUCC(ret) && OB_INVALID_ID == final_choosed_group_id) {
                  final_choosed_group_id = 0;
                }
                if (OB_SUCC(ret)) {
                  if (final_choosed_group_id == THIS_WORKER.get_group_id()) {
                    // do nothing if equals to current group id.
                  } else if (session->get_is_in_retry()
                             && OB_NEED_SWITCH_CONSUMER_GROUP
                                  == session->get_retry_info().get_last_query_retry_err()) {
                    LOG_ERROR(
                      "use unexpected group when retry, maybe set packet retry failed before",
                      K(final_choosed_group_id), K(THIS_WORKER.get_group_id()),
                      K(plan_set->resource_map_rule_));
                  } else {
                    session->set_expect_group_id(final_choosed_group_id);
                    ret = OB_NEED_SWITCH_CONSUMER_GROUP;
                  }
                  LOG_TRACE("get expect rule id", K(ret), K(final_choosed_group_id),
                            K(THIS_WORKER.get_group_id()), K(session->get_expect_group_id()),
                            K(pc_ctx.raw_sql_));
                }
              }
            }
            break; // this place is recommended to keep, if removed, additional markers need to be added in for() to judge, and the macro above the for loop should not be used;
          }
        }
        if (NULL == plan
            && OB_NOT_NULL(params)
            && (params->count() > org_param_count)) {
          ObPhysicalPlanCtx * phy_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
          if (NULL == phy_ctx) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("physical ctx is null", K(ret));
          }
          for (int64_t i = params->count(); OB_SUCC(ret) && i > org_param_count; --i) {
            params->pop_back();
            phy_ctx->get_datum_param_store().pop_back();
            // todo: param_frame_ptrs_ and other params in physical_plan_ctx need be reset
          }
        }
      } // end for
    }
  }

  if (NULL == plan && OB_SUCC(ret)) {
    ret = OB_SQL_PC_NOT_EXIST;
  }

  if (OB_SUCC(ret)) {
    plan_out = plan;
    pc_ctx.sql_traits_ = sql_traits_; //used for check read only
  }
  // reset force rich format status
  if (NULL == plan) {
    if (session != nullptr) {
      session->set_force_rich_format(orig_rich_format_status);
    }
    if (pc_ctx.exec_ctx_.get_physical_plan_ctx() != nullptr) {
      pc_ctx.exec_ctx_.get_physical_plan_ctx()->set_rich_format(orig_phy_ctx_rich_format);
    }
  }
  return ret;
}
// Convert parameterized parameters of fast parser to ObObjParam, used for get plan
int ObPlanCacheValue::resolver_params(ObPlanCacheCtx &pc_ctx,
                                      const stmt::StmtType stmt_type,
                                      const ObIArray<ObCharsetType> &param_charset_type,
                                      const ObBitSet<> &neg_param_index,
                                      const ObBitSet<> &not_param_index,
                                      const ObBitSet<> &must_be_positive_idx,
                                      const ObBitSet<> &fmt_int_or_ch_decint_idx,
                                      ObIArray<ObPCParam *> &raw_params,
                                      ParamStore *obj_params)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = pc_ctx.exec_ctx_.get_my_session();
  ObPhysicalPlanCtx *phy_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
  const int64_t raw_param_cnt = raw_params.count();
  ObObjParam value;
  bool enable_decimal_int = false;
  bool enable_mysql_compatible_dates = false;
  if (OB_ISNULL(session) || OB_ISNULL(phy_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_PC_LOG(WARN, "invalid argument", K(ret), KP(session), KP(phy_ctx));
  } else if (obj_params == NULL || PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_) {
    /* do nothing */
  } else if (OB_UNLIKELY(raw_param_cnt != param_charset_type.count())) {
    ret = OB_INVALID_ARGUMENT;
    SQL_PC_LOG(WARN, "raw_params and param_charset_type count is different", K(ret),
               K(raw_param_cnt), K(param_charset_type.count()), K(pc_ctx.raw_sql_));
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session, enable_decimal_int))) {
   } else if (OB_FAIL(ObSQLUtils::check_enable_mysql_compatible_dates(session, false,
                          enable_mysql_compatible_dates))) {
  } else {
    CHECK_COMPATIBILITY_MODE(session);
    ObCollationType collation_connection = static_cast<ObCollationType>(session->get_local_collation_connection());
    (void)obj_params->reserve(raw_param_cnt);
    for (int64_t i = 0; OB_SUCC(ret) && i < raw_param_cnt; i++) {
      bool is_param = false;
      if (OB_FAIL(ObResolverUtils::resolver_param(pc_ctx, *session, phy_ctx->get_param_store_for_update(), stmt_type,
                  param_charset_type.at(i), neg_param_index, not_param_index, must_be_positive_idx,
                  fmt_int_or_ch_decint_idx, raw_params.at(i), i, enable_mysql_compatible_dates,
                  value, is_param, enable_decimal_int))) {
      } else if (is_param && OB_FAIL(obj_params->push_back(value))) {
        SQL_PC_LOG(WARN, "fail to push item to array", K(ret));
      } else {/* do nothing */}
    }
  }
  return ret;
}

int ObPlanCacheValue::before_resolve_array_params(ObPlanCacheCtx &pc_ctx, int64_t query_num, int64_t param_num, ParamStore *&ab_params)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ab_params = static_cast<ParamStore *>(pc_ctx.allocator_.alloc(sizeof(ParamStore))))) {
     ret = OB_ALLOCATE_MEMORY_FAILED;
     LOG_WARN("failed to allocate memory", K(ret));
  } else if (FALSE_IT(ab_params = new(ab_params)ParamStore(ObWrapperAllocator(pc_ctx.allocator_)))) {
   // do nothing
  } else if (OB_FAIL(ObSQLUtils::create_multi_stmt_param_store(pc_ctx.allocator_,
                                                               query_num,
                                                               param_num,
                                                               *ab_params))) {
  }
  return ret;
}

int ObPlanCacheValue::resolve_multi_stmt_params(ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  ParamStore *ab_params = NULL;
  bool is_valid = true;
  if (pc_ctx.sql_ctx_.is_do_insert_batch_opt()) {
    int64_t query_num = pc_ctx.sql_ctx_.get_insert_batch_row_cnt();
    int64_t param_num = pc_ctx.fp_result_.raw_params_.count() - not_param_info_.count();
    if (!not_param_info_.empty() &&
        OB_FAIL(check_insert_multi_values_param(pc_ctx, is_valid))) {
      LOG_WARN("failed to check multi insert param value", K(ret));
    } else if (!is_valid) {
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      LOG_TRACE("batched multi_stmt needs rollback", K(ret));
    } else if (OB_FAIL(before_resolve_array_params(pc_ctx, query_num, param_num, ab_params))) {
    } else if (OB_ISNULL(ab_params)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else if (resolve_insert_multi_values_param(pc_ctx,
                                                 stmt_type_,
                                                 param_charset_type_,
                                                 neg_param_index_,
                                                 not_param_index_,
                                                 must_be_positive_idx_,
                                                 fmt_int_or_ch_decint_idx_,
                                                 param_num,
                                                 *ab_params)) {

    } else {
      pc_ctx.ab_params_ = ab_params;
    }
  } else if (!pc_ctx.sql_ctx_.multi_stmt_item_.is_ab_batch_opt()) {
    int64_t query_num = pc_ctx.multi_stmt_fp_results_.count();
    int64_t param_num = pc_ctx.fp_result_.raw_params_.count() - not_param_info_.count();
    // check whether all the values are the same
    // 1、Create param_store pointer
    if (!not_param_info_.empty() &&
        OB_FAIL(check_multi_stmt_not_param_value(pc_ctx.multi_stmt_fp_results_,
                                                 not_param_info_,
                                                 is_valid))) {
      LOG_WARN("failed to check multi stmt not param value", K(ret));
    } else if (!is_valid) {
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      LOG_TRACE("batched multi_stmt needs rollback", K(ret));
    } else if (OB_FAIL(before_resolve_array_params(pc_ctx, query_num, param_num, ab_params))) {
    } else if (OB_ISNULL(ab_params)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else if (OB_FAIL(check_multi_stmt_param_type(pc_ctx,
                                                   stmt_type_,
                                                   param_charset_type_,
                                                   neg_param_index_,
                                                   not_param_index_,
                                                   must_be_positive_idx_,
                                                   fmt_int_or_ch_decint_idx_,
                                                   *ab_params))) {
    } else {
      pc_ctx.ab_params_ = ab_params;
    }
  }

  return ret;
}

int ObPlanCacheValue::resolve_insert_multi_values_param(ObPlanCacheCtx &pc_ctx,
                                                        const stmt::StmtType stmt_type,
                                                        const ObIArray<ObCharsetType> &param_charset_type,
                                                        const ObBitSet<> &neg_param_index,
                                                        const ObBitSet<> &not_param_index,
                                                        const ObBitSet<> &must_be_positive_idx,
                                                        const ObBitSet<> &fmt_int_or_ch_decint_idx,
                                                        int64_t params_num,
                                                        ParamStore &param_store)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc;
  ObRawParams *raw_param_array = nullptr;
  ParamStore temp_obj_params((ObWrapperAllocator(tmp_alloc)));
  ParamStore first_obj_params((ObWrapperAllocator(tmp_alloc)));
  int64_t query_num = pc_ctx.sql_ctx_.get_insert_batch_row_cnt();
  for (int64_t i = 0; OB_SUCC(ret) && i < query_num; i++) {
    raw_param_array = nullptr;
    temp_obj_params.reuse();
    if (OB_ISNULL(raw_param_array = pc_ctx.insert_batch_opt_info_.multi_raw_params_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else if (OB_FAIL(resolver_params(pc_ctx,
                                       stmt_type,
                                       param_charset_type,
                                       neg_param_index,
                                       not_param_index,
                                       must_be_positive_idx,
                                       fmt_int_or_ch_decint_idx,
                                       *raw_param_array,
                                       &temp_obj_params))) {
    } else {
      LOG_DEBUG("print one insert temp_obj_params", 
          K(temp_obj_params), K(params_num), K(query_num), K(pc_ctx.not_param_info_));
    }

    if (OB_SUCC(ret) && i == 0) {
      if (OB_FAIL(first_obj_params.assign(temp_obj_params))) {
      }
      // set type and external type
      for (int64_t j = 0; OB_SUCC(ret) && j < params_num; j++) {
        ObSqlArrayObj *array_params = nullptr;
        if (OB_UNLIKELY(!param_store.at(j).is_ext_sql_array())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("param store object is invalid", K(ret), K(param_store.at(j)));
        } else if (OB_ISNULL(array_params = reinterpret_cast<ObSqlArrayObj*>(param_store.at(j).get_ext()))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected null", K(ret));
        } else {
          array_params->element_.set_meta_type(temp_obj_params.at(j).get_meta());
          array_params->element_.set_accuracy(temp_obj_params.at(j).get_accuracy());
        }
      } // end init accuracy
    }

    // copy data
    for (int64_t j = 0; OB_SUCC(ret) && j < params_num; j++) {
      ObSqlArrayObj *array_params = nullptr;
      if (OB_UNLIKELY(!param_store.at(j).is_ext_sql_array())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param object is invalid", K(ret), K(param_store.at(j)));
      } else if (OB_ISNULL(array_params =
          reinterpret_cast<ObSqlArrayObj*>(param_store.at(j).get_ext()))
          || OB_ISNULL(array_params->data_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), KPC(array_params));
      } else {
        array_params->data_[i] = temp_obj_params.at(j);
      }
    } // end copy
  }
  return ret;
}

int ObPlanCacheValue::check_multi_stmt_param_type(ObPlanCacheCtx &pc_ctx,
                                                  stmt::StmtType stmt_type,
                                                  const ObIArray<ObCharsetType> &param_charset_type,
                                                  const ObBitSet<> &neg_param_index,
                                                  const ObBitSet<> &not_param_index,
                                                  const ObBitSet<> &must_be_positive_idx,
                                                  const ObBitSet<> &fmt_int_or_ch_decint_idx,
                                                  ParamStore &param_store)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc;
  ParamStore temp_obj_params((ObWrapperAllocator(tmp_alloc)));
  ParamStore first_obj_params((ObWrapperAllocator(tmp_alloc)));
  int64_t query_num = pc_ctx.multi_stmt_fp_results_.count();
  int64_t param_num = pc_ctx.fp_result_.raw_params_.count() - not_param_index.num_members();

  for (int64_t i = 0; OB_SUCC(ret) && i < query_num; i++) {
    temp_obj_params.reuse();
    if (OB_FAIL(resolver_params(pc_ctx,
                                stmt_type,
                                param_charset_type,
                                neg_param_index,
                                not_param_index,
                                must_be_positive_idx,
                                fmt_int_or_ch_decint_idx,
                                pc_ctx.multi_stmt_fp_results_.at(i).raw_params_,
                                &temp_obj_params))) {
    } else if (i == 0) {
      ret = first_obj_params.assign(temp_obj_params);
      // set type and external type
      for (int64_t j = 0; OB_SUCC(ret) && j < param_num; j++) {
        ObSqlArrayObj *array_params = nullptr;
        if (OB_UNLIKELY(!param_store.at(j).is_ext_sql_array())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("param store object is invalid", K(ret), K(param_store.at(j)));
        } else if (OB_ISNULL(array_params = reinterpret_cast<ObSqlArrayObj*>(param_store.at(j).get_ext()))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected null", K(ret));
        } else {
          array_params->element_.set_meta_type(temp_obj_params.at(j).get_meta());
          array_params->element_.set_accuracy(temp_obj_params.at(j).get_accuracy());
        }
      }
    }
    // copy data
    for (int64_t j = 0; OB_SUCC(ret) && j < param_num; j++) {
      ObSqlArrayObj *array_params = nullptr;
      if (OB_UNLIKELY(!param_store.at(j).is_ext_sql_array())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param object is invalid", K(ret), K(param_store.at(j)));
      } else if (OB_ISNULL(array_params =
          reinterpret_cast<ObSqlArrayObj*>(param_store.at(j).get_ext()))
          || OB_ISNULL(array_params->data_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected null", K(ret), KPC(array_params));
      } else {
        array_params->data_[i] = temp_obj_params.at(j);
      }
    }
  }
  return ret;
}

int ObPlanCacheValue::check_multi_stmt_not_param_value(
                              const ObIArray<ObFastParserResult> &multi_stmt_fp_results,
                              const ObIArray<NotParamInfo> &not_param_info,
                              bool &is_same)
{
  int ret = OB_SUCCESS;
  is_same = true;
  for (int64_t i = 0; OB_SUCC(ret) && is_same && i < multi_stmt_fp_results.count(); i++) {
    if (OB_FAIL(check_not_param_value(multi_stmt_fp_results.at(i),
                                      not_param_info,
                                      is_same))) {
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObPlanCacheValue::check_insert_multi_values_param(ObPlanCacheCtx &pc_ctx, bool &is_same)
{
  int ret = OB_SUCCESS;
  ObRawParams *raw_params = nullptr;
  int64_t count = pc_ctx.insert_batch_opt_info_.multi_raw_params_.count();
  for (int64_t i = 0; OB_SUCC(ret) && is_same && i < count; i++) {
    if (OB_ISNULL(raw_params = pc_ctx.insert_batch_opt_info_.multi_raw_params_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null ptr", K(ret));
    } else if (OB_FAIL(check_not_param_value(*raw_params,
                                             pc_ctx.not_param_info_,
                                             is_same))) {
    } else { /*do nothing*/ }
  }
  return ret;
}

int ObPlanCacheValue::check_not_param_value(const ObIArray<ObPCParam *> &raw_params,
                                            const ObIArray<NotParamInfo> &not_param_info,
                                            bool &is_same)
{
  int ret = OB_SUCCESS;
  ParseNode *raw_param = NULL;
  ObPCParam *pc_param = NULL;
  is_same = true;
  for (int64_t i = 0; OB_SUCC(ret) && is_same && i < not_param_info.count(); ++i) {
    if (OB_FAIL(raw_params.at(not_param_info.at(i).idx_, pc_param))) {
    } else if (OB_ISNULL(pc_param)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(pc_param));
    } else if (NULL == (raw_param = pc_param->node_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(raw_param));
    } else if (0 != not_param_info.at(i).raw_text_.compare(
        ObString(raw_param->text_len_, raw_param->raw_text_))) {
      is_same = false;
      LOG_TRACE("can't match not param info",
                "raw value", ObString(raw_param->text_len_, raw_param->raw_text_),
                "cached special value", not_param_info.at(i).raw_text_);
    } else {
      LOG_TRACE("match not param info success",
                "raw value", ObString(raw_param->text_len_, raw_param->raw_text_),
                "cached special value", not_param_info.at(i).raw_text_);
    }
  }
  return ret;
}

int ObPlanCacheValue::check_not_param_value(const ObFastParserResult &fp_result,
                                            const ObIArray<NotParamInfo> &not_param_info,
                                            bool &is_same)
{
  int ret = OB_SUCCESS;
  ParseNode *raw_param = NULL;
  ObPCParam *pc_param = NULL;
  is_same = true;
  for (int64_t i = 0; OB_SUCC(ret) && is_same && i < not_param_info.count(); ++i) {
    if (OB_FAIL(fp_result.raw_params_.at(not_param_info.at(i).idx_, pc_param))) {
    } else if (OB_ISNULL(pc_param)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(pc_param));
    } else if (NULL == (raw_param = pc_param->node_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(raw_param));
    } else if (0 != not_param_info.at(i).raw_text_.compare(
        ObString(raw_param->text_len_, raw_param->raw_text_))) {
      is_same = false;
      LOG_TRACE("match not param info",
                "raw value", ObString(raw_param->text_len_, raw_param->raw_text_),
                "cached special value", not_param_info.at(i).raw_text_);
    } else {
      LOG_TRACE("match param info",
                "raw value", ObString(raw_param->text_len_, raw_param->raw_text_),
                "cached special value", not_param_info.at(i).raw_text_);
    }
  }
  return ret;
}

int ObPlanCacheValue::get_outline_param_index(ObExecContext &exec_ctx, int64_t &param_idx) const
{
  int ret = OB_SUCCESS;
  param_idx = OB_INVALID_INDEX;
  int64_t param_count = outline_params_wrapper_.get_outline_params().count();
  int64_t concurrent_num = INT64_MAX;
  if (exec_ctx.get_physical_plan_ctx() != NULL) {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_count; ++i) {
      bool is_match = false;
      const ObMaxConcurrentParam *param = outline_params_wrapper_.get_outline_params().at(i);
      if (OB_ISNULL(param)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("param is NULl", K(ret));
      } else if (OB_FAIL(param->match_fixed_param(exec_ctx.get_physical_plan_ctx()->get_param_store(), is_match))) {
      } else if (is_match && param->concurrent_num_ < concurrent_num) {
        concurrent_num = param->concurrent_num_;
        param_idx = i;
      } else {/*do nothing*/}
    }
  }
  return ret;
}

const ObMaxConcurrentParam *ObPlanCacheValue::get_outline_param(int64_t index) const
{
  return outline_params_wrapper_.get_outline_param(index);
}

int ObPlanCacheValue::get_one_group_params(int64_t pos, const ParamStore &src_params, ParamStore &dst_params)
{
  int ret = OB_SUCCESS;
  int64_t N = src_params.count();
  if (OB_FAIL(dst_params.reserve(N))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
    const ObObjParam &objparam = src_params.at(i);
    const ObSqlArrayObj *array_obj = NULL;
    if (OB_UNLIKELY(!objparam.is_ext_sql_array())) {
      if (OB_FAIL(dst_params.push_back(objparam))) {
      }
      LOG_DEBUG("get one obj", K(ret), K(pos), K(i), K(objparam));
    } else if (OB_ISNULL(array_obj = reinterpret_cast<const ObSqlArrayObj*>(objparam.get_ext()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret), K(i), K(objparam));
    } else if (array_obj->count_ <= pos) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid parameters pos", K(ret), K(i), K(pos), K(objparam));
    } else if (OB_FAIL(dst_params.push_back(array_obj->data_[pos]))) {
    } else {
      LOG_TRACE("get one batch obj", K(pos), K(i), K(array_obj->data_[pos]));
    }
  }
  return ret;
}

int ObPlanCacheValue::match_and_generate_ext_params(ObPlanSet *batch_plan_set,
                                                    ObPlanCacheCtx &pc_ctx,
                                                    int64_t outline_param_idx)
{
  int ret = OB_SUCCESS;
  ParamStore *ab_params = pc_ctx.ab_params_;
  int64_t query_cnt = pc_ctx.sql_ctx_.get_batch_params_count();
  if (OB_ISNULL(batch_plan_set)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("plan_set is null", K(ret));
  } else if (OB_ISNULL(ab_params)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ab_params is null", K(ret));
  } else {
    ObArenaAllocator tmp_alloc;
    ParamStore param_store((ObWrapperAllocator(tmp_alloc)));
    bool is_same = false;
    ObPhysicalPlanCtx *plan_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
    int64_t origin_params_cnt = plan_ctx->get_original_param_cnt();
    for (int64_t i = 0; OB_SUCC(ret) && i < query_cnt; ++i) {
      is_same = false;
      param_store.reuse();
      const ParamStore &params = pc_ctx.exec_ctx_.get_physical_plan_ctx()->get_param_store();
      plan_ctx->reset_datum_param_store();
      plan_ctx->set_original_param_cnt(origin_params_cnt);
      if (OB_FAIL(get_one_group_params(i, *ab_params, param_store))) {
      } else if (OB_FAIL(plan_ctx->get_param_store_for_update().assign(param_store))) {
      } else if (OB_FAIL(plan_ctx->init_datum_param_store())) {
      } else if (OB_FAIL(batch_plan_set->match_params_info(&(plan_ctx->get_param_store()),
                                                           pc_ctx,
                                                           outline_param_idx,
                                                           is_same))) {
      } else if (!is_same) {
        ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
        LOG_TRACE("params is not same type", K(param_store), K(i));
      } else {
        // do nothing
      }
    }
    // All the parameters matched successfully, then fold the parameters
    if (OB_SUCC(ret)) {
      plan_ctx->reset_datum_param_store();
      plan_ctx->set_original_param_cnt(origin_params_cnt);
      if (OB_FAIL(plan_ctx->get_param_store_for_update().assign(*ab_params))) {
      } else if (OB_FAIL(plan_ctx->init_datum_param_store())) {
      } else {
        LOG_DEBUG("succ to init datum", K(ret));
      }
    }
  }
  return ret;
}

int ObPlanCacheValue::add_plan(ObPlanCacheObject &plan,
                               const ObIArray<PCVSchemaObj> &schema_array,
                               ObPlanCacheCtx &pc_ctx)
{
  int ret = OB_SUCCESS;
  bool need_new_planset = true;
  bool is_old_version = false;
  int64_t outline_param_idx = OB_INVALID_INDEX;
  int add_plan_ret = OB_SUCCESS;
  bool is_multi_stmt_batch = pc_ctx.sql_ctx_.is_batch_params_execute();
  // Check the version of the view and table involved in this SQL cached in pcv,
  // If not the latest, in the plan cache layer this value will be deleted and the plan will be re-added
  if (OB_FAIL(check_value_version_for_add(plan,
                                          schema_array,
                                          is_old_version))) {
  } else if (is_old_version) {
    ret = OB_OLD_SCHEMA_VERSION;
    SQL_PC_LOG(TRACE, "view or table is old version", K(ret));
  }
  if (OB_FAIL(ret)) {//do nothing
  } else if (OB_FAIL(get_outline_param_index(pc_ctx.exec_ctx_, outline_param_idx))) {
  } else if (OB_INVALID_INDEX == outline_param_idx
             && outline_params_wrapper_.get_outline_params().count() > 0) {
    plan.get_outline_state().reset();
  }
  if (OB_SUCC(ret)) {
    DLIST_FOREACH(cur_plan_set, plan_sets_) {
      bool is_same = false;
      if (OB_FAIL(cur_plan_set->match_params_info(plan.get_params_info(),
                                                  outline_param_idx,
                                                  pc_ctx,
                                                  is_same))) {
      } else if (false == is_same) { //do nothing
      } else {//param info already matched
        SQL_PC_LOG(DEBUG, "add plan to plan set");
        need_new_planset = false;
        if (is_multi_stmt_batch &&
            OB_FAIL(match_and_generate_ext_params(cur_plan_set, pc_ctx, outline_param_idx))) {
          LOG_TRACE("fail to match and generate ext_params", K(ret));
        } else if (OB_FAIL(cur_plan_set->add_cache_obj(plan, pc_ctx, outline_param_idx, add_plan_ret))) {
        }
        break;
      }
    } // end for

    /**
     *  Add the plan to a new allocated plan set
     *  and then add the plan set to plan cache value
     */
    if (OB_SUCC(ret) && need_new_planset) {
      SQL_PC_LOG(DEBUG, "add new plan set");
      ObPlanSet *plan_set = nullptr;
      if (OB_FAIL(create_new_plan_set(ObPlanSet::get_plan_set_type_by_cache_obj_type(plan.get_ns()),
                                      plan_set))) {
      } else {
        plan_set->set_plan_cache_value(this);
        if (OB_FAIL(plan_set->init_new_set(pc_ctx,
                                           plan,
                                           outline_param_idx,
                                           get_pc_malloc()))) {
        } else if (is_multi_stmt_batch &&
            OB_FAIL(match_and_generate_ext_params(plan_set, pc_ctx, outline_param_idx))) {
          LOG_TRACE("fail to match and generate ext_params", K(ret));
        } else if (OB_FAIL(plan_set->add_cache_obj(plan, pc_ctx, outline_param_idx, add_plan_ret))) {
        } else if (!plan_sets_.add_last(plan_set)) {
          ret = OB_ERROR;
          SQL_PC_LOG(WARN, "failed to add plan set to plan cache value", K(ret));
        } else {
          ret = OB_SUCCESS;
          SQL_PC_LOG(DEBUG, "plan set added", K(ret));
        }

        // free the memory if not used
        if (OB_FAIL(ret) && NULL != plan_set) {
          free_plan_set(plan_set);
          plan_set = NULL;
        }
      }
    }
  }
  return ret;
}
// Delete the corresponding plan from the plan cache value
/*void ObPlanCacheValue::remove_plan(ObExecContext &exec_context, ObPhysicalPlan &plan)*/
//{
  //int ret = OB_SUCCESS;
  //int64_t outline_param_idx = OB_INVALID_INDEX;
  //if (OB_FAIL(get_outline_param_index(exec_context, outline_param_idx))) {
    //LOG_WARN("fail to judge concurrent limit sql", K(ret));
  //} else {
    //DLIST_FOREACH(cur_plan_set, plan_sets_) {
      //bool is_same = false;
      //if (OB_FAIL(cur_plan_set->match_params_info(plan.get_params_info(),
                                                  //outline_param_idx,
                                                  //is_same))) {
        //BACKTRACE(ERROR, true, "fail to match param info");
      //} else if (true == is_same) {
        //cur_plan_set->remove_plan(plan);
      //}
    //} // end for
  //}
/*}*/

void ObPlanCacheValue::reset()
{
  // TODO TBD: do nothing to rw_lock_
  ObDLinkBase<ObPlanCacheValue>::reset();
  last_plan_id_ = OB_INVALID_ID;
  while (!plan_sets_.is_empty()) {
    ObPlanSet *plan_set = plan_sets_.get_first();
    if (OB_ISNULL(plan_set)) {
      //do nothing;
    } else {
      plan_sets_.remove(plan_set);
      plan_set->remove_all_plan();
      free_plan_set(plan_set);
      plan_set = nullptr;
    }
  }
  plan_sets_.clear();
  // free plan_cache_key
  if (NULL == pc_alloc_) {
    SQL_PC_LOG(TRACE, "pc alloc not init, may be reset before", K(pc_alloc_));
  } else {
    for (int64_t i = 0; i < not_param_info_.count(); i++) {
      if (NULL != not_param_info_.at(i).raw_text_.ptr()) {
        pc_alloc_->free(not_param_info_.at(i).raw_text_.ptr());
        not_param_info_.at(i).raw_text_.reset();
      }
    }

    for (int64_t i = 0; i < not_param_var_.count(); i++) {
      // deep-copy destory
      // free v
      ObObjParam param = not_param_var_.at(i).ps_param_;
      void * ptr = param.get_deep_copy_obj_ptr();
      if (NULL == ptr){
        // do nothing
      } else {
        pc_alloc_->free(ptr);
        not_param_var_.at(i).ps_param_.reset();
      }
    }

    if (NULL != outline_signature_.ptr()) {
      pc_alloc_->free(outline_signature_.ptr());
      outline_signature_.reset();
    }
    if (NULL != outline_format_signature_.ptr()) {
      pc_alloc_->free(outline_format_signature_.ptr());
      outline_format_signature_.reset();
    }
    if (NULL != constructed_sql_.ptr()) {
      pc_alloc_->free(constructed_sql_.ptr());
      constructed_sql_.reset();
    }
  }
  not_param_info_.reset();
  not_param_index_.reset();
  not_param_var_.reset();
  neg_param_index_.reset();
  fmt_int_or_ch_decint_idx_.reset();
  param_charset_type_.reset();
  sql_traits_.reset();

  if (OB_SUCCESS != outline_params_wrapper_.destroy()) {
    LOG_ERROR_RET(OB_ERROR, "fail to destroy ObOutlineParamWrapper");
  }
  outline_params_wrapper_.reset_allocator();
  //use_global_location_cache_ = true;
  tenant_schema_version_ = OB_INVALID_VERSION;
  sys_schema_version_ = OB_INVALID_VERSION;
  outline_state_.reset();
  sessid_ = OB_INVALID_ID;
  sess_create_time_ = 0;
  contain_sys_name_table_ = false;
  is_nested_sql_ = false;
  is_batch_execute_ = false;
  has_dynamic_values_table_ = false;
  for (int64_t i = 0; i < stored_schema_objs_.count(); i++) {
    if (OB_ISNULL(stored_schema_objs_.at(i)) || OB_ISNULL(pc_alloc_)) {
      // do nothing
    } else {
      stored_schema_objs_.at(i)->reset();
      pc_alloc_->free(stored_schema_objs_.at(i));
    }
  }
  stored_schema_objs_.reset();
  enable_rich_vector_format_ = false;
  pcv_set_ = NULL; // put last, there may be need for pcv_set before this
}
// Get all plan memory usage under this plan cache value

int ObPlanCacheValue::check_value_version_for_add(const ObPlanCacheObject &cache_obj,
                                                  const ObIArray<PCVSchemaObj> &schema_array,
                                                  bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
  if (OB_FAIL(check_value_version(true/*need_check_schema*/,
                                  cache_obj.get_outline_state().outline_version_,
                                  schema_array,
                                  result))) {
  } else {
    // do nothing
  }

  return ret;
}

int ObPlanCacheValue::check_value_version_for_get(share::schema::ObSchemaGetterGuard *schema_guard,
                                                  bool need_check_schema,
                                                  const ObIArray<PCVSchemaObj> &schema_array,
                                                  bool &result)
{
  int ret = OB_SUCCESS;
  ObSchemaObjVersion local_outline_version;
  result = false;
  if (OB_ISNULL(schema_guard)) {
    int ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(schema_guard));
  } else if (need_check_schema
             &&OB_FAIL(get_outline_version(*schema_guard,
                                           local_outline_version))) {
    LOG_WARN("failed to get local outline version", K(ret));
  } else if (OB_FAIL(check_value_version(need_check_schema,
                                         local_outline_version,
                                         schema_array,
                                         result))) {
  } else {
    // do nothing
  }
  return ret;
}
// Check if the plan is expired logic:
//1.If :schema changes:
//    a. Determine if the outline version is expired
//    b. judge if table version is expired
//    c. Determine if view version is expired
//  Otherwise: directly consider step 2
//2.If :plan is generated by outline, then there is no need to concern whether merge_version changes
//  Otherwise: judge if merge_version is expired.
//3. Is the histogram statistics information of the table already expired
int ObPlanCacheValue::check_value_version(bool need_check_schema,
                                          const ObSchemaObjVersion &outline_version,
                                          const ObIArray<PCVSchemaObj> &schema_array,
                                          bool &is_old_version)
{
  int ret = OB_SUCCESS;
  is_old_version = false;
  if (need_check_schema) {
    if (outline_version != outline_state_.outline_version_) {
      is_old_version = true;
    } else if (OB_FAIL(check_dep_schema_version(schema_array,
                                                is_old_version))) {
    } else {
      // do nothing
    }
  }
  return ret;
}
// Check if table schema version is expired, used when getting plan
int ObPlanCacheValue::check_dep_schema_version(const ObIArray<PCVSchemaObj> &schema_array,
                                               bool &is_old_version)
{
  int ret = OB_SUCCESS;
  is_old_version = false;
  int64_t table_count = schema_array.count();
  ObSEArray<PCVSchemaObj*, 4> check_stored_schema;

  if (OB_FAIL(remove_mv_schema(schema_array, check_stored_schema))) {
  } else if (schema_array.count() != check_stored_schema.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table count do not match", K(ret), K(schema_array.count()), K(check_stored_schema.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && !is_old_version && i < table_count; ++i) {
      const PCVSchemaObj *schema_obj1 = check_stored_schema.at(i);
      const PCVSchemaObj &schema_obj2 = schema_array.at(i);
      if (nullptr == schema_obj1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("got an unexpected null table schema", K(ret), K(schema_obj1));
      } else if (*schema_obj1 == schema_obj2) { // schema do match
        LOG_DEBUG("matched schema objs", K(*schema_obj1), K(schema_obj2), K(i));
        // do nothing
      } else {
        LOG_TRACE("mismatched schema objs", K(*schema_obj1), K(schema_obj2), K(i));
        is_old_version = true;
      }
    }
  }

  return ret;
}
// Check if outline version is expired
int ObPlanCacheValue::get_outline_version(ObSchemaGetterGuard &schema_guard,
                                          ObSchemaObjVersion &local_outline_version)
{
  int ret = OB_SUCCESS;
  const ObOutlineInfo *outline_info = NULL;
  local_outline_version.reset();
  uint64_t database_id = OB_INVALID_ID;
  if (OB_ISNULL(pcv_set_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("pcv_set_ is null");
  } else if (OB_INVALID_ID == (database_id = pcv_set_->get_plan_cache_key().db_id_)
             || ObLibCacheNameSpace::NS_PRCR == pcv_set_->get_plan_cache_key().namespace_
             || ObLibCacheNameSpace::NS_SFC == pcv_set_->get_plan_cache_key().namespace_
             || ObLibCacheNameSpace::NS_PKG == pcv_set_->get_plan_cache_key().namespace_
             || ObLibCacheNameSpace::NS_ANON == pcv_set_->get_plan_cache_key().namespace_) {
    //do nothing
  } else {
    const ObString &signature = outline_signature_;
    const ObString &format_signature = outline_format_signature_;
    // try normal
    if (OB_FAIL(schema_guard.get_outline_info_with_signature(database_id,
            signature,
            false,
            outline_info))) {
    } else if (NULL == outline_info && 
              OB_FAIL(schema_guard.get_outline_info_with_signature(database_id,
                      format_signature,
                      true,
                      outline_info))) {
        LOG_WARN("failed to get_outline_info", K(database_id), K(signature));
    // try normal
    } else if (NULL == outline_info && !ObString::make_string(sql_id_).empty() &&
              OB_FAIL(schema_guard.get_outline_info_with_sql_id(database_id,
                      ObString::make_string(sql_id_),
                      false,
                      outline_info))) {
        LOG_WARN("failed to get_outline_info", K(database_id), K(signature));
    // try format
    } else if (NULL == outline_info && !ObString::make_string(format_sql_id_).empty() &&
              OB_FAIL(schema_guard.get_outline_info_with_sql_id(database_id,
                      ObString::make_string(format_sql_id_),
                      true,
                      outline_info))) {
        LOG_WARN("failed to get_outline_info", K(database_id), K(signature));
    }
    if (OB_SUCC(ret)) {
      if (NULL == outline_info) {
        // do nothing
      } else {
        local_outline_version.object_id_ = outline_info->get_outline_id();
        local_outline_version.version_ = outline_info->get_schema_version();
        local_outline_version.object_type_ = DEPENDENCY_OUTLINE;
      }
    }
  }
  return ret;
}
// 1. Through matching non-parameterizable constants
// 2. Check if it contains temporary tables and tables with the same name
int ObPlanCacheValue::match(ObPlanCacheCtx &pc_ctx,
                            const ObIArray<PCVSchemaObj> &schema_array,
                            bool &is_same)
{
  int ret = OB_SUCCESS;
  is_same = true;
  //compare not param
  ObSQLSessionInfo *session_info = pc_ctx.sql_ctx_.session_info_;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(session_info));
  }
  if (OB_FAIL(ret)) {
  } else if (is_nested_sql_ != ObSQLUtils::is_nested_sql(&pc_ctx.exec_ctx_)) {
    //the plan of nested sql can't match with the plan of general sql
    //because nested sql's plan be forced to use DAS plan
    //but the general sql's plan has no this constraint
    is_same = false;
  } else if (is_batch_execute_ != pc_ctx.sql_ctx_.is_batch_params_execute()||
             has_dynamic_values_table_ != pc_ctx.exec_ctx_.has_dynamic_values_table()) {
    // the plan of batch execute sql can't match with the plan of general sql
    is_same = false;
  } else if (!need_param_) {
    // not needs to param, compare raw_sql
    is_same = (pc_ctx.raw_sql_==raw_sql_);
  } else if (PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_) {
    const ObObjParam *ps_param = NULL;
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < not_param_var_.count(); ++i) {
      ps_param = NULL;
      if (OB_FAIL(pc_ctx.fp_result_.parameterized_params_.at(not_param_var_[i].idx_, ps_param))) {
      } else if (OB_ISNULL(ps_param)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ps_param));
      } else if (ps_param->is_pl_extend() || not_param_var_[i].ps_param_.is_pl_extend() 
                  || !not_param_var_[i].ps_param_.can_compare(*ps_param)) {
        is_same = false;
        LOG_WARN("can not compare", K(not_param_var_[i].ps_param_), K(*ps_param), K(i));
      } else if (not_param_var_[i].ps_param_.is_string_type()
          && not_param_var_[i].ps_param_.get_collation_type() != ps_param->get_collation_type()) {
        is_same = false;
        LOG_WARN("can not compare", K(not_param_var_[i].ps_param_), K(*ps_param), K(i));
      } else if (0 != not_param_var_[i].ps_param_.compare(*ps_param)) {
        is_same = false;
        LOG_WARN("match not param var", K(not_param_var_[i]), K(ps_param), K(i));
      }
      LOG_DEBUG("match", K(not_param_var_[i].idx_), K(not_param_var_[i].ps_param_), KPC(ps_param));
    }
  } else {
    if (OB_FAIL(check_not_param_value(pc_ctx.fp_result_, not_param_info_, is_same))) {
    }
  }

  // check for temp tables and same table
  if (OB_SUCC(ret) && is_same && schema_array.count() != 0) {
    if (OB_FAIL(match_dep_schema(pc_ctx, schema_array, is_same))) {
    }
  }
  return ret;
}

bool ObPlanCacheValue::is_contain_tmp_tbl() const
{
  bool is_contain = false;

  for (int64_t i = 0; !is_contain && i < stored_schema_objs_.count(); i++) {
    if (nullptr != stored_schema_objs_.at(i)
        && stored_schema_objs_.at(i)->is_tmp_table_) {
      is_contain = true;
    }
  }

  return is_contain;
}

bool ObPlanCacheValue::is_contain_synonym() const
{
  bool is_contain = false;

  for (int64_t i = 0; !is_contain && i < stored_schema_objs_.count(); i++) {
    if (nullptr != stored_schema_objs_.at(i)
        && (SYNONYM_SCHEMA == stored_schema_objs_.at(i)->schema_type_)) {
      is_contain = true;
    }
  }

  return is_contain;
}

/*!
 * The update of system packages/types will only increase the schema_version of the system tenant. Objects under normal tenants that depend on system packages/types
 * may miss the check for whether the system packages/types are expired during the update because the schema_version of the normal tenant is not increased,
 * leading to the routine objects dependent on system packages/types being unavailable after the update. Therefore, schema checks are always performed on objects containing system packages/types.
 */
bool ObPlanCacheValue::is_contain_sys_pl_object() const
{
  bool is_contain = false;

  for (int64_t i = 0; !is_contain && i < stored_schema_objs_.count(); i++) {
    if (nullptr != stored_schema_objs_.at(i)
        && (PACKAGE_SCHEMA == stored_schema_objs_.at(i)->schema_type_
            || UDT_SCHEMA == stored_schema_objs_.at(i)->schema_type_)
        && true) {
      is_contain = true;
    }
  }

  return is_contain;
}

int ObPlanCacheValue::get_tmp_depend_tbl_names(TmpTableNameArray &tmp_tbl_names) {
  int ret = OB_SUCCESS;

  for (int64_t i = 0; OB_SUCC(ret) && i < stored_schema_objs_.count(); i++) {
    if (OB_ISNULL(stored_schema_objs_.at(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("got unexpected null", K(ret), K(i));
    } else if (stored_schema_objs_.at(i)->is_tmp_table_ &&
               OB_FAIL(tmp_tbl_names.push_back(stored_schema_objs_.at(i)->table_name_))) {
      LOG_WARN("failed to push back tmp table name ptr", K(ret), K(i));
    }
  }
  return ret;
}

int ObPlanCacheValue::create_new_plan_set(const ObPlanSetType plan_set_type,
                                          ObPlanSet *&new_plan_set)
{
  int ret = OB_SUCCESS;
  ObIAllocator *pc_alloc = get_pc_alloc();
  void *buff = nullptr;
  new_plan_set = nullptr;

  int64_t mem_sz =  sizeof(ObSqlPlanSet);

  if (OB_ISNULL(pc_alloc) ||
      (plan_set_type < PST_SQL_CRSR || plan_set_type >= PST_MAX)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(pc_alloc), K(plan_set_type));
  } else if (nullptr == (buff = pc_alloc->alloc(mem_sz))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for ObPlanSet", K(ret));
  } else {
    new_plan_set = new(buff)ObSqlPlanSet();
  }

  if (OB_SUCC(ret)) {
    // do nothing
  } else if (nullptr != new_plan_set && nullptr != pc_alloc) { // cleanup
    new_plan_set->reset();
    new_plan_set->~ObPlanSet();
    pc_alloc->free(new_plan_set);
    new_plan_set = nullptr;
  }
  return ret;
}

void ObPlanCacheValue::free_plan_set(ObPlanSet *plan_set)
{
  int ret = OB_SUCCESS;
  ObIAllocator *pc_alloc = get_pc_alloc();
  if (OB_ISNULL(pc_alloc) || OB_ISNULL(plan_set)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(pc_alloc), K(plan_set), K(ret));
  } else {
    plan_set->reset();
    plan_set->~ObPlanSet();
    pc_alloc->free(plan_set);
    plan_set = nullptr;
  }
}

int ObPlanCacheValue::set_stored_schema_objs(const DependenyTableStore &dep_table_store,
                                             share::schema::ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = nullptr;
  PCVSchemaObj *pcv_schema_obj = nullptr;
  void *obj_buf = nullptr;

  stored_schema_objs_.reset();
  stored_schema_objs_.set_allocator(pc_alloc_);

  if (OB_ISNULL(schema_guard)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid null argument", K(ret), K(schema_guard));
  } else if (OB_FAIL(stored_schema_objs_.init(dep_table_store.count()))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dep_table_store.count(); i++) {
      const ObSchemaObjVersion &table_version = dep_table_store.at(i);
      table_schema = nullptr;
      int hash_err = OB_SUCCESS;
      if (table_version.get_schema_type() != TABLE_SCHEMA) {
        // If not table schema, directly store schema id and version only, synonym store an additional db_id
        if (OB_FAIL(ret)) {
        } else if (nullptr == (obj_buf = pc_alloc_->alloc(sizeof(PCVSchemaObj)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate memory", K(ret));
        } else if (FALSE_IT(pcv_schema_obj = new(obj_buf)PCVSchemaObj(pc_alloc_))) {
          // do nothing
        } else if (OB_FAIL(pcv_schema_obj->init_with_version_obj(table_version))) {
        } else if (OB_FAIL(stored_schema_objs_.push_back(pcv_schema_obj))) {
        } else {
          obj_buf = nullptr;
          pcv_schema_obj = nullptr;
        }
      } else if (OB_FAIL(schema_guard->get_table_schema(
                  table_version.get_object_id(),
                  table_schema))) {
      } else if (nullptr == table_schema) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get an unexpected null schema", K(ret), K(table_schema));
      } else if (table_schema->is_index_table()) {
        // plan cache does not need to consider the index table, directly skip the comparison
        // do nothing
      } else if (nullptr == (obj_buf = pc_alloc_->alloc(sizeof(PCVSchemaObj)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory", K(ret));
      } else if (FALSE_IT(pcv_schema_obj = new(obj_buf)PCVSchemaObj(pc_alloc_))) {
        // do nothing
      } else if (OB_FAIL(pcv_schema_obj->init(table_schema))) {
      } else if (FALSE_IT(pcv_schema_obj->is_explicit_db_name_ = table_version.is_db_explicit_)) {
        // do nothing
      } else if (OB_FAIL(stored_schema_objs_.push_back(pcv_schema_obj))) {
      } else if(!contain_sys_name_table_) {
        // oracle mode allows regular tables to have the same name as tables under sys, plan matching needs to distinguish to match different plans
        // The tables under sys include system tables and views, so call is_sys_table_name to check if the table is under sys
        // In addition, if the sql contains internal tables, changes in the schema version of internal tables will not be reflected in the tenant schema version of normal tenants
        // In order to update the plan in a timely manner, it is necessary to check the schema version number of the corresponding internal table. The internal tables of Oracle tenants are under sys, mysql tenants
        // Under oceanbase.
        if (OB_FAIL(share::schema::ObSysTableChecker::is_sys_table_name(OB_SYS_DATABASE_ID,
                                                                               table_schema->get_table_name(),
                                                                               contain_sys_name_table_))) {
        } else {
          // do nothing
        }
        LOG_TRACE("check sys table", K(table_schema->get_table_name()), K(contain_sys_name_table_));
      } else {
        // do nothing
      }
      obj_buf = nullptr;
      pcv_schema_obj = nullptr;
      table_schema = nullptr;
    } // for end
  }
  if (OB_FAIL(ret)) {
    stored_schema_objs_.reset();
  } else {
    // do nothing
  }
  return ret;
}

/* used to get plan

   For the case where the query is for Table Schema, the database id needs to be passed in, with different strategies for different modes
   MySQL mode:
     When getting the plan, directly retrieve the database id from the cache PCVSchemaObj

   Oracle mode:
     In Oracle mode, if the schema is not specified in the SQL, use the db id from the session, e.g., select xx from test;
     Otherwise, directly use the table id from PCVSchemaObj to query the schema, e.g., select xx from user.test;

     The reason for doing this is to solve the following scenario: user1 and user2 both have a table named test,
     when connecting with user1 and executing select * from user2.test,
     if the db id from the session connected with user1 is used directly, since the table_name is used to query the schema,
     it will query the schema under user1 and use it for match logic, which will definitely fail to match. In this scenario, if the db id is specified, the db id is fixed, directly using the table_id
     to query the schema can query the schema of the test table under user2

   Oracle system tables and regular tables solution:
     Use the database id from the current session to query the table_name, if it exists then use this table schema as the matching condition; otherwise go to SYS DB to query the schema
 */
int ObPlanCacheValue::get_all_dep_schema(ObPlanCacheCtx &pc_ctx,
                                         const uint64_t database_id,
                                         int64_t &new_schema_version,
                                         bool &need_check_schema,
                                         ObIArray<PCVSchemaObj> &schema_array)
{
  int ret = OB_SUCCESS;
  need_check_schema = false;
  if (OB_FAIL(need_check_schema_version(pc_ctx,
                                        new_schema_version,
                                        need_check_schema))) {
  } else if (!need_check_schema) {
    // do nothing
  } else if (OB_ISNULL(pc_ctx.sql_ctx_.schema_guard_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(pc_ctx.sql_ctx_.schema_guard_));
  } else {
    schema_array.reset();
    const ObSimpleTableSchemaV2 *table_schema = nullptr;
    PCVSchemaObj tmp_schema_obj;
    
    for (int64_t i = 0; OB_SUCC(ret) && i < stored_schema_objs_.count(); i++) {
      ObSchemaGetterGuard &schema_guard = *pc_ctx.sql_ctx_.schema_guard_;
      PCVSchemaObj *pcv_schema = stored_schema_objs_.at(i);
      if (OB_ISNULL(pcv_schema)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("got unexpected null", K(ret));
      } else if (TABLE_SCHEMA != pcv_schema->schema_type_) {
        // if no table schema, get schema version is enough
        int64_t new_version = 0;
        if (PACKAGE_SCHEMA == stored_schema_objs_.at(i)->schema_type_
            || UDT_SCHEMA == stored_schema_objs_.at(i)->schema_type_) {
        }
        if (OB_FAIL(ret)) { 
        } else if (OB_FAIL(schema_guard.get_schema_version(pcv_schema->schema_type_,
                                                    pcv_schema->schema_id_,
                                                    new_version))) {
        } else {
          if (SYNONYM_SCHEMA != pcv_schema->schema_type_) {
            tmp_schema_obj.schema_id_ = pcv_schema->schema_id_;
            tmp_schema_obj.schema_version_ = new_version;
            tmp_schema_obj.schema_type_ = pcv_schema->schema_type_;
          }
          if (OB_FAIL(schema_array.push_back(tmp_schema_obj))) {
          } else {
            tmp_schema_obj.reset();
          }
        }
      } else if (OB_FAIL(schema_guard.get_simple_table_schema(
                                                              pcv_schema->database_id_,
                                                              pcv_schema->table_name_,
                                                              false,
                                                              table_schema))) {
      } else {
        // do nothing
      }

      if (OB_FAIL(ret)) {
        // do nothing
      } else if (TABLE_SCHEMA != pcv_schema->schema_type_) { // not table schema
        tmp_schema_obj.reset();
      } else if (nullptr == table_schema) {
        ret = OB_OLD_SCHEMA_VERSION;
        LOG_TRACE("table not exist", K(ret), K(*pcv_schema), K(table_schema));
      } else if (OB_FAIL(tmp_schema_obj.init_without_copy_name(table_schema))) {
      } else if (OB_FAIL(schema_array.push_back(tmp_schema_obj))) {
      } else {
        table_schema = nullptr;
        tmp_schema_obj.reset();
      }
    } // for end
  }
  return ret;
}
// Targeting
// Materialized view rewrite makes different plans for the same SQL depend on different tables, leading to plan cache entering different pcv set
// In checking pcv set, skip tables related to materialized views, so that rewritten and non-rewritten SQL enter the same pcv set
int ObPlanCacheValue::remove_mv_schema(const common::ObIArray<PCVSchemaObj> &schema_array,
                                       common::ObIArray<PCVSchemaObj*> &check_stored_schema)
{
  int ret = OB_SUCCESS;
  bool need_remove = true;
  int64_t j = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < stored_schema_objs_.count(); ++i) {
    if (OB_ISNULL(stored_schema_objs_.at(i))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid null table schema", K(ret), K(i), K(stored_schema_objs_.at(i)));
    } else if (MATERIALIZED_VIEW == stored_schema_objs_.at(i)->table_type_
        || MATERIALIZED_VIEW_LOG == stored_schema_objs_.at(i)->table_type_
        || stored_schema_objs_.at(i)->is_mv_container_table_) {
      if (j < schema_array.count()
          && stored_schema_objs_.at(i)->schema_id_ == schema_array.at(j).schema_id_) {
        if (OB_FAIL(check_stored_schema.push_back(stored_schema_objs_.at(i)))) {
        } else {
          ++j;
        }
      } else {
        // do nothing, only materialized views existing in pcv set are rewritten, discard
      }
    } else if (OB_FAIL(check_stored_schema.push_back(stored_schema_objs_.at(i)))) {
    } else {
      ++j;
    }
  }
  return ret;
}
// For comparing the schema that the plan depends on, note that the version information of the table schema is not compared here
// table schema version information is used to phase out pcv set, during check_value_version comparison
int ObPlanCacheValue::match_dep_schema(const ObPlanCacheCtx &pc_ctx,
                                       const ObIArray<PCVSchemaObj> &schema_array,
                                       bool &is_same)
{
  int ret = OB_SUCCESS;
  is_same = true;
  ObSQLSessionInfo *session_info = pc_ctx.sql_ctx_.session_info_;
  common::ObSEArray<PCVSchemaObj*, 4> check_stored_schema;
  if (OB_ISNULL(session_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(session_info));
  } else if (OB_FAIL(remove_mv_schema(schema_array, check_stored_schema))) {
  } else if (schema_array.count() != check_stored_schema.count()) {
    // schema objs count mismatch, may be the following cases:
    // select * from all_sequences;  // system view, the dependency_table of the system view has multiple entries
    // select * from all_sequences;  // ordinary table
    is_same = false;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && is_same && i < schema_array.count(); i++) {
      if (OB_ISNULL(check_stored_schema.at(i))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid null table schema",
                 K(ret), K(i), K(schema_array.at(i)), K(check_stored_schema.at(i)));
      } else if (TMP_TABLE == schema_array.at(i).table_type_
                 && schema_array.at(i).is_tmp_table_) { // check for mysql tmp table
        // If it contains a temporary table
        // Temporary table is also a special case of a table with the same name, but here sessid_ is used to distinguish whether this pcv contains a temporary plan,
        // sessid_is not 0, then it is a pocv containing temporary tables, otherwise it is a pcv of an ordinary table
        // oracle mode, temporary tables are actually regular tables, server internally rewrites them by adding a sessid field to distinguish temporary tables on different sessions
        // sessid may be reused, so different temporary tables need to match the sessid and sess_create_time_ fields
        // In mysql mode, temporary tables are only created in the corresponding session, different sessid can distinguish them, sess_create_time must be the same
        // plan cache matching temporary tables should always use the user-created session to ensure the correctness of semantics
        // When executed remotely, a temporary session object will be created, and its session_id is also temporary,
        // So here the get_sessid_for_table() rule must be used to determine
        is_same = ((session_info->get_sessid_for_table() == sessid_) &&
                   (session_info->get_sess_create_time() == sess_create_time_));
      } else {
        // do nothing
      }
    }
  }
  return ret;
}


// used for add plan
int ObPlanCacheValue::get_all_dep_schema(ObSchemaGetterGuard &schema_guard,
                                         const DependenyTableStore &dep_schema_objs,
                                         ObIArray<PCVSchemaObj> &schema_array)
{
  int ret = OB_SUCCESS;
  schema_array.reset();
  const ObSimpleTableSchemaV2 *table_schema = nullptr;
  PCVSchemaObj tmp_schema_obj;

  for (int64_t i = 0; OB_SUCC(ret) && i < dep_schema_objs.count(); i++) {
    if (TABLE_SCHEMA != dep_schema_objs.at(i).get_schema_type()) {
      if (OB_FAIL(tmp_schema_obj.init_with_version_obj(dep_schema_objs.at(i)))) {
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_array.push_back(tmp_schema_obj))) {
      } else {
        tmp_schema_obj.reset();
      }
    } else if (OB_FAIL(schema_guard.get_simple_table_schema( dep_schema_objs.at(i).get_object_id(), table_schema))) {
    } else if (nullptr == table_schema) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get an unexpected null table schema", K(ret));
    } else if (table_schema->is_index_table()) {
      // do nothing
    } else if (OB_FAIL(tmp_schema_obj.init_without_copy_name(table_schema))) {
    } else if (OB_FAIL(schema_array.push_back(tmp_schema_obj))) {
    } else {
      table_schema = nullptr;
      tmp_schema_obj.reset();
    }
  }

  if (OB_FAIL(ret)) {
    schema_array.reset();
  } else {
    LOG_DEBUG("get all dep schema", K(schema_array));
  }
  return ret;
}

int ObPlanCacheValue::need_check_schema_version(ObPlanCacheCtx &pc_ctx,
                                                int64_t &new_schema_version,
                                                bool &need_check)
{
  int ret = OB_SUCCESS;
  need_check = false;
  if (OB_FAIL(pc_ctx.sql_ctx_.schema_guard_->get_schema_version(new_schema_version))) {
  } else {
    int64_t cached_tenant_schema_version = ATOMIC_LOAD(&tenant_schema_version_);
    /*
      session1 :
           create temporary table t1(c1 int, c2 int);
      session2 :
           create table t1(c1 int);
           insert into t1 values(1);
           select * from t1;
      session1 :
           select * from t1;   ->The plan match is wrong, the entity table t1 is queried, and one row is returned

        First create a temporary table and then create a normal table,
        and then add the plan of the normal table to the plan cache. In
        this case, it is impossible to predict whether there is a
        temporary table with the same name in the current session.
        Therefore, if there is a temporary table, you need to recheck the schema .
     */
    need_check = ((new_schema_version != cached_tenant_schema_version)
                  || is_contain_synonym()
                  || is_contain_tmp_tbl()
                  || is_contain_sys_pl_object()
                  || contain_sys_name_table_);
    if (need_check && REACH_TIME_INTERVAL(10000000)) { // 10s interval print
      LOG_INFO("need check schema", K(new_schema_version), K(cached_tenant_schema_version),
               K(is_contain_synonym()), K(contain_sys_name_table_), K(is_contain_tmp_tbl()),
               K(is_contain_sys_pl_object()), K(need_check), K(constructed_sql_));
    }
  }
  return ret;
}

int ObPlanCacheValue::lift_tenant_schema_version(int64_t new_schema_version)
{
  int ret = OB_SUCCESS;
  if (new_schema_version <= tenant_schema_version_) {
    // do nothing
  } else {
    ATOMIC_STORE(&(tenant_schema_version_), new_schema_version);
  }
  return ret;
}

int ObPlanCacheValue::check_contains_table(uint64_t db_id, common::ObString tab_name, bool &contains)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; !contains && i < stored_schema_objs_.count(); i++) {
    if (OB_ISNULL(stored_schema_objs_.at(i))) {
      // do nothing
    } else {
      if ((stored_schema_objs_.at(i)->database_id_ == db_id) &&
              (stored_schema_objs_.at(i)->table_name_ == tab_name)) {
        contains = true;
      }
    }
  }
  return ret;
}

}//end of namespace sql
}//end of namespace oceanbase
