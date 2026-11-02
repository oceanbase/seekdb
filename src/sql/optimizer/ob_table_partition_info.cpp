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

#define USING_LOG_PREFIX SQL_OPT

#include "ob_table_partition_info.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace sql
{

int ObTablePartitionInfo::assign(const ObTablePartitionInfo &other)
{
  int ret = OB_SUCCESS;
  table_location_ = other.table_location_;
  if (OB_FAIL(candi_table_loc_.assign(other.candi_table_loc_))) {
    LOG_WARN("fail to assign candi_table_loc_", K(ret), K(candi_table_loc_));
  }
  return ret;
}

int ObTablePartitionInfo::init_table_location(ObSqlSchemaGuard &schema_guard,
                                              const ObDMLStmt &stmt,
                                              ObExecContext *exec_ctx,
                                              const ObIArray<ObRawExpr*> &filter_exprs,
                                              const uint64_t table_id,
                                              const uint64_t ref_table_id,
                                              const ObIArray<ObObjectID> *part_ids,
                                              const common::ObDataTypeCastParams &dtc_params,
                                              bool is_dml_table,
                                              ObIArray<ObRawExpr *> *sort_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_location_.init_location(&schema_guard,
                                        stmt,
                                        exec_ctx,
                                        filter_exprs,
                                        table_id,
                                        ref_table_id,
                                        part_ids,
                                        dtc_params,
                                        is_dml_table,
                                        sort_exprs))) {
      LOG_WARN("fail to init table location", K(ret));
    }
  }
  // Judge and set whether it is a copy table
  if (OB_SUCC(ret)) {
    const ObTableSchema *table_schema = NULL;
    if (OB_FAIL(schema_guard.get_table_schema(table_id, ref_table_id, &stmt, table_schema))) {
      LOG_WARN("fail to get table schema", K(ref_table_id), K(ret));
    } else if (table_schema->is_duplicate_table()) {
      // If the replication table itself has been modified, only leader can be selected, do not set duplicate table attribute
      candi_table_loc_.set_duplicate_type(is_dml_table ? ObDuplicateType::DUPLICATE_IN_DML :
                                                               ObDuplicateType::DUPLICATE);
    }
  }
  return ret;
}

int ObTablePartitionInfo::get_not_insert_dml_part_sort_expr(const ObDMLStmt &stmt,
                                                            ObIArray<ObRawExpr*> *sort_exprs) const
{
  return table_location_.get_not_insert_dml_part_sort_expr(stmt, sort_exprs);
}

int ObTablePartitionInfo::calculate_phy_table_location_info(
                          ObExecContext &exec_ctx,
                          const ParamStore &params,
                          const common::ObDataTypeCastParams &dtc_params)
{
  int ret = OB_SUCCESS;
  ObCandiTableLoc candi_table_loc;
  if (OB_FAIL(table_location_.calculate_candi_tablet_locations(
                       exec_ctx,
                       params,
                       candi_table_loc_.get_phy_part_loc_info_list_for_update(),
                       dtc_params))) {
    LOG_WARN("Failed to calculate table location", K(ret));
  } else {
    candi_table_loc_.set_table_location_key(
        table_location_.get_table_id(), table_location_.get_ref_table_id());
  }
  return ret;
}
// Select all as leader, and set direction
int ObTablePartitionInfo::calc_phy_table_loc_and_select_leader(ObExecContext &exec_ctx,
                                                               const ParamStore &params,
                                                               const common::ObDataTypeCastParams &dtc_params)
{
  int ret = OB_SUCCESS;
  bool is_on_same_server = true;
  ObAddr same_server;
  if (OB_FAIL(calculate_phy_table_location_info(exec_ctx,
                                                params,
                                                dtc_params))) {
    LOG_WARN("fail to calculate phy table location info", K(ret));
  } else if (OB_FAIL(candi_table_loc_.all_select_leader(is_on_same_server, same_server))) {
    LOG_WARN("fail to all select leader", K(ret), K(candi_table_loc_));
    // 
    //
    // Consider the scenario without a leader, all_select_leader will definitely fail
    // Cause optimize failure. Optimize failure will not enter the execution phase, consequently
    // Causes no available retry information to be recorded.
    //
    // So: add the current info to the exec ctx, so there is a chance to retry refreshing
    //
    ObCandiTableLoc candi_table_loc;
    ObTaskExecutorCtx *task_exec_ctx = GET_TASK_EXECUTOR_CTX(exec_ctx);
    int tmp_ret = OB_SUCCESS;
    if (OB_ISNULL(task_exec_ctx)) {
      // don't overwirte err code
      tmp_ret = OB_ERR_UNEXPECTED;
      LOG_WARN("task_exec_ctx not inited", K(tmp_ret));
    } else if (OB_SUCCESS != (tmp_ret = candi_table_loc.assign(candi_table_loc_))) {
      LOG_WARN("fail to assign", K(tmp_ret), K(candi_table_loc_));
    } else {
      ObCandiTabletLocIArray &info_array = candi_table_loc.get_phy_part_loc_info_list_for_update();

      for (int64_t i = 0; i < info_array.count() && OB_SUCCESS == tmp_ret; i++) {
        ObCandiTabletLoc &info = info_array.at(i);
        if (info.get_partition_location().get_replica_locations().count() <= 0) {
          //nothing todo
        } else if (OB_SUCCESS != (tmp_ret = info.set_selected_replica_idx(0))) {
          LOG_WARN("fail to set selected replica index", KR(ret));
        }
      }

      if (OB_SUCCESS != tmp_ret) {
        //nothing todo
      } else if (OB_SUCCESS != (tmp_ret = task_exec_ctx
                         ->append_table_location(candi_table_loc))) {
        LOG_WARN("fail append table locaion info", K(ret), K(tmp_ret));
      }
    }
  }
  return ret;
}

