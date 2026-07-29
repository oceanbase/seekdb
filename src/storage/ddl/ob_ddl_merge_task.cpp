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

#include "observer/ob_tablet_runtime_meta_updater.h" // for ObTabletRuntimeMetaUpdater
#include "storage/ddl/ob_ddl_storage_util.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "share/ob_ddl_checksum.h"
#include "observer/scheduler/ob_dag_warning_history_mgr.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/ob_ddl_sim_point.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#include "storage/ddl/ob_ddl_merge_task_utils.h"
#include "storage/ddl/ob_ddl_merge_task_v2.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "storage/compaction/ob_partition_merge_policy.h"
#include "storage/ddl/ob_ddl_merge_schedule.h"

using namespace oceanbase::observer;
using namespace oceanbase::share::schema;
using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::blocksstable;

namespace oceanbase
{
using namespace transaction;
namespace storage
{
/******************             ObDDLTableMergeDag             *****************/
ObDDLTableMergeDag::ObDDLTableMergeDag()
  : ObIDag(ObDagType::DAG_TYPE_DDL_KV_MERGE),
    is_inited_(false),
    arena_(ObMemAttr("ddl_mrg_dag")),
    ddl_param_(),
    tablet_ctx_(nullptr)
{
}

void ObDDLTableMergeDag::reset_tablet_ctx()
{
  /* only idem type for dump sstable need clean storage schema action */
  if (nullptr != tablet_ctx_) {
    if (is_idem_type(ddl_param_.direct_load_type_) && !ddl_param_.is_commit_) {
      if (nullptr != tablet_ctx_->tablet_param_.storage_schema_) {
        ObTabletObjLoadHelper::free(arena_, tablet_ctx_->tablet_param_.storage_schema_);
        tablet_ctx_->tablet_param_.storage_schema_ = nullptr;
      }
    }
    /* for both dump & major, schema should be set as nullptr
     * storage schema life time rely on dag, don't need release by allocator
    */
    tablet_ctx_->tablet_param_.storage_schema_ = nullptr;
    /* only storage schema & merge_ctx is used
     * not need too release other struct
    */
    tablet_ctx_->merge_ctx_.~MergeCtx();
    arena_.free(tablet_ctx_);
    tablet_ctx_ = nullptr;
    arena_.reset();
  }
}

ObDDLTableMergeDag::~ObDDLTableMergeDag()
{
  reset_tablet_ctx();
}

int ObDDLTableMergeDag::init_by_param(const share::ObIDagInitParam *param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(ddl_param_));
  } else if (OB_ISNULL(param) || !param->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), KP(param));
  } else {
    set_max_retry_times(DDL_MGR_RETRY_TIMES);
    if (OB_FAIL(ddl_param_.assign(*static_cast<const ObDDLTableMergeDagParam *>(param)))) {
      LOG_WARN("failed to assign val", K(ret));
    }
    is_inited_ = true;
  }
  return ret;
}

/* check allow schedule major merge, by ls status
 * if tablet is sstable is not complete, then set the task as dump merge task
 * instead of major merge task
*/
int ObDDLTableMergeDag::check_allow_major_merge()
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(ddl_param_.tablet_id_, tablet_handle))) {
    LOG_WARN("failed to get tablet handle", K(ret), K(ddl_param_));
  } else if (!tablet_handle.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet handle is invalid", K(ret), K(ddl_param_));
  } else if (ddl_param_.is_commit_) {
    if (!tablet_handle.get_obj()->get_tablet_meta().local_status_.check_allow_read()) {
      ddl_param_.is_commit_ = false;
      LOG_INFO("status not full change to dump task", K(ret));
    }
  }
  return ret;
}

int ObDDLTableMergeDag::init_tablet_ctx()
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  ObTabletHandle tablet_handle;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(ddl_param_.tablet_id_, tablet_handle))) {
    LOG_WARN("failed to get tablet handle", K(ret), K(ddl_param_));
  } else if (tablet_ctx_ != nullptr) {
    LOG_INFO("tablet ctx already inited", K(ret));
  } else if (OB_ISNULL(buf = static_cast<char*>(arena_.alloc(sizeof(ObDDLTabletContext))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret));
  } else if (FALSE_IT(tablet_ctx_ = new (buf) ObDDLTabletContext())) {
  } else {
    tablet_ctx_->tablet_id_   = ddl_param_.tablet_id_;

    /* only sn major merge need to load storage schema from user data
     * otherwise, load from cur tablet
    */
    if (OB_FAIL(tablet_ctx_->merge_ctx_.init(ddl_param_.direct_load_type_))) {
      LOG_WARN("failed to get merge helper", K(ret));
    } else if (ddl_param_.is_commit_ &&
               ddl_param_.direct_load_type_ == IDEM_DIRECT_LOAD_DDL) {
      tablet_ctx_->tablet_param_.storage_schema_ = &ddl_param_.user_data_.storage_schema_;
    } else {
      if (OB_FAIL(tablet_handle.get_obj()->load_storage_schema(arena_, tablet_ctx_->tablet_param_.storage_schema_))) {
        LOG_WARN("failed to load storage schema", K(ret));
      }
    }
  }
  return ret;
}
/*
* 1. for idem type full direct load mgr，since data & lob tablet has each mds info, can schedule merge independently
* 2. for non idem type full direct load mgr, must schedule lob major merge first since both of them use the same commit log
*/

int ObDDLTableMergeDag::create_first_task()
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = share::g_mp->ls_service();
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObArray<ObDDLKVHandle> ddl_kvs_handle;
  if (is_idem_type(ddl_param_.direct_load_type_)) {
    // Decide whether the local tablet is ready for a full major merge.
    if (OB_FAIL(check_allow_major_merge())) {
      LOG_WARN("failed to check allow major merge", K(ret));
    } else {
      /* create ddl merge task for new dag path*/
      ObDDLTaskParam task_param;
      ObDDLTabletMergeDagParamV2 ddl_param_v2;
      ObDDLMergePrepareTask *ddl_merge_task = nullptr;
      task_param.data_format_version_ = ddl_param_.data_format_version_;
      task_param.snapshot_version_    = ddl_param_.snapshot_version_;
      /* init tablet context
       * cleanup tablet ctx to avoid invalid val in retry
      */
      reset_tablet_ctx();
      if (OB_FAIL(init_tablet_ctx())) {
        LOG_WARN("failed to init tablet ctx", K(ret));
      } else if (OB_FAIL(ddl_param_v2.init(ddl_param_.is_commit_,
                                    false /*for lob*/,
                                    true /* for replay*/,
                                    ddl_param_.start_scn_,
                                    ddl_param_.direct_load_type_,
                                    task_param,
                                    tablet_ctx_))) {
        LOG_WARN("failed to init ddl param", K(ret));
      } else if (OB_FAIL(create_task(nullptr /* parent task */, ddl_merge_task, ddl_param_v2))) {
        LOG_WARN("failed to create task", K(ret));
      }
    }
  } else {
    ObDDLTableMergeTask *merge_task = nullptr;
    if (OB_FAIL(ls_service->get_ls(ls))) {
      LOG_WARN("get ls failed", K(ret), K(ddl_param_));
    } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls,
                                               ddl_param_.tablet_id_,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_ALL_COMMITED))) {
      LOG_WARN("get tablet failed", K(ret), K(ddl_param_));
    } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected err", K(ret), K(ddl_param_));
    } else if (OB_FAIL(prepare_ddl_kvs(*tablet_handle.get_obj(), ddl_kvs_handle))) {
      LOG_WARN("fail to prepare load ddl kvs", K(ret));
    } else if (OB_FAIL(alloc_task(merge_task))) {
      LOG_WARN("Fail to alloc task", K(ret), K(ddl_param_));
    } else if (OB_FAIL(merge_task->init(ddl_param_, ddl_kvs_handle))) {
      LOG_WARN("failed to init ddl table merge task", K(ret), K(*this));
    } else if (OB_FAIL(add_task(*merge_task))) {
      LOG_WARN("Fail to add task", K(ret), K(ddl_param_));
    }
  }
  return ret;
}

int ObDDLTableMergeDag::prepare_ddl_kvs(ObTablet &tablet, ObIArray<ObDDLKVHandle> &ddl_kvs_handle)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(prepare_full_direct_load_ddl_kvs(tablet, ddl_kvs_handle))) {
    LOG_WARN("fail to prepare ddl kvs", K(ret));
  }
  return ret;
}

