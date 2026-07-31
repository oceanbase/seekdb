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

#define USING_LOG_PREFIX STORAGE_COMPACTION

#include "storage/ddl/ob_ddl_merge_schedule.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "share/ob_ddl_checksum.h"
#include "storage/scheduler/ob_dag_warning_history_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/ob_ddl_sim_point.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/ddl/ob_ddl_merge_task_utils.h"
#include "storage/ddl/ob_ddl_merge_task_v2.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "storage/compaction/ob_partition_merge_policy.h"

using namespace oceanbase::share::schema;
using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::blocksstable;

namespace oceanbase
{
namespace storage
{

/*
 * For idempotent direct load, both major merge and dump merge need to be checked.
*/
int ObDDLMergeScheduler::check_need_merge_for_idempotent(ObTablet &tablet, ObArray<ObDDLKVHandle> &ddl_kvs, bool &need_schedule_merge, ObDDLKVType &ddl_kv_type)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator arena(ObMemAttr("Ddl_Check_Maj"));
  ObTabletDDLCompleteMdsUserData user_data;
  if (ddl_kv_type != ObDDLKVType::DDL_KV_INVALID || need_schedule_merge) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument, return param should be invalid", K(ret), K(ddl_kv_type), K(need_schedule_merge));
  } else if ((tablet.get_major_table_count() > 0) || 
              tablet.get_tablet_meta().table_store_flag_.with_major_sstable()) {
    LOG_INFO("tablet already exist, not need to merge", K(ret), K(tablet.get_tablet_id()));
  } else {
    /* check need to merge major, first */
    if (OB_FAIL(tablet.get_ddl_complete(share::SCN::max_scn(), arena, user_data))) {
      if (OB_EMPTY_RESULT == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to get ddl complete", K(ret));  
      }
    } else if (user_data.has_complete_ && is_full_direct_load(user_data.direct_load_type_)) {
      need_schedule_merge = true;
      ddl_kv_type = ObDDLKVType::DDL_KV_FULL;
      LOG_INFO("set ddl complete need merge", K(ret), K(user_data));
    }

    /* check need to merge dump */
    if (OB_FAIL (ret)) {
    } else if (need_schedule_merge) {
      /* already found major merge needed, skip dump check */
    } else if (ddl_kvs.empty()) {
      /* ddl kv is empty, skip */
    } else if (ObDDLKVType::DDL_KV_FULL == ddl_kvs.at(0).get_obj()->get_ddl_kv_type()) {
      need_schedule_merge = true;
      ddl_kv_type = ObDDLKVType::DDL_KV_FULL;
      LOG_INFO("ddl kv exist, need merge", K(ret), K(user_data));
    }

    if (OB_SUCC(ret) && need_schedule_merge) {
      /* try create ddl kv mgr, for emtpy table */
      ObDDLKvMgrHandle ddl_kv_mgr_handle;
      if (OB_FAIL(tablet.get_ddl_kv_mgr(ddl_kv_mgr_handle, true /* try create */))) {
        LOG_WARN("failed to get tablet ddl kv mgr", K(ret));
      }
    }
  }
  return ret;
}

/*
 * for idem mode, since start log not exist
 * when restart all observer, before build major and all ddl kvs have been dump
 * ddl kv mgr may not exist and may not schedule merge
 * add new check function to schedule merge, here are need to set type
 * 1. idem sn
 * 2. inc major
*/
int check_full_major_exist(const ObTablet &tablet, bool &full_major_exist)
{
  int ret = OB_SUCCESS;
  full_major_exist = false;
  const ObTabletMeta &tablet_meta = tablet.get_tablet_meta();
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  if (OB_FAIL(tablet.fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fetch table store failed", K(ret));
  } else if (nullptr != table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)) {
    full_major_exist = true;
  }
  return ret;
}

