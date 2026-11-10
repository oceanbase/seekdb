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

#include "rust/config.h"
#include "ob_plan_cache_util.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "common/datum/datum_copy_plan.h"
#include "sql/pl/ob_pl_type.h"
#include "sql/optimizer/ob_log_plan.h"
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::omt;

namespace oceanbase
{
namespace sql
{
namespace {
int constraint_legacy_view(const common::NativeTypedDatum &source,
                           common::DatumValue &view, char (&scratch)[64])
{
  int ret = OB_SUCCESS;
  if (source.type_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const common::DatumCopyPlan plan(*source.type_);
    common::ObDatum datum;
    if (OB_FAIL(plan.legacy_view(source.datum_, datum, scratch, sizeof(scratch)))) {
    } else {
      ret = view.assign_legacy_borrowed(datum, source.type_->legacy_meta());
    }
  }
  return ret;
}

bool simple_constraint_type(const common::SqlTypeMeta &meta)
{
  return !meta.is_enum_or_set() && !meta.is_enumset_inner_type()
      && !meta.is_collection_sql_type() && !meta.is_ext_sql_array()
      && !meta.is_pl_extend_type() && meta.get_type() != common::ObUserDefinedSQLType;
}
// The existing comparison preflight with a NULL peer validates the legacy
// descriptor before its no-comparison NULL branch. It reads no resource graph.
// Only this already-unsupported/absent-declaration path uses the admission
// marker; malformed inputs must remain errors instead of becoming cache misses.
int mark_uncapturable_constraint(const common::DatumValue &value, bool &eligible)
{
  const common::DatumValue null;
  const int ret = common::can_compare_datum_values(value, null)
      ? OB_SUCCESS : OB_INVALID_ARGUMENT;
  if (ret == OB_SUCCESS) { eligible = false; }
  return ret;
}
} // namespace

int ObPCConstParamInfo::add_constant(const common::Datum &value, const common::SqlType &type,
                                     common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  const common::DatumCopyPlan plan(type);
  int64_t span = 0;
  if (OB_FAIL(plan.carrier_span_size(value, span))) {
  } else if (value.is_present() && type.requires_resource_clone()) {
    // Such values were never comparable cache keys. Retain neither their
    // wrappers nor resources; the entire query is explicitly cache-ineligible.
    cache_eligible_ = false;
  } else if (cache_eligible_) {
    common::NativeTypedDatum owned;
    // Each API call names its actual owner. Equality alone does not establish
    // that an earlier slot's Type belongs to this allocator.
    if (OB_FAIL(type.clone_to(allocator, owned.type_))) {
    } else if (OB_FAIL(plan.deep_copy(value, owned.datum_, allocator))) {
    } else { ret = const_params_.push_back(owned); }
  }
  return ret;
}

int ObPCConstParamInfo::add_constant(const common::ObDatum &value, const common::SqlType &type,
                                     common::ObIAllocator &allocator)
{
  common::Datum imported;
  const common::DatumCopyPlan plan(type);
  const int ret = plan.import_legacy(value, imported);
  return ret == OB_SUCCESS ? add_constant(imported, type, allocator) : ret;
}

int ObPCConstParamInfo::add_runtime_parameter(const ObPhysicalPlanCtx &parameters,
                                              int64_t index,
                                              common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  common::NativeTypedDatum value;
  common::DatumParameterMetadata attributes;
  if (OB_FAIL(parameters.capture_native_parameter(index, value, attributes))) {
  } else if (value.type_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t span = 0;
    const common::DatumCopyPlan plan(*value.type_);
    if (OB_FAIL(plan.carrier_span_size(value.datum_, span))) {
    } else if (value.type_->requires_resource_clone()
        || parameters.get_group_parameter_input(index) != nullptr) {
      // P1 does not broaden cache admission for resource/PL typed NULL or
      // original grouped arrays merely because a complete bound Type exists.
      // Execution keeps its value; no cache key or synthetic Type is published.
      cache_eligible_ = false;
    } else {
      ret = add_constant(value.datum_, *value.type_, allocator);
    }
  }
  return ret;
}

int ObPCConstParamInfo::add_parameter(const common::DatumParameter &value,
                                      const ObExecContext *actual_ctx,
                                      common::ObIAllocator &allocator,
                                      const pl::ObPLINS *actual_declarations)
{
  int ret = OB_SUCCESS;
  auto add_prepared = [&](const common::SqlType &type) {
    alignas(8) char scratch[64];
    common::ObDatum legacy;
    const int code = value.export_legacy(legacy, scratch, sizeof(scratch));
    // The wrapper is generic physical (Decimal keeps actual bytes, Unknown
    // is Reference8). The actual prepared Type chooses semantic representation.
    return code == OB_SUCCESS ? add_constant(legacy, type, allocator) : code;
  };
  // A runtime NULL has no payload type, but its protocol/declaration metadata
  // and full accuracy remain available independently of initialized frames.
  const common::SqlTypeMeta &meta = value.is_null() && value.get_type_meta().is_null()
      && !value.get_param_meta().is_null() ? value.get_param_meta() : value.get_type_meta();
  if (simple_constraint_type(meta)) {
    common::SqlType type;
    if (OB_FAIL(common::SqlType::from_legacy(meta, value.get_accuracy(), type))) {
    } else { ret = add_prepared(type); }
  } else if (meta.get_type() == common::ObUserDefinedSQLType
      || actual_ctx == nullptr || (meta.is_pl_extend_type()
      && (meta.get_extend_type() != pl::PL_RECORD_TYPE || actual_declarations == nullptr))) {
    ret = mark_uncapturable_constraint(value.value_view(), cache_eligible_);
  } else {
    common::ObArenaAllocator temporary("PCConstType");
    const common::SqlType *type = nullptr;
    if (OB_FAIL(ObExprUtil::resolve_sql_type(*const_cast<ObExecContext *>(actual_ctx), meta, value.get_accuracy(),
          meta.get_subschema_id(), temporary, type, actual_declarations))) {
    } else { ret = add_prepared(*type); }
  }
  return ret;
}

int ObPCConstParamInfo::add_raw_constant(const common::DatumValue &value,
                                         const ObRawExprResType &original_type,
                                         const ObSQLSessionInfo *session,
                                         share::schema::ObSchemaGetterGuard *schema_guard,
                                         common::ObIAllocator &allocator,
                                         const pl::ObPLINS *actual_declarations)
{
  int ret = OB_SUCCESS;
  auto add_prepared = [&](const common::SqlType &type) {
    alignas(8) char scratch[64];
    common::ObDatum legacy;
    const int code = value.export_legacy(legacy, scratch, sizeof(scratch));
    // The wrapper is generic physical (Decimal keeps actual bytes, Unknown
    // is Reference8). The actual prepared Type chooses semantic representation.
    return code == OB_SUCCESS ? add_constant(legacy, type, allocator) : code;
  };
  const common::SqlTypeMeta &meta = value.is_null()
      ? original_type.get_sql_type_meta() : value.get_meta();
  if (simple_constraint_type(meta)) {
    common::SqlType type;
    if (OB_FAIL(common::SqlType::from_legacy(meta, original_type.get_accuracy(), type))) {
    } else { ret = add_prepared(type); }
  } else if (meta.get_type() == common::ObUserDefinedSQLType
      || (meta.is_pl_extend_type()
      && (meta.get_extend_type() != pl::PL_RECORD_TYPE || actual_declarations == nullptr))
      || (!meta.is_pl_extend_type() && (session == nullptr || session->get_cur_exec_ctx() == nullptr))) {
    ret = mark_uncapturable_constraint(value, cache_eligible_);
  } else {
    common::ObArenaAllocator temporary("PCConstType");
    const common::SqlType *type = nullptr;
    if (OB_FAIL(ObExprUtil::resolve_raw_sql_type(original_type, session, schema_guard,
                                               temporary, type, actual_declarations))) {
    } else { ret = add_prepared(*type); }
  }
  return ret;
}

int ObPCConstParamInfo::add_int_constant(int64_t value, common::ObIAllocator &allocator)
{
  common::SqlTypeMeta meta;
  meta.set_int();
  common::SqlType type;
  common::Datum datum;
  datum.set_int(value);
  const int ret = common::SqlType::from_legacy(meta,
      common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType], type);
  return ret == OB_SUCCESS ? add_constant(datum, type, allocator) : ret;
}

int ObPCConstParamInfo::clone_constant(const common::NativeTypedDatum &source,
                                       common::ObIAllocator &allocator,
                                       common::NativeTypedDatum &output)
{
  int ret = OB_SUCCESS;
  common::NativeTypedDatum owned;
  if (source.type_ == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(source.type_->clone_to(allocator, owned.type_))) {
  } else {
    const common::DatumCopyPlan plan(*owned.type_);
    if (OB_FAIL(plan.deep_copy(source.datum_, owned.datum_, allocator))) {
    } else { output = owned; }
  }
  return ret;
}

int ObPCConstParamInfo::compare_constant(const common::NativeTypedDatum &left,
                                         const common::DatumParameter &right,
                                         int &comparison)
{
  int ret = OB_SUCCESS;
  common::DatumValue view;
  alignas(8) char scratch[64];
  if (OB_FAIL(constraint_legacy_view(left, view, scratch))) {
  } else { ret = common::compare_datum_values(view, right.value_view(), comparison); }
  return ret;
}

int ObPCConstParamInfo::equal_constants(const common::NativeTypedDatum &left,
                                        const common::NativeTypedDatum &right, bool &equal)
{
  int ret = OB_SUCCESS;
  common::DatumValue l, r;
  alignas(8) char left_scratch[64], right_scratch[64];
  if (OB_FAIL(constraint_legacy_view(left, l, left_scratch))) {
  } else if (OB_FAIL(constraint_legacy_view(right, r, right_scratch))) {
  } else { ret = common::datum_values_equal(l, r, equal); }
  return ret;
}

int ObPCConstParamInfo::print_constant(const common::NativeTypedDatum &value,
                                       char *buffer, int64_t capacity, int64_t &position)
{
  int ret = OB_SUCCESS;
  common::DatumValue view;
  alignas(8) char scratch[64];
  if (OB_FAIL(constraint_legacy_view(value, view, scratch))) {
  } else { ret = view.print_sql_literal(buffer, capacity, position); }
  return ret;
}

bool ObPCConstParamInfo::operator==(const ObPCConstParamInfo &other) const
{
  bool same = cache_eligible_ && other.cache_eligible_
      && const_idx_.count() == other.const_idx_.count()
      && const_params_.count() == other.const_params_.count();
  for (int64_t i = 0; same && i < const_idx_.count(); ++i) {
    same = const_idx_.at(i) == other.const_idx_.at(i);
  }
  for (int64_t i = 0; same && i < const_params_.count(); ++i) {
    bool equal = false;
    same = OB_SUCCESS == equal_constants(const_params_.at(i), other.const_params_.at(i), equal)
        && equal;
  }
  return same;
}

int ObGetAllCacheIdOp::operator()(common::hash::HashMapPair<ObCacheObjID, ObILibCacheObject *> &entry)
{
  int ret = common::OB_SUCCESS;
  if (NULL == key_array_ || OB_ISNULL(entry.second)) {
    ret = common::OB_NOT_INIT;
    SQL_PC_LOG(WARN, "invalid argument", K(ret));
  } else if ((entry.second->get_ns() >= ObLibCacheNameSpace::NS_CRSR
            && entry.second->get_ns() <= ObLibCacheNameSpace::NS_PKG)
            ||entry.second->get_ns() == ObLibCacheNameSpace::NS_CALLSTMT) {
    if (OB_ISNULL(entry.second)) {
      // do nothing
    } else if (!entry.second->added_lc()) {
      // do nothing
    } else if (OB_FAIL(key_array_->push_back(entry.first))) {
    }
  }
  return ret;
}

int TableAccessTargetBuilder::collect_access_targets(const common::ObIArray<ObTablePartitionInfo *> &partition_infos,
                                           ObIArray<TableAccessTargets> &table_targets)
{
  int ret = OB_SUCCESS;
  int64_t N = partition_infos.count();
  if (OB_FAIL(table_targets.reserve(N))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
    if (OB_ISNULL(partition_infos.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(table_targets.push_back(
                   partition_infos.at(i)->get_access_targets()))) {
    } else { /* do nothing */ }
  }
  return ret;
}
int TableAccessTargetBuilder::collect_access_targets(const ObIArray<TableAccessPlan> &table_access_plans,
                                           const ObPlanCacheCtx &pc_ctx,
                                           ObIArray<TableAccessTargets> &table_targets)
{
  int ret = OB_SUCCESS;
  ObExecContext &exec_ctx = pc_ctx.exec_ctx_;
  const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(pc_ctx.sql_ctx_.session_info_);
  ObPhysicalPlanCtx *plan_ctx = exec_ctx.get_physical_plan_ctx();
  int64_t N = table_access_plans.count();
  if (OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObSEArray<const TableAccessPlan *, 2> table_access_plan_ptrs;
    ObSEArray<TableAccessTargets *, 2> target_ptrs;
    const ObPhysicalPlanCtx &params = *plan_ctx;
    if (OB_FAIL(table_targets.prepare_allocate(N))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
        const TableAccessPlan &table_access_plan = table_access_plans.at(i);
        TableAccessTargets &targets = table_targets.at(i);
        NG_TRACE(calculate_partition_targets_begin);
        if (OB_FAIL(table_access_plan.calculate_tablet_targets(exec_ctx,
                                                          params,
                                                          targets.get_tablets_for_update(),
                                                          dtc_params))) {
        } else {
          NG_TRACE(calculate_partition_targets_end);
          targets.set_table_identity(
              table_access_plan.get_table_id(), table_access_plan.get_ref_table_id());
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(table_access_plan_ptrs.push_back(&table_access_plan))) {
          } else if (OB_FAIL(target_ptrs.push_back(&targets))) {
          }
        }
      } // for end
    }

    if (OB_SUCC(ret) && N!=0 ) {
      if (OB_FAIL(ObLogPlan::validate_local_tablets(exec_ctx,
                                                    table_access_plan_ptrs,
                                                    target_ptrs))) {
      }
    }
  }