int ObDDLTableMergeDag::prepare_full_direct_load_ddl_kvs(ObTablet &tablet, ObIArray<ObDDLKVHandle> &ddl_kvs_handle)
{
  int ret = OB_SUCCESS;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ddl_kvs_handle.reset();
  if (OB_FAIL(tablet.get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TASK_EXPIRED;
      LOG_INFO("ddl kv mgr not exist", K(ret), K(ddl_param_));
    } else {
      LOG_WARN("get ddl kv mgr failed", K(ret), K(ddl_param_));
    }
  } else if (ddl_param_.start_scn_ < tablet.get_tablet_meta().ddl_start_scn_) {
    ret = OB_TASK_EXPIRED;
    LOG_WARN("ddl task expired, skip it", K(ret), K(ddl_param_), "new_start_scn", tablet.get_tablet_meta().ddl_start_scn_);
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->get_ddl_kvs(true/*frozen_only*/, ddl_kvs_handle))) {
    LOG_WARN("get freezed ddl kv failed", K(ret), K(ddl_param_));
  }
  return ret;
}

int ObDDLTableMergeDag::inner_reset_status_for_retry()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_FAIL(create_first_task())) {
    LOG_WARN("failed to create first task", K(ret));
  } else {
    FLOG_INFO("ddl merge batch execute dag retry", K(ret), KPC(this));
  }
  FLOG_INFO("[DDL_MRG_TASK] retry ddl merge task", K(ret), KPC(this));
  return ret;
}

bool ObDDLTableMergeDag::operator == (const ObIDag &other) const
{
  bool is_same = true;
  if (this == &other) {
  } else if (get_type() != other.get_type()) {
    is_same = false;
  } else {
    const ObDDLTableMergeDag &other_dag = static_cast<const ObDDLTableMergeDag&> (other);
    // each tablet has max 1 dag in running, so that the compaction task is unique and no need to consider concurrency
    is_same = ddl_param_.tablet_id_ == other_dag.ddl_param_.tablet_id_
      && ddl_param_.direct_load_type_ == other_dag.ddl_param_.direct_load_type_;
  }
  return is_same;
}

uint64_t ObDDLTableMergeDag::hash() const
{
  return ddl_param_.tablet_id_.hash();
}

int ObDDLTableMergeDag::fill_info_param(compaction::ObIBasicInfoParam *&out_param, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLTableMergeDag has not been initialized", K(ret));
  } else if (OB_FAIL(ADD_DAG_WARN_INFO_PARAM(out_param, allocator, get_type(),
                                  static_cast<int64_t>(ddl_param_.tablet_id_.id()),
                                  static_cast<int64_t>(ddl_param_.rec_scn_.get_val_for_inner_table_field()),
                                  "is_commit", ddl_param_.is_commit_))) {
    LOG_WARN("failed to fill info param", K(ret));
  }
  return ret;
}

int ObDDLTableMergeDag::fill_dag_key(char *buf, const int64_t buf_len) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(databuff_printf(buf, buf_len, "ddl table merge task: tablet_id=%ld, rec_scn=%lu",
                              ddl_param_.tablet_id_.id(), ddl_param_.rec_scn_.get_val_for_inner_table_field()))) {
    LOG_WARN("fill dag key for ddl table merge dag failed", K(ret), K(ddl_param_));
  }
  return ret;
}

bool ObDDLTableMergeDag::ignore_warning()
{
  return OB_LS_NOT_EXIST == dag_ret_
    || OB_TABLET_NOT_EXIST == dag_ret_
    || OB_TASK_EXPIRED == dag_ret_
    || OB_EAGAIN == dag_ret_
    || OB_NEED_RETRY == dag_ret_;
}

ObDDLTableMergeTask::ObDDLTableMergeTask()
  : ObITask(ObITaskType::TASK_TYPE_DDL_KV_MERGE),
    is_inited_(false), merge_param_()
{

}

ObDDLTableMergeTask::~ObDDLTableMergeTask()
{
}

int ObDDLTableMergeTask::init(const ObDDLTableMergeDagParam &ddl_dag_param, const ObIArray<ObDDLKVHandle> &frozen_ddl_kvs)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(merge_param_));
  } else if (OB_UNLIKELY(!ddl_dag_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_dag_param));
  } else if (OB_FAIL(frozen_ddl_kvs_.assign(frozen_ddl_kvs))) {
    LOG_WARN("assign ddl kv handle array failed", K(ret), K(frozen_ddl_kvs.count()));
  } else if (OB_FAIL(merge_param_.assign(ddl_dag_param))) {
    LOG_WARN("failed to assign val", K(ret), K(ddl_dag_param));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int wait_lob_tablet_major_exist(const ObDirectLoadType &direct_load_type, ObLS *ls, ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  ObTabletBindingMdsUserData ddl_data;
  const ObTabletMeta &tablet_meta = tablet.get_tablet_meta();
  ObDirectLoadMgr *direct_load_mgr = share::g_mp->direct_load_mgr();
  ObTabletDirectLoadMgrHandle direct_load_mgr_handle;
  ObDDLTableMergeDagParam param;
  bool is_major_sstable_exist = false;
  ObArenaAllocator allocator(ObMemAttr("Ddl_Com_WMaj"));
  ObTabletDDLCompleteMdsUserData ddl_complete;
  if (OB_FAIL(tablet.ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), ddl_data))) {
    LOG_WARN("failed to get ddl data from tablet", K(ret), K(tablet_meta));
  } else if (ddl_data.lob_meta_tablet_id_.is_valid()) {
    ObTabletHandle lob_tablet_handle;
    const ObTabletID lob_tablet_id = ddl_data.lob_meta_tablet_id_;
    if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, lob_tablet_id, lob_tablet_handle, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
      LOG_WARN("get lob tablet handle failed", K(ret), K(lob_tablet_id));
    } else if (is_idem_type(direct_load_type)) {
      if (OB_FAIL(lob_tablet_handle.get_obj()->get_ddl_complete(share::SCN::max_scn(), allocator, ddl_complete))) {
        LOG_WARN("failed to get ddl complete");
      } else if (!ddl_complete.has_complete_) {
        ret = OB_EAGAIN;
        LOG_WARN("ddl not complete", K(ret), K(direct_load_type));
      }
    }

    if (OB_FAIL(ret)) {
    } else {
      bool is_major_sstable_exist = lob_tablet_handle.get_obj()->get_major_table_count() > 0
        || lob_tablet_handle.get_obj()->get_tablet_meta().table_store_flag_.with_major_sstable();
      if (!is_major_sstable_exist) {
        ret = OB_EAGAIN;
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(ObDDLMergeScheduler::schedule_tablet_ddl_major_merge(ls, lob_tablet_handle))) {
          LOG_WARN("schedule ddl major merge for lob tablet failed", K(tmp_ret), K(lob_tablet_id));
        }
      }
    }
  }
  return ret;
}

int ObDDLTableMergeTask::process()
{
  int ret = OB_SUCCESS;
  LOG_INFO("ddl merge task start process", K(*this), "ddl_event_info", ObDDLEventInfo());
  ObLSService *ls_service = share::g_mp->ls_service();
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("get ls failed", K(ret), K(merge_param_));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls,
                                               merge_param_.tablet_id_,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_ALL_COMMITED))) {
    LOG_WARN("get tablet failed", K(ret), K(merge_param_));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(merge_param_));
  } else if (is_idem_type(merge_param_.direct_load_type_) && merge_param_.is_commit_ &&
             !tablet_handle.get_obj()->get_tablet_meta().local_status_.check_allow_read()) {
    LOG_INFO("skip since tablet not allow read", K(ret), K(merge_param_));
  } else if (OB_FAIL(merge_ddl_kvs(ls, *(tablet_handle.get_obj())))) {
    LOG_WARN("fail to merge ddl kvs", K(ret));
  }
  return ret;
}

int ObDDLTableMergeTask::merge_ddl_kvs(ObLS *ls, ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(merge_full_direct_load_ddl_kvs(ls, tablet))) {
    LOG_WARN("fail to merge ddl kvs", K(ret));
  }
  return ret;
}