//For the case of local index and lookup,
//because the ObTableLocation generated by the optimizer retains the table_id of the main table,
//and the executor needs to calculate the tablet_id according to the table_id of the local index,
//the ref_table_id in the ObTableLocation needs to be replaced eventually
int ObTablePartitionInfo::replace_final_location_key(ObExecContext &exec_ctx,
                                                     uint64_t ref_table_id,
                                                     bool is_local_index)
{
  int ret = OB_SUCCESS;
  bool is_das_dyn_prune_part = table_location_.use_das() && table_location_.get_has_dynamic_exec_param();
  if (table_location_.get_ref_table_id() != ref_table_id) {
    if (is_local_index && !is_das_dyn_prune_part) {
      //only use to calc local index and main table related tablet info
      //for the local index lookup,
      //need to use the local index id as the table_id for partition calculation
      ObDASTableLocMeta &loc_meta = table_location_.get_loc_meta();
      DASRelatedTabletMap *related_map = nullptr;

      loc_meta.related_table_ids_.reset();
      loc_meta.related_table_ids_.set_capacity(1);
      ref_table_id = ref_table_id;
      if (OB_FAIL(loc_meta.related_table_ids_.push_back(ref_table_id))) {
        LOG_WARN("store related table ids failed", K(ret));
      } else if (OB_FAIL(ObPhyLocationGetter::build_related_tablet_info(table_location_, exec_ctx, related_map))) {
        LOG_WARN("build related tablet info failed", K(ret));
      } else if (OB_FAIL(candi_table_loc_.replace_local_index_loc(*related_map, ref_table_id))) {
        LOG_WARN("replace local index loc failed", K(ret));
      } else if (OB_FAIL(table_location_.replace_ref_table_id(ref_table_id, exec_ctx))) {
        LOG_WARN("replace ref table id failed", K(ret));
      }
      //need to clear the related info in location meta
      loc_meta.related_table_ids_.reset();
      if (nullptr != related_map) {
        related_map->clear();
        related_map = nullptr;
      }
    } else {
      //get the real table location info use the real table id
      if (OB_FAIL(table_location_.replace_ref_table_id(ref_table_id, exec_ctx))) {
        LOG_WARN("replace ref table id failed", K(ret), K(ref_table_id));
      } else if (!is_das_dyn_prune_part) {
        candi_table_loc_.set_table_location_key(table_location_.get_table_id(), ref_table_id);
      }
    }
    LOG_TRACE("replace final location info", K(table_location_.get_loc_meta()), K(ref_table_id), K(candi_table_loc_));
  }
  return ret;
}

int ObTablePartitionInfo::get_location_type(const common::ObAddr &server, ObTableLocationType &type) const
{
  const ObCandiTabletLocIArray &phy_part_loc_info_list =
      candi_table_loc_.get_phy_part_loc_info_list();
  return table_location_.get_location_type(server, phy_part_loc_info_list, type);
}

int ObTablePartitionInfo::get_all_servers(ObIArray<common::ObAddr> &servers) const
{
	int ret = OB_SUCCESS;
  if (OB_FAIL(candi_table_loc_.get_all_servers(servers))) {
    LOG_WARN("failed to get all servers", K(ret));
  } else { /* do nothing */ }
  return ret;
}

} // namespace sql
} // namespace oceanbase
