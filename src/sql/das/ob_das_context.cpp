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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_context.h"
#include "sql/das/ob_das_utils.h"
#include "sql/ob_sql_context.h"
#include "observer/ob_server.h"
namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{

int ObDASCtx::build_local_tablet_loc(uint64_t ref_table_id,
                                    const ObTabletID &tablet_id,
                                    ObDASTabletLoc &tablet_loc)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_virtual_table(ref_table_id))
      && OB_UNLIKELY(tablet_id.id() != 1
                     && tablet_id.id() != EMPTY_VIRTUAL_TABLE_TABLET_ID)) {
    ret = OB_LOCATION_NOT_EXIST;
    LOG_WARN("virtual tablet location does not exist", K(ret), K(ref_table_id), K(tablet_id));
  } else {
    tablet_loc.tablet_id_ = tablet_id;
  }
  save_cur_exec_status(ret);
  return ret;
}

bool ObDASCtx::is_refresh_location_error(int err_no) const
{
  return is_master_changed_error(err_no)
      || is_partition_change_error(err_no)
      || is_get_location_timeout_error(err_no);
}

void ObDASCtx::refresh_location_cache_by_errno(bool is_nonblock, int err_no)
{
  NG_TRACE_TIMES(1, get_location_cache_begin);
  if (is_refresh_location_error(err_no)) {
    force_refresh_location_cache(is_nonblock, err_no);
  }
  NG_TRACE_TIMES(1, get_location_cache_end);
}

void ObDASCtx::force_refresh_location_cache(bool is_nonblock, int err_no)
{
  UNUSED(is_nonblock);
  last_location_errno_ = err_no;
}

void ObDASCtx::set_retry_info(const ObQueryRetryInfo *retry_info)
{
  if (OB_NOT_NULL(retry_info)) {
    last_location_errno_ = retry_info->get_last_query_retry_err();
    history_retry_cnt_ = retry_info->get_retry_cnt();
  }
}

void ObDASCtx::save_cur_exec_status(int err_no)
{
  if (is_refresh_location_error(err_no)) {
    last_location_errno_ = err_no;
  }
}


int ObDASCtx::init(const ObPhysicalPlan &plan, ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
  ObSEArray<ObObjectID, 2> partition_ids;
  ObSEArray<ObObjectID, 2> first_level_part_ids;
  ObSEArray<ObTabletID, 2> tablet_ids;
  ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(ctx.get_my_session());
  const ObIArray<ObTableLocation> &normal_locations = plan.get_table_locations();
  const ObIArray<ObTableLocation> &das_locations = plan.get_das_table_locations();
  set_last_errno(ctx.get_my_session()->get_retry_info().get_last_query_retry_err());
  set_history_retry_cnt(ctx.get_my_session()->get_retry_info().get_retry_cnt());
  for (int64_t i = 0; OB_SUCC(ret) && i < das_locations.count(); ++i) {
    const ObTableLocation &das_location = das_locations.at(i);
    ObDASTableLoc *table_loc = nullptr;
    tablet_ids.reuse();
    partition_ids.reuse();
    first_level_part_ids.reuse();
    if (OB_FAIL(das_location.calculate_tablet_ids(ctx,
                                                  plan_ctx->get_param_store(),
                                                  tablet_ids,
                                                  partition_ids,
                                                  first_level_part_ids,
                                                  dtc_params))) {
      LOG_WARN("calculate partition ids failed", K(ret));
    } else if (OB_FAIL(extended_table_loc(das_location.get_loc_meta(), table_loc))) {
      LOG_WARN("extended table location failed", K(ret));
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < tablet_ids.count(); ++j) {
      ObDASTabletLoc *tablet_loc = nullptr;
      if (OB_FAIL(extended_tablet_loc(*table_loc, tablet_ids.at(j), tablet_loc, partition_ids.at(j),
                                      first_level_part_ids.empty() ? OB_INVALID_ID : first_level_part_ids.at(j)))) {
        LOG_WARN("extended tablet location failed", K(ret));
      }
    }
  }
  LOG_DEBUG("init das context finish", K(ret), K(normal_locations), K(das_locations), K(table_locs_));
  return ret;
}