int ObDDLTableMergeTask::check_macro_intergrate_for_nidem_sn(ObTabletDDLParam &ddl_param,
                                                             ObTablet &tablet,
                                                             SCN &compact_start_scn,
                                                             SCN &compact_end_scn)
{
  int ret = OB_SUCCESS;
  ObTableStoreIterator ddl_table_iter;
  if (OB_FAIL(tablet.get_ddl_sstables(ddl_table_iter))) {
    LOG_WARN("get ddl sstable handles failed", K(ret));
  } else if (merge_param_.start_scn_ > SCN::min_scn() && merge_param_.start_scn_ < ddl_param.start_scn_) {
    ret = OB_TASK_EXPIRED;
    LOG_INFO("ddl merge task expired, do nothing", K(merge_param_), "new_start_scn", ddl_param.start_scn_);
  } else if (OB_FAIL(ObTabletDDLUtil::get_compact_scn(ddl_param.start_scn_, ddl_table_iter, frozen_ddl_kvs_, compact_start_scn, compact_end_scn))) {
    LOG_WARN("get compact scn failed", K(ret), K(merge_param_), K(ddl_param), K(ddl_table_iter), K(frozen_ddl_kvs_));
  } else if (ddl_param.commit_scn_.is_valid_and_not_min() && compact_end_scn > ddl_param.commit_scn_) {
    ret = OB_ERR_SYS;
    LOG_WARN("compact end scn is larger than commit scn", K(ret), K(ddl_param), K(compact_end_scn), K(frozen_ddl_kvs_), K(ddl_table_iter));
  }
  return ret;
}

int prepare_ddl_param_for_nidem_sn(const ObDDLTableMergeDagParam &merge_param, ObTabletDDLParam &ddl_param)
{
  int ret = OB_SUCCESS;
  ObDirectLoadMgr *direct_load_mgr = share::g_mp->direct_load_mgr();
  ObTabletDirectLoadMgrHandle tablet_mgr_hdl;

  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("merge param is invalid", K(ret));
  } else if (OB_ISNULL(direct_load_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("direct_load_mgr should not be null", K(ret));
  } else if (OB_FAIL(direct_load_mgr->get_tablet_mgr(ObTabletDirectLoadMgrKey(merge_param.tablet_id_, ObDirectLoadType::DIRECT_LOAD_DDL),
                                                     tablet_mgr_hdl))) {
    LOG_WARN("get tablet direct load mgr failed", K(ret), K(merge_param));
  } else if (OB_FAIL(tablet_mgr_hdl.get_full_obj()->prepare_major_merge_param(ddl_param))) {
    LOG_WARN("preare full direct load sstable param failed", K(ret));
  }
  return ret;
}

int prepare_ddl_param_for_idem_sn(const ObDDLTableMergeDagParam &merge_param, ObTabletDDLParam &ddl_param)
{
  int ret = OB_SUCCESS;
  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arugment", K(ret), K(merge_param));
  } else if (!is_idem_type(merge_param.direct_load_type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("only support diem direct load type", K(ret));
  } else {
    ddl_param.direct_load_type_    = merge_param.direct_load_type_;
    ddl_param.table_key_           = merge_param.table_key_;
    ddl_param.start_scn_           = merge_param.start_scn_;
    ddl_param.commit_scn_          = merge_param.rec_scn_;
    ddl_param.snapshot_version_    = merge_param.table_key_.get_snapshot_version();
    ddl_param.data_format_version_ = merge_param.data_format_version_;
  }
  return ret;
}

int prepare_full_direct_load_ddl_param(const ObDDLTableMergeDagParam &merge_param, ObTabletDDLParam &ddl_param)
{
  int ret = OB_SUCCESS;
  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_param));
  } else if (!is_full_direct_load(merge_param.direct_load_type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inc direct load is not supported", K(ret), K(merge_param));
  } else if (is_idem_type(merge_param.direct_load_type_) && OB_FAIL(prepare_ddl_param_for_idem_sn(merge_param, ddl_param))) {
    LOG_WARN("failed to set merge param", K(ret));
  } else if (!is_idem_type(merge_param.direct_load_type_) && OB_FAIL(prepare_ddl_param_for_nidem_sn(merge_param, ddl_param))) {
    LOG_WARN("failed to set merge param", K(ret));
  }
  return ret;
}

int ObDDLTableMergeTask::merge_full_direct_load_ddl_kvs(ObLS *ls, ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  if (!is_full_direct_load(merge_param_.direct_load_type_)) {
    LOG_WARN("func can only be used for full direct load", K(ret), K(merge_param_));
  } else if (OB_FAIL(merge_full_direct_load_ddl_kvs_for_sn(ls, tablet))) {
    LOG_WARN("failed to merge full direct load", K(ret));
  }
  return ret;
}

int ObDDLTableMergeTask::merge_full_direct_load_ddl_kvs_for_sn(ObLS *ls, ObTablet &tablet)
{
  int ret = OB_SUCCESS;
  ObTableStoreIterator ddl_table_iter;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  common::ObArenaAllocator allocator("DDLMergeTask", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObTableHandleV2 old_sstable_handle;
  ObTableHandleV2 compacted_sstable_handle;
  ObSSTable *sstable = nullptr;
  bool is_major_exist = false;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;

  if (OB_FAIL(tablet.get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TASK_EXPIRED;
      LOG_INFO("ddl kv mgr not exist", K(ret), K(merge_param_));
    } else {
      LOG_WARN("get ddl kv mgr failed", K(ret), K(merge_param_));
    }
  } else if (OB_FAIL(tablet.get_ddl_sstables(ddl_table_iter))) {
    LOG_WARN("get ddl sstable handles failed", K(ret));
  } else {
    DEBUG_SYNC(BEFORE_DDL_TABLE_MERGE_TASK);
#ifdef ERRSIM
    if (GCONF.errsim_test_tablet_id.get_value() > 0 && merge_param_.tablet_id_.id() == GCONF.errsim_test_tablet_id.get_value()) {
      LOG_INFO("test tablet ddl merge start", K(ret), K(merge_param_));
      DEBUG_SYNC(BEFORE_LOB_META_TABELT_DDL_MERGE_TASK);
    }
#endif
    ObTabletDDLParam ddl_param;
    bool is_data_complete = false;
    const ObSSTable *first_major_sstable = nullptr;
    SCN compact_start_scn, compact_end_scn;
    if (OB_FAIL(ObTabletDDLUtil::check_and_get_major_sstable(
        merge_param_.tablet_id_, first_major_sstable, table_store_wrapper))) {
      LOG_WARN("check if major sstable exist failed", K(ret));
    } else if (nullptr != first_major_sstable) {
      is_major_exist = true;
      LOG_INFO("major sstable has been created before", K(merge_param_));
    } else if (tablet.get_tablet_meta().table_store_flag_.with_major_sstable()) {
      ret = OB_TASK_EXPIRED;
      LOG_INFO("tablet metadata records a major table but no local major table exists, skip");
    } else if (merge_param_.is_commit_ && OB_FAIL(wait_lob_tablet_major_exist(merge_param_.direct_load_type_, ls, tablet))) {
      if (OB_EAGAIN != ret) {
        LOG_WARN("wait lob tablet major sstable exist faild", K(ret), K(merge_param_));
      } else {
        LOG_INFO("need wait lob tablet major sstable exist", K(ret), K(merge_param_));
      }
    } else if (OB_FAIL(prepare_full_direct_load_ddl_param(merge_param_, ddl_param))) {
      LOG_WARN("failed to get ddl param", K(ret));
    } else if (!is_idem_type(merge_param_.direct_load_type_) &&
               OB_FAIL(check_macro_intergrate_for_nidem_sn(ddl_param, tablet, compact_start_scn, compact_end_scn))) {
      LOG_WARN("failed to check ddl kv intergrated", K(ret));
    }

    if (OB_FAIL(ret)) {
    } else if (is_major_exist) { /* skip major already exist*/
    } else {
      bool is_data_complete = (is_idem_type(merge_param_.direct_load_type_) && merge_param_.is_commit_) ||
                              (!is_idem_type(merge_param_.direct_load_type_) && (merge_param_.is_commit_
                                                                                && compact_start_scn == SCN::scn_dec(merge_param_.start_scn_)
                                                                                && compact_end_scn == merge_param_.rec_scn_)

#ifdef ERRSIM
        // skip build major until current time reach the delayed time
        && ObTimeUtility::current_time() > merge_param_.rec_scn_.convert_to_ts() + GCONF.errsim_ddl_major_delay_time
#endif
                              );
      if (!is_data_complete) {
        ddl_param.table_key_.table_type_ = ObITable::DDL_DUMP_SSTABLE;
        ddl_param.table_key_.scn_range_.start_scn_ = compact_start_scn;
        ddl_param.table_key_.scn_range_.end_scn_ = compact_end_scn;
      } else {
        // use the final table key of major, do nothing
      }
      if (OB_FAIL(ObTabletDDLUtil::compact_ddl_kv(*ls,
                                                  tablet,
                                                  ddl_table_iter,
                                                  frozen_ddl_kvs_,
                                                  ddl_param,
                                                  allocator,
                                                  compacted_sstable_handle))) {
        LOG_WARN("compact sstables failed", K(ret), K(ddl_param), K(is_data_complete));
      } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->release_ddl_kvs(ObDDLKVType::DDL_KV_FULL, compact_end_scn))) {
        LOG_WARN("release ddl kv failed", K(ret), K(ddl_param), K(compact_end_scn));
      }
      if (OB_SUCC(ret) && is_data_complete) {
        is_major_exist = true;
        LOG_INFO("create major sstable success", K(ret), K(ddl_param), KPC(compacted_sstable_handle.get_table()));
      }
    }

    if (OB_SUCC(ret) && merge_param_.is_commit_ && is_major_exist) {
      ObDirectLoadMgr *direct_load_mgr = share::g_mp->direct_load_mgr();
      if (OB_FAIL(share::g_mp->tablet_runtime_meta_updater()->submit_update_task(merge_param_.tablet_id_))) {
        LOG_WARN("fail to submit tablet update task", K(ret), K(merge_param_));
      } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->release_ddl_kvs(ObDDLKVType::DDL_KV_FULL, compact_end_scn))) {
        LOG_WARN("release all ddl kv failed", K(ret), K(ddl_param));
      } else if (OB_FAIL(direct_load_mgr->remove_tablet_direct_load(
          ObTabletDirectLoadMgrKey(merge_param_.tablet_id_, DIRECT_LOAD_DDL)))) {
        if (OB_ENTRY_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("remove tablet mgr failed", K(ret), K(merge_param_));
        }
      }
      LOG_INFO("commit ddl sstable finished", K(ret), K(ddl_param), K(merge_param_), "ddl_event_info", ObDDLEventInfo());
    }
  }
  return ret;
}