int ObDDLMergeScheduler::check_tablet_need_merge(ObTablet &tablet, ObDDLKvMgrHandle &ddl_kv_mgr_handle, bool &need_schedule_merge, ObDDLKVType &ddl_kv_type)
{
  int ret = OB_SUCCESS;
  bool full_major_exist = false;
  need_schedule_merge = false;
  ObArray<ObDDLKVHandle> ddl_kv_handles;
  if (!ddl_kv_mgr_handle.is_valid()) {
    /* if ddl kv mgr handle is not valid, skip not need to get ddl kvs */
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->get_ddl_kvs(false /* for both frozen & active*/, ddl_kv_handles))) {
    LOG_WARN("failed to get ddl kv", K(ret));
  } else if (OB_FAIL(check_full_major_exist(tablet, full_major_exist))) {
    LOG_WARN("failed to check full major exist", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (!full_major_exist && !need_schedule_merge &&
             OB_FAIL(check_need_merge_for_idempotent(tablet, ddl_kv_handles, need_schedule_merge, ddl_kv_type))) {
    LOG_WARN("failed to check need merge for idem sn", K(ret));
  }
  return ret;
}

int ObDDLMergeScheduler::schedule_ddl_merge(ObLS *ls,
                                            ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  const ObTabletID tablet_id = tablet_handle.is_valid() ? tablet_handle.get_obj()->get_tablet_meta().tablet_id_ : ObTabletID();
  int tmp_ret = OB_SUCCESS;
  bool need_schedule_merge = false;
  ObDDLKVType ddl_kv_type = ObDDLKVType::DDL_KV_INVALID; /* used for decided using which direct load type*/
  if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(tablet_handle));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      LOG_TRACE("kv mgr not exist", K(ret), K(tablet_handle.get_obj()->get_tablet_id()));
      ret = OB_SUCCESS; /* for empty table, ddl kv may not exist*/
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObDDLMergeScheduler::check_tablet_need_merge(*tablet_handle.get_obj(), ddl_kv_mgr_handle, need_schedule_merge, ddl_kv_type))) {
    LOG_WARN("failed to check tablet need merge", K(ret), K(tablet_id));
  } else if (need_schedule_merge) {
    LOG_INFO("need schedule merge", K(ret), K(tablet_id), K(need_schedule_merge), K(ddl_kv_type));
  }

  if (OB_FAIL(ret)) {
  } else if (need_schedule_merge) {
    switch(ddl_kv_type) {
      case ObDDLKVType::DDL_KV_FULL:
        {
          if (OB_FAIL(schedule_tablet_ddl_major_merge(ls, tablet_handle))) {
            if (OB_SIZE_OVERFLOW != ret && OB_EAGAIN != ret) {
              LOG_ERROR("failed to schedule tablet ddl merge", K(ret), K(tablet_id));
            } else {
              LOG_TRACE("schedule ddl major merge failed", K(ret), K(tablet_id));
            }
          }
        }
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ddl kv type", K(ret), K(ddl_kv_type));
        break;
    }
  }
  LOG_TRACE("schedule ddl tablet merge", K(ret), K(tablet_id));
  return ret;
}

/*
*  schedule to build ddl dump/major sstable in share nothing mode
*/
int ObDDLMergeScheduler::schedule_tablet_ddl_major_merge(
    ObLS *ls,
    ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  bool is_major_sstable_exist = false;
  if (OB_UNLIKELY(OB_ISNULL(ls) || !tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(ls), K(tablet_handle));
  }

  if (OB_FAIL(ret)) {
  } else {
    bool has_freezed_ddl_kv = false;
    ObDDLTableMergeDagParam param;
    ObArenaAllocator arena(ObMemAttr("DDL_Mrg_Par"));
    ObTabletDDLCompleteMdsUserData  ddl_complete;
    if (OB_FAIL(ObDDLStorageUtil::is_major_exist(tablet_handle.get_obj()->get_tablet_meta().tablet_id_, is_major_sstable_exist))) {
      LOG_WARN("failed to check major sstable exist", K(ret), K(tablet_handle.get_obj()->get_tablet_meta().tablet_id_));
    } else if (is_major_sstable_exist) {
      LOG_INFO("major sstable already exist, don't need to schdule ddl merge", K(ret), K(tablet_handle.get_obj()->get_tablet_meta().tablet_id_));
    } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
      LOG_WARN("get ddl kv mgr failed", K(ret));
    } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->check_has_freezed_ddl_kv(has_freezed_ddl_kv))) {
      LOG_WARN("check has freezed ddl kv failed", K(ret));
    } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_complete(share::SCN::max_scn(), arena, ddl_complete))) {
      if (OB_EMPTY_RESULT == ret) {
        ret = OB_SUCCESS;
      }
      LOG_WARN("failed to get ddl complete", K(ret), K(tablet_handle.get_obj()->get_tablet_meta().ddl_data_format_version_), K(has_freezed_ddl_kv));
    } 
    
    if (OB_FAIL(ret)) {
    } else if (ddl_complete.has_complete_ || has_freezed_ddl_kv) {
      if (OB_FAIL(ObDirectLoadMgrUtil::generate_merge_param(ddl_complete, *(tablet_handle.get_obj()), param))) {
        LOG_WARN("failed to generate merge param", K(ret), K(ddl_complete));
      } else if (FALSE_IT(param.rec_scn_ = ddl_kv_mgr_handle.get_obj()->get_max_freeze_scn())) {
      } else if (OB_FAIL(compaction::ObScheduleDagFunc::schedule_ddl_table_merge_dag(param))) {
        LOG_WARN("try schedule ddl merge dag failed when ddl kv is full ", K(ret), K(param));
      } else {
        FLOG_INFO("schedule ddl merge task", K(ret), K(tablet_handle.get_obj()->get_tablet_id()), K(param));
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