int ObDASCtx::get_das_tablet_mapper(const uint64_t ref_table_id,
                                    ObDASTabletMapper &tablet_mapper,
                                    const DASTableIDArrayWrap *related_table_ids)
{
  int ret = OB_SUCCESS;

  tablet_mapper.related_info_.related_map_ = &related_tablet_map_;
  tablet_mapper.related_info_.related_tids_ = related_table_ids;

  bool is_vt = is_virtual_table(ref_table_id);
  uint64_t real_table_id = ref_table_id;
  
  if (tablet_mapper.is_non_partition_optimized()) {
    // table ids has calced for no partition entity table, continue
  } else if (!is_vt) {
    //get ObTableSchema object corresponding to the table_id from ObSchemaGetterGuard
    //record the ObTableSchema into tablet_mapper
    //the tablet and partition info come from ObTableSchema in the real table
    ObSchemaGetterGuard *schema_guard = nullptr;
    if (OB_ISNULL(sql_ctx_) || OB_ISNULL(schema_guard = sql_ctx_->schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema guard is nullptr", K(ret), K(sql_ctx_), K(schema_guard));
    } else if (OB_ISNULL(tablet_mapper.table_schema_)
        && OB_FAIL(schema_guard->get_table_schema( real_table_id, tablet_mapper.table_schema_))) {
      LOG_WARN("get table schema failed", K(ret), K(real_table_id));
    } else if (OB_ISNULL(tablet_mapper.table_schema_)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table schema is not found", K(ret), K(real_table_id));
    } else {
      tablet_mapper.related_info_.guard_ = schema_guard;
    }
  } else {
    tablet_mapper.virtual_table_id_ = real_table_id;
  }
  return ret;
}

ObDASTableLoc *ObDASCtx::get_table_loc_by_id(uint64_t table_loc_id, uint64_t ref_table_id)
{
  ObDASTableLoc *table_loc = nullptr;
  FOREACH(tmp_node, table_locs_) {
    if ((*tmp_node)->loc_meta_->table_loc_id_ == table_loc_id &&
        (*tmp_node)->loc_meta_->ref_table_id_ == ref_table_id) {
      table_loc = *tmp_node;
      break;
    }
  }
  return table_loc;
}

int ObDASCtx::extended_tablet_loc(ObDASTableLoc &table_loc,
                                  const ObTabletID &tablet_id,
                                  ObDASTabletLoc *&tablet_loc,
                                  const common::ObObjectID &partition_id,
                                  const common::ObObjectID &first_level_part_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(table_loc.get_tablet_loc_by_id(tablet_id, tablet_loc))) {
    LOG_WARN("get tablet loc failed", KR(ret));
  }
  if (OB_SUCC(ret) && tablet_loc == nullptr) {
    LOG_DEBUG("tablet location is not exists, begin to construct it", K(table_loc), K(tablet_id));
    void *loc_buf = allocator_.alloc(sizeof(ObDASTabletLoc));
    if (OB_ISNULL(loc_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate tablet loc failed", K(ret));
    } else if (OB_ISNULL(tablet_loc = new(loc_buf) ObDASTabletLoc())) {
      //do nothing
    } else if (OB_FAIL(build_local_tablet_loc(table_loc.loc_meta_->ref_table_id_,
                                              tablet_id,
                                              *tablet_loc))) {
      LOG_WARN("nonblock get tablet location failed", K(ret), KPC(table_loc.loc_meta_), K(tablet_id));
    } else if (OB_FAIL(table_loc.add_tablet_loc(tablet_loc))) {
      LOG_WARN("store tablet location info failed", K(ret));
    } else {
      tablet_loc->loc_meta_ = table_loc.loc_meta_;
      tablet_loc->partition_id_ = partition_id;
      tablet_loc->first_level_part_id_ = first_level_part_id;
    }
    //build related tablet location
    if (OB_SUCC(ret) && OB_FAIL(build_related_tablet_loc(*tablet_loc))) {
      LOG_WARN("build related tablet loc failed", K(ret), KPC(tablet_loc), KPC(tablet_loc->loc_meta_));
    }
  }
  return ret;
}

int ObDASCtx::extended_tablet_loc(ObDASTableLoc &table_loc,
                                  const ObCandiTabletLoc &candi_tablet_loc,
                                  ObDASTabletLoc *&tablet_loc)
{
  int ret = OB_SUCCESS;
  const ObOptTabletLoc &opt_tablet_loc = candi_tablet_loc.get_partition_location();
  if (OB_FAIL(table_loc.get_tablet_loc_by_id(opt_tablet_loc.get_tablet_id(), tablet_loc))) {
    LOG_WARN("get tablet loc failed", KR(ret), K(opt_tablet_loc.get_tablet_id()));
  }
  if (OB_SUCC(ret) && tablet_loc == nullptr) {
    void *tablet_buf = allocator_.alloc(sizeof(ObDASTabletLoc));
    if (OB_ISNULL(tablet_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate tablet loc buf failed", K(ret), K(sizeof(ObDASTabletLoc)));
    } else {
      tablet_loc = new(tablet_buf) ObDASTabletLoc();
      tablet_loc->tablet_id_ = opt_tablet_loc.get_tablet_id();
      tablet_loc->partition_id_ = opt_tablet_loc.get_partition_id();
      tablet_loc->first_level_part_id_ = opt_tablet_loc.get_first_level_part_id();
      tablet_loc->loc_meta_ = table_loc.loc_meta_;
      if (OB_FAIL(table_loc.add_tablet_loc(tablet_loc))) {
        LOG_WARN("store tablet loc failed", K(ret), K(tablet_loc));
      }
    }
    //build related tablet location
    if (OB_SUCC(ret) && OB_FAIL(build_related_tablet_loc(*tablet_loc))) {
      LOG_WARN("build related tablet loc failed", K(ret), KPC(tablet_loc), KPC(tablet_loc->loc_meta_));
    }
  }
  return ret;
}

OB_INLINE int ObDASCtx::build_related_tablet_loc(ObDASTabletLoc &tablet_loc)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_loc.loc_meta_->related_table_ids_.count(); ++i) {
    ObTableID related_table_id = tablet_loc.loc_meta_->related_table_ids_.at(i);
    ObDASTableLoc *related_table_loc = nullptr;
    ObDASTabletLoc *related_tablet_loc = nullptr;
    const DASRelatedTabletMap::Value *rv = nullptr;
    void *related_loc_buf = allocator_.alloc(sizeof(ObDASTabletLoc));
    if (OB_ISNULL(related_loc_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate tablet loc failed", K(ret));
    } else if (OB_ISNULL(related_table_loc = get_table_loc_by_id(tablet_loc.loc_meta_->table_loc_id_,
                                                                 related_table_id))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get table loc by id failed", K(ret), KPC(tablet_loc.loc_meta_),
               K(related_table_id), K(table_locs_));
    } else if (OB_ISNULL(rv = related_tablet_map_.get_related_tablet_id(tablet_loc.tablet_id_,
                                                                        related_table_id))) {
      // Related local-index tablet pruning is available only when all operators
      // share the same DAS context.
      // A distributed plan passes tablet_id through an exchange operator,
      //but the related tablet_id map can not be passed by exchange operator,
      //unused related pruning in distributed plan's dml operator,
      //we will use get_all_tablet_and_object_id() to build the related tablet_id map when
      //dml operator's table loc was inited
      if (OB_FAIL(build_related_tablet_map(*tablet_loc.loc_meta_))) {
        LOG_WARN("build related tablet map failed", K(ret), KPC(tablet_loc.loc_meta_));
      } else if (OB_ISNULL(rv = related_tablet_map_.get_related_tablet_id(tablet_loc.tablet_id_,
                                                                          related_table_id))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get related tablet id failed", K(ret),
                 K(tablet_loc.tablet_id_), K(related_table_id), K(related_tablet_map_));
      }
    }
    if (OB_SUCC(ret)) {
      related_tablet_loc = new(related_loc_buf) ObDASTabletLoc();
      related_tablet_loc->tablet_id_ = rv->tablet_id_;
      related_tablet_loc->loc_meta_ = related_table_loc->loc_meta_;
      related_tablet_loc->next_ = tablet_loc.next_;
      related_tablet_loc->partition_id_ = rv->part_id_;
      related_tablet_loc->first_level_part_id_ = rv->first_level_part_id_;
      tablet_loc.next_ = related_tablet_loc;
      if (OB_FAIL(related_table_loc->add_tablet_loc(related_tablet_loc))) {
        LOG_WARN("add related tablet location failed", K(ret));
      }
    }
    LOG_DEBUG("build related tablet loc", K(ret), K(tablet_loc), KPC(related_tablet_loc),
              KPC(tablet_loc.loc_meta_), K(related_table_id));
  }
  return ret;
}