int ObTabletDDLUtil::check_data_continue(
    ObTableStoreIterator &ddl_sstable_iter,
    bool &is_data_continue,
    share::SCN &compact_start_scn,
    share::SCN &compact_end_scn)
{
  int ret = OB_SUCCESS;
  is_data_continue = false;
  ddl_sstable_iter.resume();
  if (OB_UNLIKELY(!ddl_sstable_iter.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_sstable_iter.count()));
  } else if (1 == ddl_sstable_iter.count()) {
    ObITable *single_table = nullptr;
    if (OB_FAIL(ddl_sstable_iter.get_boundary_table(true/*is_last*/, single_table))) {
      LOG_WARN("get single table failed", K(ret));
    } else {
      is_data_continue = true;
      compact_start_scn = SCN::min(compact_start_scn, single_table->get_start_scn());
      compact_end_scn = SCN::max(compact_end_scn, single_table->get_end_scn());
    }
  } else {
    is_data_continue = true;
    int64_t last_slice_idx = -1;
    SCN last_end_scn = SCN::invalid_scn();
    ObITable *table = nullptr;
    while (OB_SUCC(ret) && is_data_continue) {
      if (OB_FAIL(ddl_sstable_iter.get_next(table))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("get next table from ddl_sstable_iter failed", K(ret));
        } else {
          ret = OB_SUCCESS;
          break;
        }
      } else if (OB_ISNULL(table) || OB_UNLIKELY(!table->is_sstable())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error, table is nullptr", K(ret), KPC(table));
      } else if (table->get_slice_idx() < last_slice_idx) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl sstable should sorted by slice idx asc", K(ret), K(table->get_key()), K(last_slice_idx));
      } else if (table->get_slice_idx() > last_slice_idx) { // slice idx changed
        last_end_scn = table->get_end_scn();
        last_slice_idx = table->get_slice_idx();
      }
      if (OB_SUCC(ret)) {
        // check scn range continue for each slice
        if (table->get_start_scn() > last_end_scn) {
          is_data_continue = false;
          LOG_INFO("ddl sstable not continue", K(table->get_key()), K(last_end_scn), K(last_slice_idx));
        } else {
          last_end_scn = SCN::max(last_end_scn, table->get_end_scn());
          compact_start_scn = SCN::min(compact_start_scn, table->get_start_scn());
          compact_end_scn = SCN::max(compact_end_scn, table->get_end_scn());
        }
      }
    }
  }
  return ret;
}

int ObTabletDDLUtil::check_data_continue(
    const ObIArray<ObDDLKVHandle> &ddl_kvs,
    bool &is_data_continue,
    share::SCN &compact_start_scn,
    share::SCN &compact_end_scn)
{
  int ret = OB_SUCCESS;
  is_data_continue = false;
  if (OB_UNLIKELY(ddl_kvs.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_kvs.count()));
  } else if (1 == ddl_kvs.count()) {
    is_data_continue = true;
    ObDDLKV *single_kv = ddl_kvs.at(0).get_obj();
    compact_start_scn = SCN::min(compact_start_scn, single_kv->get_start_scn());
    compact_end_scn = SCN::max(compact_end_scn, single_kv->get_end_scn());
  } else {
    ObDDLKVHandle first_kv_handle = ddl_kvs.at(0);
    ObDDLKVHandle last_kv_handle = ddl_kvs.at(ddl_kvs.count() - 1);
    is_data_continue = true;
    SCN last_end_scn = first_kv_handle.get_obj()->get_end_scn();
    for (int64_t i = 1; OB_SUCC(ret) && i < ddl_kvs.count(); ++i) {
      ObDDLKVHandle cur_kv = ddl_kvs.at(i);
      if (OB_ISNULL(cur_kv.get_obj())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl kv is null", K(ret), K(i));
      } else if (cur_kv.get_obj()->get_start_scn() <= last_end_scn) {
        last_end_scn = SCN::max(last_end_scn, cur_kv.get_obj()->get_end_scn());
      } else {
        is_data_continue = false;
        LOG_INFO("ddl kv not continue", K(i), K(last_end_scn), KPC(cur_kv.get_obj()));
        break;
      }
    }
    if (OB_SUCC(ret) && is_data_continue) {
      compact_start_scn = SCN::min(compact_start_scn, first_kv_handle.get_obj()->get_start_scn());
      compact_end_scn = SCN::max(compact_end_scn, last_kv_handle.get_obj()->get_end_scn());
    }
  }
  return ret;
}

int ObTabletDDLUtil::prepare_index_data_desc(const ObTablet &tablet,
                                             const ObITable::TableKey &table_key,
                                             const int64_t snapshot_version,
                                             const uint64_t data_format_version,
                                             const ObSSTable *first_ddl_sstable,
                                             const ObStorageSchema *storage_schema,
                                             ObWholeDataStoreDesc &data_desc)
{
  int ret = OB_SUCCESS;
  data_desc.reset();
  const ObTabletID &tablet_id = tablet.get_tablet_meta().tablet_id_;
  const SCN end_scn = table_key.get_end_scn();
  const bool micro_index_clustered = tablet.get_tablet_meta().micro_index_clustered_;
  if (OB_UNLIKELY(!tablet_id.is_valid() || snapshot_version <= 0 || data_format_version <= 0 || OB_ISNULL(storage_schema))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(snapshot_version), K(data_format_version), KP(storage_schema));
  } else if (OB_FAIL(data_desc.init(true/*is_ddl*/,
                                    *storage_schema,
                                    tablet_id,
                                    table_key.is_minor_sstable() ? compaction::MINOR_MERGE : compaction::MAJOR_MERGE,
                                    snapshot_version,
                                    data_format_version,
                                    tablet.get_tablet_meta().micro_index_clustered_,
                                    0 /* concurrent cnt */,
                                    end_scn))) {
    // use storage schema to init ObDataStoreDesc
    // all cols' default checksum will assigned to 0
    // means all macro should contain all columns in schema
    LOG_WARN("init data store desc failed", K(ret), K(tablet_id));
  } else {
    data_desc.get_static_desc().micro_index_clustered_ = micro_index_clustered;
  }
  if (OB_SUCC(ret) && nullptr != first_ddl_sstable) {
    // use the param in first ddl sstable, which persist the param when ddl start
    ObSSTableMetaHandle meta_handle;
    if (OB_FAIL(first_ddl_sstable->get_meta(meta_handle))) {
      LOG_WARN("get sstable meta handle fail", K(ret), KPC(first_ddl_sstable));
    } else {
      const ObSSTableBasicMeta &basic_meta = meta_handle.get_sstable_meta().get_basic_meta();
      if (OB_FAIL(data_desc.get_desc().update_basic_info_from_macro_meta(basic_meta))) {
        LOG_WARN("failed to update basic info from macro_meta", KR(ret), K(basic_meta));
      }
    }
  }
  LOG_DEBUG("prepare_index_data_desc", K(ret), K(data_desc));
  return ret;
}