  return ret;
}

int TableAccessTargetBuilder::build_runtime_accesses(ObDASCtx &das_ctx,
                                          const ObIArray<TableAccessPlan> &table_access_plans,
                                          const ObIArray<TableAccessTargets> &table_targets)
{
  int ret = OB_SUCCESS;
  CK(table_access_plans.count() == table_targets.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < table_access_plans.count(); i++) {
    if (OB_FAIL(das_ctx.add_table_access_targets(table_access_plans.at(i).get_access_meta(), table_targets.at(i)))) {
    }
  }
  if (OB_FAIL(ret)) {
    das_ctx.clear_table_accesses();
  }

  return ret;
}


//this function will rewrite the related tablet map info in DASCtx
int TableAccessTargetBuilder::build_related_tablet_info(const TableAccessPlan &table_access_plan,
                                                   ObExecContext &exec_ctx,
                                                   DASRelatedTabletMap *&related_map)
{
  int ret = OB_SUCCESS;
  ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(exec_ctx.get_my_session());
  ObPhysicalPlanCtx *plan_ctx = exec_ctx.get_physical_plan_ctx();
  ObArray<ObObjectID> partition_ids;
  ObArray<ObObjectID> first_level_part_ids;
  ObArray<ObTabletID> tablet_ids;

  if (OB_FAIL(table_access_plan.calculate_tablet_ids(exec_ctx,
                                                  *plan_ctx,
                                                  tablet_ids,
                                                  partition_ids,
                                                  first_level_part_ids,
                                                  dtc_params))) {
  } else {
    related_map = &exec_ctx.get_das_ctx().get_related_tablet_map();
    LOG_DEBUG("build_related tablet info", K(tablet_ids), K(partition_ids),
             K(table_access_plan.get_access_meta()), KPC(related_map));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTableRowCount, op_id_, row_count_);

int ObConfigInfoInPC::load_influence_plan_config()
{
  int ret = OB_SUCCESS;
  // Add runtime configuration dependencies here when needed.

  // For Cluster configs
  // here to add value of configs that can influence execution plan.
  enable_px_ordered_coord_ = config::_enable_px_ordered_coord();
  enable_newsort_ = config::_enable_newsort();
  is_strict_defensive_check_ = (config::_enable_defensive_check() == 2);
  bloom_filter_ratio_ = config::_bloom_filter_ratio();
  realistic_runtime_bloom_filter_size_ = !config::_preset_runtime_bloom_filter_size();
  ndv_runtime_bloom_filter_size_ = config::_ndv_runtime_bloom_filter_size();

  // Runtime configuration dependencies.
  // Use the runtime configuration to read the current settings.

  pushdown_storage_level_ = config::_pushdown_storage_level();
  enable_px_batch_rescan_ = config::_enable_px_batch_rescan();
  bloom_filter_enabled_ = config::_bloom_filter_enabled();
  px_join_skew_handling_ = config::_px_join_skew_handling();
  px_join_skew_minfreq_ = static_cast<int8_t>(config::_px_join_skew_minfreq());
  enable_spf_batch_rescan_ = config::_enable_spf_batch_rescan();
  enable_var_assign_use_das_ = config::_enable_var_assign_use_das();
  enable_das_keep_order_ = config::_enable_das_keep_order();
  enable_index_merge_ = config::_enable_index_merge();
  enable_parallel_das_dml_ = config::_enable_parallel_das_dml();
  const rust::String rollup_value = config::_use_hash_rollup();
  const ObString rollup_policy(static_cast<int32_t>(rollup_value.size()), rollup_value.data());
  hash_rollup_policy_ = rollup_policy.case_compare("auto") == 0 ?
                          0 :
                          (rollup_policy.case_compare("forced") == 0 ? 1 : 2);
  enable_distributed_das_scan_ = config::_enable_distributed_das_scan();
  enable_das_batch_rescan_flag_ = config::_enable_das_batch_rescan_flag();
  enable_topn_runtime_filter_ = config::_enable_topn_runtime_filter();
  min_const_integer_precision_ = static_cast<int8_t>(config::_min_const_integer_precision());
  enable_px_task_rebalance_ = config::_enable_px_task_rebalance();


  return ret;
}

// reading values and generate strings
int ObConfigInfoInPC::serialize_configs(char *buf, int buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  pos = 0;

  // gen config str
  if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", pushdown_storage_level_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_px_batch_rescan_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_px_ordered_coord_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", bloom_filter_enabled_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_newsort_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", px_join_skew_handling_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", is_strict_defensive_check_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", px_join_skew_minfreq_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_spf_batch_rescan_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_var_assign_use_das_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_das_keep_order_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", bloom_filter_ratio_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", realistic_runtime_bloom_filter_size_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_parallel_das_dml_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%d,", hash_rollup_policy_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", ndv_runtime_bloom_filter_size_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                               "%d,", enable_index_merge_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_distributed_das_scan_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%ld,", enable_das_batch_rescan_flag_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                              "%d,", enable_topn_runtime_filter_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%d,", min_const_integer_precision_))) {
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%d,", enable_px_task_rebalance_))) {
  } else {
    // do nothing
  }
  // trim last comma
  pos--;
  return ret;
}

}
}