OB_INLINE int ObDASCtx::build_related_table_loc(ObDASTableLoc &table_loc)
{
  int ret = OB_SUCCESS;
  if (!table_loc.loc_meta_->related_table_ids_.empty()) {
    for (DASTabletLocListIter node = table_loc.tablet_locs_begin();
         OB_SUCC(ret) && node != table_loc.tablet_locs_end(); ++node) {
      ObDASTabletLoc *tablet_loc = *node;
      if (OB_FAIL(build_related_tablet_loc(*tablet_loc))) {
        LOG_WARN("build related tablet loc failed", K(ret));
      }
    }
  }
  return ret;
}

int ObDASCtx::extended_table_loc(const ObDASTableLocMeta &loc_meta, ObDASTableLoc *&table_loc)
{
  int ret = OB_SUCCESS;
  table_loc = get_table_loc_by_id(loc_meta.table_loc_id_, loc_meta.ref_table_id_);
  if (nullptr == table_loc) {
    void *loc_buf = nullptr;
    if (OB_ISNULL(loc_buf = allocator_.alloc(sizeof(ObDASTableLoc)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate table loc failed", K(ret), K(sizeof(ObDASTableLoc)));
    } else if (OB_ISNULL(table_loc = new(loc_buf) ObDASTableLoc(allocator_))) {
      //do nothing
    } else if (OB_FAIL(table_locs_.push_back(table_loc))) {
      LOG_WARN("extended table location failed", K(ret));
    } else {
      table_loc->loc_meta_ = &loc_meta;
      LOG_DEBUG("extended table loc", K(loc_meta));
    }
    //to extended related table location
    for (int64_t i = 0; OB_SUCC(ret) && i < loc_meta.related_table_ids_.count(); ++i) {
      ObTableID related_table_id = loc_meta.related_table_ids_.at(i);
      ObDASTableLoc *related_table_loc = nullptr;
      void *related_loc_buf = allocator_.alloc(sizeof(ObDASTableLoc));
      void *loc_meta_buf = allocator_.alloc(sizeof(ObDASTableLocMeta));
      if (OB_ISNULL(related_loc_buf) || OB_ISNULL(loc_meta_buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate table loc failed", K(ret), K(related_loc_buf), K(loc_meta_buf));
      } else if (OB_ISNULL(related_table_loc = new(related_loc_buf) ObDASTableLoc(allocator_))) {
        //do nothing
      } else if (OB_FAIL(table_locs_.push_back(related_table_loc))) {
        LOG_WARN("extended table location failed", K(ret));
      } else {
        ObDASTableLocMeta *related_loc_meta = new(loc_meta_buf) ObDASTableLocMeta(allocator_);
        if (OB_FAIL(loc_meta.init_related_meta(related_table_id, *related_loc_meta))) {
          LOG_WARN("init related meta failed", K(ret), K(related_table_id));
        } else {
          related_table_loc->loc_meta_ = related_loc_meta;
        }
      }
    }
  }
  return ret;
}

int ObDASCtx::add_candi_table_loc(const ObDASTableLocMeta &loc_meta,
                                  const ObCandiTableLoc &candi_table_loc)
{
  int ret = OB_SUCCESS;
  ObDASTableLoc *table_loc = nullptr;
  ObDASTableLocMeta *final_meta = nullptr;
  LOG_DEBUG("das table loc assign begin", K(loc_meta));
  const ObCandiTabletLocIArray &candi_tablet_locs = candi_table_loc.get_phy_part_loc_info_list();
  if (OB_FAIL(ObDASUtils::build_table_loc_meta(allocator_, loc_meta, final_meta))) {
    LOG_WARN("build table loc meta failed", K(ret));
  } else if (OB_FAIL(extended_table_loc(*final_meta, table_loc))) {
    LOG_WARN("extended table loc failed", K(ret), K(loc_meta));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < candi_tablet_locs.count(); ++i) {
    const ObCandiTabletLoc &candi_tablet_loc = candi_tablet_locs.at(i);
    ObDASTabletLoc *tablet_loc = nullptr;
    if (OB_FAIL(extended_tablet_loc(*table_loc, candi_tablet_loc, tablet_loc))) {
      LOG_WARN("extended tablet loc failed", K(ret));
    }
  }
  LOG_DEBUG("das table loc assign finish", K(candi_table_loc), K(loc_meta), K(table_loc->get_tablet_locs()));
  return ret;
}

int ObDASCtx::add_final_table_loc(const ObDASTableLocMeta &loc_meta,
                                  const ObIArray<ObTabletID> &tablet_ids,
                                  const ObIArray<ObObjectID> &partition_ids,
                                  const ObIArray<ObObjectID> &first_level_part_ids)
{
  int ret = OB_SUCCESS;
  ObDASTableLoc *table_loc = nullptr;
  ObDASTableLocMeta *final_meta = nullptr;
  LOG_DEBUG("das table loc assign begin", K(loc_meta));
  if (OB_FAIL(ObDASUtils::build_table_loc_meta(allocator_, loc_meta, final_meta))) {
    LOG_WARN("build table loc meta failed", K(ret));
  } else if (OB_FAIL(extended_table_loc(*final_meta, table_loc))) {
    LOG_WARN("extended table loc failed", K(ret), K(loc_meta));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
    ObDASTabletLoc *tablet_loc = nullptr;
    ObObjectID first_level_part_id =
        first_level_part_ids.empty() ? OB_INVALID_ID : first_level_part_ids.at(i);
    if (OB_FAIL(extended_tablet_loc(*table_loc,
                                    tablet_ids.at(i),
                                    tablet_loc,
                                    partition_ids.at(i),
                                    first_level_part_id))) {
      LOG_WARN("extended tablet loc failed", K(ret));
    }
  }
  LOG_DEBUG("das table loc assign finish", K(loc_meta), K(table_loc->get_tablet_locs()));

  if (OB_FAIL(ret)) {
    clear_all_location_info();
  }
  return ret;
}

int ObDASCtx::build_table_loc_meta(const ObDASTableLocMeta &src,
                                    ObDASTableLocMeta *&dst)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDASUtils::build_table_loc_meta(allocator_, src, dst))) {
    LOG_WARN("build table loc meta failed", K(ret));
  }
  return ret;
}