int ObTabletDDLUtil::create_ddl_sstable(ObTablet &tablet,
                                        const ObTabletDDLParam &ddl_param,
                                        const ObIArray<ObDDLBlockMeta> &meta_array,
                                        const ObIArray<blocksstable::MacroBlockId> &macro_id_array,
                                        const ObSSTable *first_ddl_sstable,
                                        const ObStorageSchema *storage_schema,
                                        lib::ObMutex *alloc_mutex,
                                        common::ObArenaAllocator &allocator,
                                        ObTableHandleV2 &sstable_handle)
{
  int ret = OB_SUCCESS;
  HEAP_VAR(ObSSTableIndexBuilder, sstable_index_builder, true /*use buffer*/) {
    ObIndexBlockRebuilder index_block_rebuilder;
    ObWholeDataStoreDesc data_desc;
    int64_t macro_block_column_count = 0;
    if (OB_UNLIKELY(!ddl_param.is_valid() || OB_ISNULL(storage_schema))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret), K(ddl_param), KP(storage_schema));
    } else if (nullptr != first_ddl_sstable && (first_ddl_sstable->is_ddl_mem_sstable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("first ddl sstable is ddl mem sstable, which is not supported", K(ret), KPC(first_ddl_sstable));
    } else if (OB_FAIL(ObTabletDDLUtil::prepare_index_data_desc(
            tablet,
            ddl_param.table_key_,
            ddl_param.snapshot_version_,
            ddl_param.data_format_version_,
            first_ddl_sstable,
            storage_schema,
            data_desc))) {
      LOG_WARN("prepare data store desc failed", K(ret), K(ddl_param));
    } else if (FALSE_IT(macro_block_column_count = meta_array.empty() ? 0 : meta_array.at(0).block_meta_->get_meta_val().column_count_)) {
    } else if (meta_array.count() > 0 && OB_FAIL(data_desc.get_col_desc().mock_valid_col_default_checksum_array(macro_block_column_count))) {
      LOG_ERROR("mock valid column default checksum failed", K(ret), "firt_macro_block_meta", meta_array.at(0), K(ddl_param));
    } else if (OB_FAIL(sstable_index_builder.init(data_desc.get_desc()))) {
      LOG_WARN("init sstable index builder failed", K(ret), K(data_desc));
    } else if (OB_FAIL(index_block_rebuilder.init(sstable_index_builder,
            nullptr/*task_idx*/,
            ddl_param.table_key_))) {
      LOG_WARN("fail to alloc index builder", K(ret));
    } else if (meta_array.empty()) {
      // do nothing
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < meta_array.count(); ++i) {
        if (OB_FAIL(index_block_rebuilder.append_macro_row(*meta_array.at(i).block_meta_))) {
          LOG_WARN("append block meta failed", K(ret), K(i), KPC(meta_array.at(i).block_meta_));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(index_block_rebuilder.close())) {
        LOG_WARN("close index block rebuilder failed", K(ret));
      } else if (nullptr == alloc_mutex) {
        if (OB_FAIL(ObTabletDDLUtil::create_ddl_sstable(tablet, &sstable_index_builder, macro_id_array, ddl_param, first_ddl_sstable,
              macro_block_column_count, storage_schema, allocator, sstable_handle))) {
          LOG_WARN("create ddl sstable failed", K(ret), K(ddl_param));
        }
      } else {
        ObMutexGuard guard(*alloc_mutex);
        if (OB_FAIL(ObTabletDDLUtil::create_ddl_sstable(tablet, &sstable_index_builder, macro_id_array, ddl_param, first_ddl_sstable,
              macro_block_column_count, storage_schema, allocator, sstable_handle))) {
          LOG_WARN("create ddl sstable failed", K(ret), K(ddl_param));
        }
      }
    }
  }
  return ret;
}

int ObTabletDDLUtil::create_ddl_sstable(
    ObTablet &tablet,
    ObSSTableIndexBuilder *sstable_index_builder,
    const ObIArray<blocksstable::MacroBlockId> &macro_id_array,
    const ObTabletDDLParam &ddl_param,
    const ObSSTable *first_ddl_sstable,
    const int64_t macro_block_column_count,
    const ObStorageSchema *storage_schema,
    common::ObArenaAllocator &allocator,
    ObTableHandleV2 &sstable_handle)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == sstable_index_builder || !ddl_param.is_valid() || OB_ISNULL(storage_schema))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(sstable_index_builder), K(ddl_param), KP(storage_schema));
  } else {
    const int64_t create_schema_version_on_tablet = tablet.get_tablet_meta().create_schema_version_;
    ObTabletCreateSSTableParam param;
    if (OB_FAIL(param.init_for_ddl(sstable_index_builder, ddl_param, first_ddl_sstable,
        *storage_schema, macro_block_column_count, create_schema_version_on_tablet, macro_id_array))) {
      LOG_WARN("fail to init param for ddl",
          K(ret), K(macro_block_column_count), K(create_schema_version_on_tablet),
          KPC(sstable_index_builder), K(ddl_param),
          KPC(first_ddl_sstable), KPC(storage_schema), K(macro_id_array));
    } else if (OB_FAIL(ObTabletCreateDeleteHelper::create_sstable<ObSSTable>(param, allocator, sstable_handle))) {
      LOG_WARN("create sstable failed", K(ret), K(param));
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("create ddl sstable success", K(ddl_param), K(sstable_handle),
               "create_schema_version", create_schema_version_on_tablet);
    }
  }
  return ret;
}

int ObTabletDDLUtil::update_ddl_table_store(
    ObLS &ls,
    ObTablet &tablet,
    const ObTabletDDLParam &ddl_param,
    const ObStorageSchema *storage_schema,
    blocksstable::ObSSTable *sstable)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ddl_param.is_valid() || OB_ISNULL(storage_schema) || OB_ISNULL(sstable))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_param), KP(storage_schema), KP(sstable));
  } else {
    const bool is_major_sstable = ddl_param.table_key_.is_major_sstable();
    int64_t snapshot_version = 0;
    int64_t multi_version_start = 0;
    if (is_full_direct_load(ddl_param.direct_load_type_)) {
      snapshot_version = is_major_sstable ? max(ddl_param.snapshot_version_, tablet.get_snapshot_version())
                                          : tablet.get_snapshot_version();
      multi_version_start = is_major_sstable ? max(ddl_param.snapshot_version_, tablet.get_multi_version_start())
                                             : 0;
    } else {
      snapshot_version = max(ddl_param.snapshot_version_, tablet.get_snapshot_version());
      multi_version_start = tablet.get_multi_version_start();
    }
    ObTabletHandle new_tablet_handle;
    ObUpdateTableStoreParam table_store_param(snapshot_version,
                                              multi_version_start,
                                              storage_schema,
                                              sstable);
    if (OB_FAIL(table_store_param.init_with_compaction_info(
            ObCompactionTableStoreParam(is_major_sstable ? compaction::MEDIUM_MERGE : compaction::MINI_MERGE,
                                        share::SCN::min_scn(),
                                        is_major_sstable /*need_report*/,
                                        false/*has_truncate_info*/)))) {
      LOG_WARN("failed to init with compaction info", KR(ret));
    } else {
      if (is_full_direct_load(ddl_param.direct_load_type_)) { // full direct load
        table_store_param.ddl_info_.update_with_major_flag_ = is_major_sstable;
        table_store_param.ddl_info_.keep_old_ddl_sstable_ = !is_major_sstable;
        table_store_param.ddl_info_.data_format_version_ = ddl_param.data_format_version_;
        table_store_param.ddl_info_.ddl_commit_scn_ = ddl_param.commit_scn_;
        table_store_param.ddl_info_.ddl_checkpoint_scn_ = ddl_param.table_key_.is_ddl_dump_sstable() ? ddl_param.table_key_.get_end_scn() : ddl_param.commit_scn_;
      } else { // incremental direct load
        table_store_param.compaction_info_.clog_checkpoint_scn_ = sstable->get_end_scn();
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ls.update_tablet_table_store(ddl_param.table_key_.get_tablet_id(), table_store_param, new_tablet_handle))) {
        LOG_WARN("failed to update tablet table store", K(ret), K(ddl_param.table_key_), K(table_store_param));
      } else {
        FLOG_INFO("ddl update table store success", K(ddl_param), KPC(new_tablet_handle.get_obj()), K(table_store_param));
      }
    }
  }
  return ret;
}

int get_sstables(ObTableStoreIterator &ddl_sstable_iter,
                 ObIArray<ObSSTable *> &target_sstables)
{
  int ret = OB_SUCCESS;
  ddl_sstable_iter.resume();
  while (OB_SUCC(ret)) {
    ObITable *table = nullptr;
    if (OB_FAIL(ddl_sstable_iter.get_next(table))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next table failed", K(ret));
      } else {
        ret = OB_SUCCESS;
        break;
      }
    } else if (OB_ISNULL(table) || OB_UNLIKELY(!table->is_sstable())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, table is nullptr", K(ret), KPC(table));
    } else if (OB_FAIL(target_sstables.push_back(static_cast<ObSSTable *>(table)))) {
      LOG_WARN("push back target sstable failed", K(ret));
    }
  }
  return ret;
}

int get_sstables(const ObIArray<ObDDLKVHandle> &frozen_ddl_kvs,
                 ObIArray<ObSSTable *> &target_sstables)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < frozen_ddl_kvs.count(); ++i) {
    ObDDLKV *cur_kv = frozen_ddl_kvs.at(i).get_obj();
    if (OB_ISNULL(cur_kv)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObDDLMemtable *target_sstable = nullptr;
      if (cur_kv->get_ddl_memtables().empty()) {
        // do nothing
      } else if (OB_ISNULL(target_sstable = cur_kv->get_ddl_memtables().at(0))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("current sstable is null", K(ret), KPC(cur_kv), K(target_sstable));
      } else if (OB_FAIL(target_sstables.push_back(target_sstable))) {
        LOG_WARN("push back target sstable failed", K(ret));
      }
    }
  }
  return ret;
}

ObDDLMacroBlockIterator::ObDDLMacroBlockIterator()
  : is_inited_(false), sstable_(nullptr), allocator_(nullptr), macro_block_iter_(nullptr), sec_meta_iter_(nullptr)
{

}

ObDDLMacroBlockIterator::~ObDDLMacroBlockIterator()
{
  if ((nullptr != macro_block_iter_ || nullptr != sec_meta_iter_) && OB_ISNULL(allocator_)) {
    int ret = OB_ERR_SYS;
    LOG_ERROR("the iterator is allocated, but allocator is null", K(ret), KP(macro_block_iter_), KP(allocator_));
  } else if (nullptr != macro_block_iter_) {
    macro_block_iter_->~ObIMacroBlockIterator();
    allocator_->free(macro_block_iter_);
    macro_block_iter_ = nullptr;
  } else if (nullptr != sec_meta_iter_) {
    sec_meta_iter_->~ObSSTableSecMetaIterator();
    allocator_->free(sec_meta_iter_);
    sec_meta_iter_ = nullptr;
  }
}

int ObDDLMacroBlockIterator::open(ObSSTable *sstable, const ObDatumRange &query_range, const ObITableReadInfo &read_info, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(nullptr == sstable || !query_range.is_valid() || !read_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(sstable), K(query_range), K(read_info));
  } else if (sstable->is_ddl_mem_sstable()) { // ddl mem, scan keybtree
    ObDDLMemtable *ddl_memtable = static_cast<ObDDLMemtable *>(sstable);
    if (OB_ISNULL(ddl_memtable)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl memtable cast failed", K(ret));
    } else if (OB_FAIL(ddl_memtable->get_block_meta_tree()->get_keybtree().set_key_range(
            ddl_iter_,
            ObDatumRowkeyWrapper(&query_range.get_start_key(), &read_info.get_datum_utils()),
            query_range.is_left_open(),
            ObDatumRowkeyWrapper(&query_range.get_end_key(), &read_info.get_datum_utils()),
            query_range.is_right_open()))) {
      LOG_WARN("ddl memtable locate range failed", K(ret));
    }
  } else {
    ObSSTableSecMetaIterator *sec_meta_iter;
    if (OB_ISNULL(sec_meta_iter = OB_NEWx(ObSSTableSecMetaIterator, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory for sec meta iterator failed", K(ret));
    } else if (OB_FAIL(sec_meta_iter->open(query_range, ObMacroBlockMetaType::DATA_BLOCK_META, *sstable, read_info, allocator))) {
      LOG_WARN("open sec meta iterator failed", K(ret));
      sec_meta_iter->~ObSSTableSecMetaIterator();
      allocator.free(sec_meta_iter);
    } else {
      sec_meta_iter_ = sec_meta_iter;
    }
  }
  if (OB_SUCC(ret)) {
    sstable_ = sstable;
    allocator_ = &allocator;
    is_inited_ = true;
  }
  return ret;
}

int ObDDLMacroBlockIterator::get_next(ObDataMacroBlockMeta &data_macro_meta)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (sstable_->is_ddl_mem_sstable()) {
    ObDatumRowkeyWrapper tree_key;
    ObBlockMetaTreeValue *tree_value = nullptr;
    if (OB_FAIL(ddl_iter_.get_next(tree_key, tree_value))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next tree value failed", K(ret));
      }
    } else if (OB_FAIL(data_macro_meta.assign(*tree_value->block_meta_))) {
      LOG_WARN("assign block meta failed", K(ret));
    }
  } else {
    if (OB_FAIL(sec_meta_iter_->get_next(data_macro_meta))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get data macro meta failed", K(ret));
      }
    }
  }
  return ret;
}

int compact_sstables(
    ObTablet &tablet,
    ObIArray<ObSSTable *> &sstables,
    const ObTabletDDLParam &ddl_param,
    const ObITableReadInfo &read_info,
    const ObStorageSchema *storage_schema,
    ObArenaAllocator &allocator,
    ObTableHandleV2 &sstable_handle,
    ObDDLWriteStat *write_stat = nullptr)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator arena("compact_sst", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObArray<ObDDLBlockMeta> sorted_metas;
  HEAP_VARS_2((ObBlockMetaTree, meta_tree),
              (ObTableStoreIterator, ddl_table_iter)) {
  ObITable *first_ddl_sstable = nullptr; // get compressor_type of macro block for query
  if (OB_FAIL(tablet.get_ddl_sstables(ddl_table_iter))) {
    LOG_WARN("get ddl sstable handles failed", K(ret));
  } else if ((ddl_table_iter.count() > 0)
      && OB_FAIL(ddl_table_iter.get_boundary_table(false/*is_last*/, first_ddl_sstable))) {
    LOG_WARN("failed to get boundary table", K(ret));
  } else if (OB_FAIL(meta_tree.init(tablet, ddl_param.table_key_, ddl_param.start_scn_, ddl_param.data_format_version_, storage_schema, static_cast<ObSSTable *>(first_ddl_sstable)))) {
    LOG_WARN("init meta tree failed", K(ret), K(ddl_param));
  } else if (OB_FAIL(ObDDLMergeTaskUtils::get_sorted_meta_array(tablet, ddl_param, storage_schema, sstables, read_info, arena, sorted_metas))) {
    LOG_WARN("get sorted meta array failed", K(ret), K(read_info), K(sstables));
  } else if (OB_FAIL(ObTabletDDLUtil::create_ddl_sstable(
          tablet,
          ddl_param,
          sorted_metas,
          ObArray<MacroBlockId>(),
          sstables.empty() ? nullptr : sstables.at(0)/*first ddl sstable*/,
          storage_schema,
          nullptr /* not need mutex*/,
          allocator,
          sstable_handle))) {
    LOG_WARN("create sstable failed", K(ret), K(ddl_param), K(sstables));
  }
  } // heap var meta_tree
  LOG_DEBUG("compact_sstables", K(ret), K(sstables), K(ddl_param), K(read_info), KPC(sstable_handle.get_table()));
  return ret;
}

int ObTabletDDLUtil::get_compact_meta_array(
    ObTablet &tablet,
    ObIArray<ObSSTable *> &sstables,
    const ObTabletDDLParam &ddl_param,
    const ObITableReadInfo &read_info,
    const ObStorageSchema *storage_schema,
    common::ObArenaAllocator &allocator,
    ObArray<ObDDLBlockMeta> &sorted_metas)
{
  int ret = OB_SUCCESS;
  sorted_metas.reset();
  if (OB_FAIL(ObDDLMergeTaskUtils::get_sorted_meta_array(tablet, ddl_param, storage_schema, sstables, read_info, allocator, sorted_metas))) {
    LOG_WARN("get sorted meta array failed", K(ret), K(read_info), K(sstables));
  }
  return ret;
}