int64_t ObDASCtx::get_related_tablet_cnt() const
{
  int64_t total_cnt = 0;
  FOREACH(table_node, table_locs_) {
    ObDASTableLoc *table_loc = *table_node;
    total_cnt += table_loc->get_tablet_locs().size();
  }

  return total_cnt;
}

int ObDASCtx::rebuild_tablet_loc_reference()
{
  int ret = OB_SUCCESS;
  FOREACH_X(table_node, table_locs_, OB_SUCC(ret)) {
    ObDASTableLoc *table_loc = *table_node;
    ObTableID table_loc_id = table_loc->loc_meta_->table_loc_id_;
    if (table_loc->rebuild_reference_) {
      //has been rebuild the related table reference, ignore it
      continue;
    } else {
      table_loc->rebuild_reference_ = 1;
    }
    for (int64_t i = 0; i < table_loc->loc_meta_->related_table_ids_.count(); ++i) {
      ObTableID related_table_id = table_loc->loc_meta_->related_table_ids_.at(i);
      ObDASTableLoc *related_table_loc = get_table_loc_by_id(table_loc_id, related_table_id);
      related_table_loc->rebuild_reference_ = 1;
      if (table_loc->get_tablet_locs().size() != related_table_loc->get_tablet_locs().size()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet location count not matched", K(ret),
                 KPC(table_loc), KPC(related_table_loc));
      }
      DASTabletLocList::iterator tablet_iter = table_loc->tablet_locs_begin();
      DASTabletLocList::iterator related_tablet_iter = related_table_loc->tablet_locs_begin();
      for (; OB_SUCC(ret) && tablet_iter != table_loc->tablet_locs_end();
          ++tablet_iter, ++related_tablet_iter) {
        ObDASTabletLoc *tablet_loc = *tablet_iter;
        ObDASTabletLoc *related_tablet_loc = *related_tablet_iter;
        related_tablet_loc->next_ = tablet_loc->next_;
        tablet_loc->next_ = related_tablet_loc;
        LOG_DEBUG("build related reference", KPC(related_tablet_loc), K(related_tablet_loc->next_),
                  KPC(tablet_loc), KPC(table_loc->loc_meta_), KPC(related_table_loc->loc_meta_));
      }
    }
  }
  return ret;
}

int ObDASCtx::build_related_tablet_map(const ObDASTableLocMeta &loc_meta)
{
  int ret = OB_SUCCESS;
  ObDASTabletMapper tablet_mapper;
  ObArray<ObTabletID> tablet_ids;
  ObArray<ObObjectID> partition_ids;
  if (OB_FAIL(get_das_tablet_mapper(loc_meta.ref_table_id_, tablet_mapper, &loc_meta.related_table_ids_))) {
    LOG_WARN("get das tablet mapper failed", K(ret));
  } else if (OB_FAIL(tablet_mapper.get_all_tablet_and_object_id(tablet_ids, partition_ids))) {
    LOG_WARN("build related tablet_id map failed", K(ret), K(loc_meta));
  }
  return ret;
}

int ObDASCtx::find_group_param_by_param_idx(int64_t param_idx,
                                    bool &exist, uint64_t &array_idx)
{
  int ret = OB_SUCCESS;
  exist = false;
  array_idx = OB_INVALID_ID;
  if(OB_ISNULL(group_params_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("group params set by above operator is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < group_params_->count() && !exist; ++i) {
      const GroupRescanParam &group_param = group_params_->at(i);
      if (param_idx == group_param.param_idx_) {
        exist = true;
        array_idx = i;
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObDASCtx)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(table_locs_.size());
  FOREACH_X(tmp_node, table_locs_, OB_SUCC(ret)) {
    ObDASTableLoc *table_loc = *tmp_node;
    OB_UNIS_ENCODE(*table_loc);
    LOG_DEBUG("serialize das table location", K(ret), KPC(table_loc));
  }
  OB_UNIS_ENCODE(flags_);
  OB_UNIS_ENCODE(snapshot_);
  OB_UNIS_ENCODE(write_branch_id_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDASCtx)
{
  int ret = OB_SUCCESS;
  int64_t size = 0;
  OB_UNIS_DECODE(size);
  for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
    ObDASTableLoc *table_loc = nullptr;
    void *table_buf = allocator_.alloc(sizeof(ObDASTableLoc));
    if (OB_ISNULL(table_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate table loc buf failed", K(ret));
    } else {
      table_loc = new(table_buf) ObDASTableLoc(allocator_);
      if (OB_FAIL(table_locs_.push_back(table_loc))) {
        LOG_WARN("store table locs failed", K(ret));
      }
    }
    OB_UNIS_DECODE(*table_loc);
    OX(table_loc->rebuild_reference_ = 0);
    LOG_DEBUG("deserialized das table location", K(ret), KPC(table_loc));
  }
  OB_UNIS_DECODE(flags_);
  OB_UNIS_DECODE(snapshot_);
  if (OB_SUCC(ret) && OB_FAIL(rebuild_tablet_loc_reference())) {
    LOG_WARN("rebuild tablet loc reference failed", K(ret));
  }
  OB_UNIS_DECODE(write_branch_id_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDASCtx)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(table_locs_.size());
  FOREACH(tmp_node, table_locs_) {
    ObDASTableLoc *table_loc = *tmp_node;
    OB_UNIS_ADD_LEN(*table_loc);
  }
  OB_UNIS_ADD_LEN(flags_);
  OB_UNIS_ADD_LEN(snapshot_);
  OB_UNIS_ADD_LEN(write_branch_id_);
  return len;
}
}  // namespace sql
}  // namespace oceanbase