int compact_ro_ddl_sstable(
    ObTablet &tablet,
    ObTableStoreIterator &ddl_sstable_iter,
    const ObIArray<ObDDLKVHandle> &frozen_ddl_kvs,
    const ObTabletDDLParam &ddl_param,
    const ObStorageSchema *storage_schema,
    common::ObArenaAllocator &allocator,
    ObTableHandleV2 &ro_sstable_handle)
{
  int ret = OB_SUCCESS;
  ro_sstable_handle.reset();
  if (OB_UNLIKELY(ddl_sstable_iter.count() == 0 && frozen_ddl_kvs.count() == 0 && !is_idem_type(ddl_param.direct_load_type_))) { // idem may genearte empty sstalbe
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_sstable_iter.count()), K(frozen_ddl_kvs.count()));
  } else {
    ObArray<ObSSTable *> base_sstables;
    if (OB_FAIL(get_sstables(ddl_sstable_iter, base_sstables))) {
      LOG_WARN("get base sstable from ddl sstables failed", K(ret), K(ddl_sstable_iter));
    } else if (OB_FAIL(get_sstables(frozen_ddl_kvs, base_sstables))) {
      LOG_WARN("get base sstable from ddl kv array failed", K(ret), K(frozen_ddl_kvs));
    } else if (OB_FAIL(compact_sstables(tablet, base_sstables, ddl_param, tablet.get_rowkey_read_info(), storage_schema, allocator, ro_sstable_handle))) {
      LOG_WARN("compact base sstable failed", K(ret));
    }
  }
  LOG_INFO("compact_ro_ddl_sstable", K(ret), K(ddl_sstable_iter), K(ddl_param), KP(&tablet), KPC(ro_sstable_handle.get_table()));
  return ret;
}


int get_storage_schema_sn_idem(const ObTabletDDLParam &ddl_param,
                               ObIAllocator &allocator,
                               const ObTablet &tablet,
                               ObStorageSchema *&storage_schema)
{
  int ret = OB_SUCCESS;
  storage_schema = nullptr;
  ObTabletDDLCompleteMdsUserData data;
  if (!ddl_param.is_valid() || !tablet.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_param), K(tablet));
  } else if (OB_FAIL(tablet.get_ddl_complete(share::SCN::max_scn(), allocator, data))) {
    LOG_WARN("failed to get ddl complete mds", K(ret));
  } else if (!data.has_complete_ || !data.is_valid()) {
    if (OB_FAIL(tablet.load_storage_schema(allocator, storage_schema))) {
      LOG_WARN("load storage schema failed", K(ret), K(ddl_param));
    }
  } else {
    /* load storage schema */
    if (!data.get_storage_schema().is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid stroage schema", K(ret), K(data));
    } else if (OB_FAIL(ObStorageSchemaUtil::alloc_storage_schema(allocator, storage_schema))) {
      LOG_WARN("failed to alloc storage schema", K(ret));
    } else if (OB_FAIL(storage_schema->init(allocator, data.get_storage_schema()))) {
      LOG_WARN("failed to assign storage schema", K(ret), K(data.get_storage_schema()));
    }
  }
  return ret;
}

int ObTabletDDLUtil::compact_ddl_kv(
    ObLS &ls,
    ObTablet &tablet,
    ObTableStoreIterator &ddl_sstable_iter,
    const ObIArray<ObDDLKVHandle> &frozen_ddl_kvs,
    const ObTabletDDLParam &ddl_param,
    common::ObArenaAllocator &allocator,
    ObTableHandleV2 &compacted_sstable_handle)
{
  int ret = OB_SUCCESS;
  compacted_sstable_handle.reset();
  ObArenaAllocator arena("compact_ddl_kv", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObStorageSchema *storage_schema = nullptr;

  if (OB_UNLIKELY(!ddl_param.is_valid() || (0 == ddl_sstable_iter.count() && frozen_ddl_kvs.empty() && !is_idem_type(ddl_param.direct_load_type_)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_param), K(ddl_param.table_key_), K(ddl_param.table_key_.is_valid()), K(ddl_sstable_iter.count()), K(frozen_ddl_kvs.count()));
  } else if (is_idem_type(ddl_param.direct_load_type_) &&
                          OB_FAIL(get_storage_schema_sn_idem(ddl_param, arena, tablet, storage_schema))) {
    LOG_WARN("load storage schema failed", K(ret), K(ddl_param));
  } else if (OB_FAIL(tablet.load_storage_schema(arena, storage_schema))) {
    LOG_WARN("load storage schema failed", K(ret), K(ddl_param));
  }

  if (OB_FAIL(ret)) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < frozen_ddl_kvs.count(); ++i) {
      if (OB_FAIL(frozen_ddl_kvs.at(i).get_obj()->close())) {
        LOG_WARN("close ddl kv failed", K(ret), K(i));
      }
    }

#ifdef ERRSIM
    if (OB_SUCC(ret) && ddl_param.table_key_.is_major_sstable()) {
      ret = OB_E(EventTable::EN_DDL_COMPACT_FAIL) OB_SUCCESS;
      if (OB_FAIL(ret)) {
        LOG_WARN("errsim compact ddl sstable failed", KR(ret));
      }
    }
#endif

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(compact_ro_ddl_sstable(tablet, ddl_sstable_iter, frozen_ddl_kvs, ddl_param, storage_schema, allocator, compacted_sstable_handle))) {
      LOG_WARN("compact row-store ddl sstable failed", K(ret), K(ddl_param));
    }
    if (OB_SUCC(ret)) { // update table store
      if (OB_FAIL(update_ddl_table_store(ls, tablet, ddl_param, storage_schema, static_cast<ObSSTable *>(compacted_sstable_handle.get_table())))) {
        LOG_WARN("update ddl table store failed", K(ret));
      } else {
        LOG_INFO("compact ddl sstable success", K(ddl_param));
      }
    }
  }
  ObTabletObjLoadHelper::free(arena, storage_schema);
  return ret;
}

int check_ddl_sstable_expired(const SCN &ddl_start_scn, ObTableStoreIterator &ddl_sstable_iter)
{
  int ret = OB_SUCCESS;
  ObITable *table = nullptr;
  ObSSTable *ddl_sstable = nullptr;
  ObSSTableMetaHandle meta_handle;
  if (0 == ddl_sstable_iter.count()) {
    // do nothing
  } else if (OB_FAIL(ddl_sstable_iter.get_boundary_table(false, table))) {
    LOG_WARN("get first ddl sstable failed", K(ret));
  } else if (OB_ISNULL(ddl_sstable = static_cast<ObSSTable *>(table))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl sstable is null", K(ret), KPC(table));
  } else if (OB_FAIL(ddl_sstable->get_meta(meta_handle))) {
    LOG_WARN("get meta handle failed", K(ret));
  } else if (meta_handle.get_sstable_meta().get_ddl_scn() < ddl_start_scn) {
    ret = OB_TASK_EXPIRED;
    LOG_WARN("ddl sstable is expired", K(ret), K(meta_handle.get_sstable_meta()), K(ddl_start_scn));
  }
  return ret;
}

int check_ddl_kv_expired(const SCN &ddl_start_scn, const ObIArray<ObDDLKVHandle> &frozen_ddl_kvs)
{
  int ret = OB_SUCCESS;
  ObDDLKV *ddl_kv = nullptr;
  if (frozen_ddl_kvs.empty()) {
    // do nothing
  } else if (OB_ISNULL(ddl_kv = frozen_ddl_kvs.at(0).get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv is null", K(ret), K(frozen_ddl_kvs));
  } else if (ddl_kv->get_ddl_start_scn() < ddl_start_scn) {
    ret = OB_TASK_EXPIRED;
    LOG_WARN("ddl sstable is expired", K(ret), KPC(ddl_kv), K(ddl_start_scn));
  }
  return ret;
}

int ObTabletDDLUtil::get_compact_scn(
    const SCN &ddl_start_scn,
    ObTableStoreIterator &ddl_sstable_iter,
    const ObIArray<ObDDLKVHandle> &frozen_ddl_kvs,
    SCN &compact_start_scn,
    SCN &compact_end_scn)
{
  int ret = OB_SUCCESS;
  bool is_data_continue = true;
  compact_start_scn = SCN::max_scn();
  compact_end_scn = SCN::min_scn();
  SCN ddl_sstables_start_scn = SCN::max_scn();
  SCN ddl_sstables_end_scn = SCN::min_scn();
  SCN ddl_kvs_start_scn = SCN::max_scn();
  SCN ddl_kvs_end_scn = SCN::min_scn();
  ddl_sstable_iter.resume();
  if (OB_UNLIKELY((0 == ddl_sstable_iter.count() && frozen_ddl_kvs.empty()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_sstable_iter.count()), K(frozen_ddl_kvs.count()));
  } else if (OB_FAIL(check_ddl_sstable_expired(ddl_start_scn, ddl_sstable_iter))) {
    LOG_WARN("check ddl sstable expired failed", K(ret), K(ddl_start_scn), K(ddl_sstable_iter));
  } else if (ddl_sstable_iter.count() > 0 && OB_FAIL(check_data_continue(ddl_sstable_iter, is_data_continue, ddl_sstables_start_scn, ddl_sstables_end_scn))) {
    LOG_WARN("check ddl sstable continue failed", K(ret));
  } else if (!is_data_continue) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl sstable not continuous", K(ret), K(ddl_sstable_iter));
  } else if (OB_FAIL(check_ddl_kv_expired(ddl_start_scn, frozen_ddl_kvs))) {
    LOG_WARN("check ddl kv expired failed", K(ret), K(ddl_start_scn), K(frozen_ddl_kvs));
  } else if (frozen_ddl_kvs.count() > 0 && OB_FAIL(check_data_continue(frozen_ddl_kvs, is_data_continue, ddl_kvs_start_scn, ddl_kvs_end_scn))) {
    LOG_WARN("check ddl sstable continue failed", K(ret));
  } else if (!is_data_continue) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv not continuous", K(ret), K(frozen_ddl_kvs));
  } else {
    if (ddl_sstable_iter.count() > 0 && frozen_ddl_kvs.count() > 0) {
      // |___________________________________________________|
      // ddl_sstables_start_scn                              ddl_sstables_end_scn
      //                                 |____________________________________________________________|
      //                                 ddl_kvs_start_scn                                            ddl_kvs_end_scn
      is_data_continue = ddl_kvs_start_scn >= ddl_sstables_start_scn
        && ddl_kvs_start_scn <= ddl_sstables_end_scn
        && ddl_kvs_end_scn >= ddl_sstables_end_scn;
      if (!is_data_continue) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("scn range not continue between ddl sstable iter and ddl kv array", K(ret), K(ddl_sstables_start_scn), K(ddl_sstables_end_scn), K(ddl_kvs_start_scn), K(ddl_kvs_end_scn), K(ddl_sstable_iter), K(frozen_ddl_kvs));
      } else {
        compact_start_scn = ddl_sstables_start_scn;
        compact_end_scn = ddl_kvs_end_scn;
      }
    } else if (ddl_sstable_iter.count() > 0) {
      compact_start_scn = ddl_sstables_start_scn;
      compact_end_scn = ddl_sstables_end_scn;
    } else if (frozen_ddl_kvs.count() > 0) {
      compact_start_scn = ddl_kvs_start_scn;
      compact_end_scn = ddl_kvs_end_scn;
    }
    LOG_INFO("get compact scn", K(ret), K(compact_start_scn), K(compact_end_scn), K(ddl_sstable_iter), K(frozen_ddl_kvs));
  }
  return ret;
}

int ObTabletDDLUtil::report_ddl_checksum(
    const ObTabletID &tablet_id,
    const uint64_t table_id,
    const int64_t execution_id,
    const int64_t ddl_task_id,
    const int64_t *column_checksums,
    const int64_t column_count,
    const uint64_t data_format_version)
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  ObMultiVersionSchemaService *schema_service = GCTX.schema_service_;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = nullptr;

  if (OB_UNLIKELY(!tablet_id.is_valid() || OB_INVALID_ID == ddl_task_id
        || !is_valid_id(table_id) || 0 == table_id || execution_id < 0 || nullptr == column_checksums || column_count <= 0 || data_format_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(table_id), K(execution_id), KP(column_checksums), K(column_count), K(data_format_version));
  } else if (OB_ISNULL(sql_proxy) || OB_ISNULL(schema_service)) {
    ret = OB_ERR_SYS;
    LOG_WARN("ls service or sql proxy is null", K(ret), KP(sql_proxy), KP(schema_service));
  } else if (OB_FAIL(schema_service->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(table_id));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_INFO("table not exit", K(ret), K(table_id));
    ret = OB_TASK_EXPIRED; // for ignore warning
  } else if (OB_FAIL(DDL_SIM(ddl_task_id, REPORT_DDL_CHECKSUM_FAILED))) {
    LOG_WARN("ddl sim failure", K(ddl_task_id));
  } else {
    ObArray<ObColDesc> column_ids;
    ObArray<ObDDLChecksumItem> ddl_checksum_items;
    if (OB_FAIL(table_schema->get_multi_version_column_descs(column_ids))) {
      LOG_WARN("fail to get column ids", K(ret), K(tablet_id));
    } else if (OB_UNLIKELY(column_count > column_ids.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpect error, column checksums count larger than column ids count", K(ret),
          K(tablet_id), K(column_count), K(column_ids.count()));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < column_count; ++i) {
      share::ObDDLChecksumItem item;
      item.execution_id_ = execution_id;

      item.table_id_ = table_id;
      item.tablet_id_ = tablet_id.id();
      item.ddl_task_id_ = ddl_task_id;
      item.column_id_ = column_ids.at(i).col_id_;
      item.task_id_ = tablet_id.id();
      item.checksum_ = column_checksums[i];
#ifdef ERRSIM
      if (OB_SUCC(ret)) {
        ret = OB_E(EventTable::EN_HIDDEN_CHECKSUM_DDL_TASK) OB_SUCCESS;
        // set the checksum of the second column inconsistent with the report checksum of data table. (report_ddl_column_checksum())
        if (OB_FAIL(ret) && 17 == item.column_id_) {
          item.checksum_ = i + 100;
        }
      }
#endif
      if (item.column_id_ >= OB_MIN_SHADOW_COLUMN_ID ||
          item.column_id_ == OB_HIDDEN_TRANS_VERSION_COLUMN_ID ||
          item.column_id_ == OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID) {
        continue;
      } else if (OB_FAIL(ddl_checksum_items.push_back(item))) {
        LOG_WARN("push back column checksum item failed", K(ret));
      }
    }
#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = OB_E(EventTable::EN_DDL_REPORT_CHECKSUM_FAIL) OB_SUCCESS;
      if (OB_FAIL(ret)) {
        LOG_ERROR("errsim report checksum failed", KR(ret));
      }
    }
#endif
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObDDLChecksumOperator::update_checksum(data_format_version, ddl_checksum_items, *sql_proxy))) {
      LOG_WARN("fail to update checksum", K(ret), K(tablet_id), K(table_id), K(ddl_checksum_items));
    } else {
      LOG_INFO("report ddl checkum success", K(tablet_id), K(table_id), K(execution_id), K(ddl_checksum_items), K(common::lbt()));
    }
  }
  return ret;
}

int ObTabletDDLUtil::check_and_get_major_sstable(const ObTabletID &tablet_id,
                                                 const blocksstable::ObSSTable *&first_major_sstable,
                                                 ObTabletMemberWrapper<ObTabletTableStore> &table_store_wrapper)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  first_major_sstable = nullptr;
  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("failed to get log stream", K(ret));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls,
                                               tablet_id,
                                               tablet_handle,
                                               ObMDSGetTabletMode::READ_ALL_COMMITED))) {
    LOG_WARN("get tablet handle failed", K(ret), K(tablet_id));
  } else if (OB_UNLIKELY(nullptr == tablet_handle.get_obj())) {
    ret = OB_ERR_SYS;
    LOG_WARN("tablet handle is null", K(ret), K(tablet_id));
  } else if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
    LOG_WARN("fail to fetch table store", K(ret));
  } else {
    first_major_sstable = static_cast<ObSSTable *>(
        table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/));
  }
  return ret;
}

int ObTabletDDLUtil::freeze_ddl_kv(const ObDDLTableMergeDagParam &param)
{
  return ObDDLMergeTaskUtils::freeze_ddl_kv(param.tablet_id_, param.direct_load_type_,
                                            param.start_scn_, param.snapshot_version_, param.data_format_version_);
}

} // namespace storage
} // namespace oceanbase
