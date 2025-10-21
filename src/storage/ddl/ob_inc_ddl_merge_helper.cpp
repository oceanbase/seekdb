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
#include "storage/ddl/ob_inc_ddl_merge_helper.h"
#include "storage/ddl/ob_ddl_merge_task_utils.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablet/ob_tablet_create_sstable_param.h"
#include "storage/blocksstable/index_block/ob_macro_meta_temp_store.h"
#include "share/compaction/ob_shared_storage_compaction_util.h"
#include "storage/ddl/ob_ddl_independent_dag.h"
#ifdef OB_BUILD_SHARED_STORAGE
#include "close_modules/shared_storage/storage/compaction_v2/ob_ss_compact_helper.h"
#include "storage/compaction/ob_schedule_dag_func.h"
#endif

using namespace oceanbase::observer;
using namespace oceanbase::share::schema;
using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::blocksstable;
using namespace oceanbase::transaction;
using namespace oceanbase::compaction;

namespace oceanbase
{
namespace storage
{

int ObIncMinDDLMergeHelper::get_rec_scn(ObDDLTabletMergeDagParamV2 &merge_param)
{
  return OB_SUCCESS; /* do nothing */
}

int ObIncMinDDLMergeHelper::process_prepare_task(ObIDag *dag,
                                                 ObDDLTabletMergeDagParamV2 &dag_merge_param,
                                                 ObIArray<ObTuple<int64_t, int64_t, int64_t>> &cg_slices)
{
  int ret = OB_SUCCESS;

  cg_slices.reset();
  ObLSID ls_id;
  ObTabletID tablet_id;
  ObWriteTabletParam           *tablet_param = nullptr;
  ObDDLTabletContext::MergeCtx *merge_ctx    = nullptr;

  ObTabletHandle tablet_handle;
  ObDDLKV *ddl_kv = nullptr;

  bool need_check_tablet = false;
  share::SCN clog_checkpoint_scn;
  hash::ObHashSet<int64_t> slice_idxes;

  /* check param & prepare necessary param*/
  if (!dag_merge_param.is_valid() || nullptr == dag) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(dag_merge_param), KPC(dag));
  } else if (OB_FAIL(dag_merge_param.get_tablet_param(ls_id, tablet_id, tablet_param))) {
    LOG_WARN("failed to get tablet param", K(ret), K(dag_merge_param));
  } else if (OB_FAIL(dag_merge_param.get_merge_ctx(merge_ctx))) {
    LOG_WARN("failed to get merge ctx", K(ret), K(dag_merge_param));
  } else if (nullptr == tablet_param || nullptr == merge_ctx) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param & merge ctx should not be null", K(ret));
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(ls_id, tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet handle", K(ret), K(ls_id), K(tablet_id));
  } else if (!tablet_handle.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet handle should not be invalid", K(ret));
  } else if (OB_FAIL(slice_idxes.create(DDL_SLICE_BUCKET_NUM, ObMemAttr(MTL_ID(), "slice_idx_set")))) {
    LOG_WARN("create slice index set failed", K(ret));
  } else {
    clog_checkpoint_scn = tablet_handle.get_obj()->get_clog_checkpoint_scn();
  }

  /* check ddl kv valid && prepare ddl kv */
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObDDLMergeTaskUtils::prepare_incremental_direct_load_ddl_kvs(*(tablet_handle.get_obj()), merge_ctx->ddl_kv_handles_))) {
    LOG_WARN("failed to prepare incremental direct load ddl kvs", K(ret));
  } else if (1 != merge_ctx->ddl_kv_handles_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected frozen ddl kv count", K(ret));
  } else if (OB_ISNULL(ddl_kv = merge_ctx->ddl_kv_handles_.at(0).get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv should not be null", K(ret), K(dag_merge_param));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < merge_ctx->ddl_kv_handles_.count(); ++i) {
    if (OB_FAIL(merge_ctx->ddl_kv_handles_.at(i).get_obj()->close())) {
      LOG_WARN("close ddl kv failed", K(ret), K(i));
    }
  }
  /* notice !!! in incremental direct load, scn range should be set according to the ddl kv state */
  if (OB_FAIL(ret)) {
  } else {
    dag_merge_param.table_key_.scn_range_.start_scn_ = ddl_kv->get_start_scn();
    dag_merge_param.table_key_.scn_range_.end_scn_   = ddl_kv->get_end_scn();
  }

  /* chekc table key is valid */
  if (OB_FAIL(ret)) {
  } else if (clog_checkpoint_scn >= ddl_kv->get_end_scn()) {
    ret = OB_TASK_EXPIRED; /* use task expired to cancel the following tasks */
    LOG_WARN("task expired", K(ret), K(ddl_kv->get_end_scn()), K(clog_checkpoint_scn));
  } else if (OB_FAIL(ObDDLMergeTaskUtils::refine_incremental_direct_load_merge_param(*tablet_handle.get_obj(),
                                                                                     dag_merge_param.table_key_,
                                                                                     need_check_tablet))) {
    if (OB_NO_NEED_MERGE != ret) {
      LOG_WARN("fail to refine incremental direct load merge param", K(ret), KPC(tablet_handle.get_obj()), K(dag_merge_param));
    } else {
      ret = OB_TASK_EXPIRED;
      LOG_WARN("not need to merge, use task expired to cancel to whole dag", K(ret));
    }
  } else if (OB_UNLIKELY(need_check_tablet)) {
    ret = OB_EAGAIN;
    if (tablet_handle.get_obj()->get_clog_checkpoint_scn() != clog_checkpoint_scn) { // do nothing, just retry the merge task
    } else {
      LOG_WARN("Unexpected uncontinuous scn_range in mini merge", K(ret), K(clog_checkpoint_scn), K(dag_merge_param));
    }
  } else if (OB_FAIL(cg_slices.push_back(ObTuple<int64_t, int64_t, int64_t>(0 /* cg_idx */,
                                                                            0 /* start slice idx */,
                                                                            0 /* end slice idx   */)))) {
    LOG_WARN("failed to push back cg slice", K(ret));
  } else if (OB_FAIL(slice_idxes.set_refactored(0))) {
    LOG_WARN("failed to set slice idx", K(ret));
  } else if (OB_FAIL(dag_merge_param.init_cg_sstable_array(slice_idxes))) {
    LOG_WARN("failed to init cg sstable array", K(ret));
  }

  return ret;
}

int ObIncMinDDLMergeHelper::merge_cg_slice(ObIDag *dag,
                                           ObDDLTabletMergeDagParamV2 &dag_merge_param,
                                           const int64_t cg_idx,
                                           const int64_t start_slice_idx,
                                           const int64_t end_slice_idx)
{
  int ret = OB_SUCCESS;

  ObLSID ls_id;
  ObTabletID tablet_id;
  ObTabletHandle tablet_handle;
  ObWriteTabletParam *tablet_param = nullptr;
  ObDDLTabletContext::MergeCtx    *merge_ctx    = nullptr;

  ObDDLKV *ddl_kv = nullptr;
  bool need_check_tablet = false;
  ObITable *last_table = nullptr;

  ObTableHandleV2 sstable_handle;
  ObArray<ObStorageMetaHandle> meta_handles;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  ObTabletDDLParam tablet_ddl_param;
  ObArray<ObDDLBlockMeta> sorted_metas;
  ObArray<ObSSTable*> ddl_sstables;
  ObArenaAllocator arena(ObMemAttr(MTL_ID(), "merge_cg_slice"));

  /* prepare param */
  if (nullptr == dag || !dag_merge_param.is_valid() || cg_idx < 0 || start_slice_idx > end_slice_idx) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(dag), K(dag_merge_param), K(cg_idx), K(start_slice_idx), K(end_slice_idx));
  } else if (OB_FAIL(dag_merge_param.get_tablet_param(ls_id, tablet_id, tablet_param))) {
    LOG_WARN("failed to get tablet param", K(ret), K(dag_merge_param));
  } else if (OB_FAIL(dag_merge_param.get_merge_ctx(merge_ctx))) {
    LOG_WARN("failed to get merge ctx", K(ret));
  } else if (nullptr == merge_ctx || nullptr == tablet_param) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param should not be null", K(ret), K(dag_merge_param));
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(ls_id, tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet handle", K(ret));
  } else if (OB_ISNULL(ddl_kv = merge_ctx->ddl_kv_handles_.at(0).get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv should not be null", K(ret), K(dag_merge_param));
  }

  /* prepare ddl param */
  if (OB_FAIL(ret)) {
  } else {
    tablet_ddl_param.direct_load_type_    = dag_merge_param.direct_load_type_;
    tablet_ddl_param.ls_id_               = ls_id;
    tablet_ddl_param.start_scn_           = ddl_kv->get_ddl_start_scn();
    tablet_ddl_param.commit_scn_          = ddl_kv->get_ddl_start_scn();
    tablet_ddl_param.data_format_version_ = dag_merge_param.ddl_task_param_.tenant_data_version_;
    tablet_ddl_param.table_key_.tablet_id_= dag_merge_param.table_key_.tablet_id_;
    tablet_ddl_param.table_key_.scn_range_.start_scn_ = ddl_kv->get_start_scn();
    tablet_ddl_param.table_key_.scn_range_.end_scn_   = ddl_kv->get_end_scn();
    tablet_ddl_param.table_key_.table_type_           = ObITable::MINI_SSTABLE;
    tablet_ddl_param.snapshot_version_                = ddl_kv->get_snapshot_version();
    tablet_ddl_param.trans_id_                        = ddl_kv->get_trans_id();
    if (OB_FAIL(ObDDLMergeTaskUtils::update_storage_schema(*tablet_handle.get_obj(),
                                                           tablet_ddl_param,
                                                           merge_ctx->arena_,
                                                           tablet_param->storage_schema_,
                                                           merge_ctx->ddl_kv_handles_))) {
      LOG_WARN("failed to update storage schema", K(ret));
    }
  }
  /* merge from ddl sstable & ddl slice sstalbe like row store*/
  SMART_VAR(ObTableStoreIterator, ddl_sstable_iter) {
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_sstables(ddl_sstable_iter))) {
      LOG_WARN("failed to get ddl sstable", K(ret));
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_ddl_tables_from_ddl_kvs(merge_ctx->ddl_kv_handles_,
                                                   cg_idx,
                                                   start_slice_idx,
                                                   dag_merge_param.for_major_ ? INT64_MAX : end_slice_idx,
                                                   ddl_sstables))) {
     LOG_WARN("failed to get ddl tables from  ddl kvs", K(ret));
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_ddl_tables_from_dump_tables(tablet_param->storage_schema_->is_row_store(),
                                                       ddl_sstable_iter,
                                                       cg_idx,
                                                       start_slice_idx,
                                                       dag_merge_param.for_major_ ? INT64_MAX : end_slice_idx,
                                                       ddl_sstables,
                                                       meta_handles))) {
      LOG_WARN("failed to get ddl tables from dump sstables", K(ret), K(dag_merge_param), K(cg_idx), K(start_slice_idx), K(end_slice_idx));
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_sorted_meta_array(*tablet_handle.get_obj(),
                                                                  tablet_ddl_param,
                                                                  tablet_param->storage_schema_,
                                                                  ddl_sstables,
                                                                  tablet_handle.get_obj()->get_rowkey_read_info(),
                                                                  arena,
                                                                  sorted_metas))) {
      LOG_WARN("failed to get sorted meta array", K(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObTabletDDLUtil::create_ddl_sstable(*(tablet_handle.get_obj()),
                                                           tablet_ddl_param,
                                                           sorted_metas,
                                                           ObArray<MacroBlockId>(),
                                                           nullptr,
                                                           tablet_param->storage_schema_,
                                                           &merge_ctx->mutex_,
                                                           merge_ctx->arena_,
                                                           sstable_handle))) {
    LOG_WARN("failed to create sstable", K(ret), K(cg_idx), K(tablet_ddl_param));
  } else if (OB_FAIL(dag_merge_param.set_cg_slice_sstable(start_slice_idx, cg_idx, sstable_handle))) {
    LOG_WARN("failed to set ddl sstable", K(ret), K(dag_merge_param));
  }
  return ret;
}

int ObIncMinDDLMergeHelper::assemble_sstable(ObDDLTabletMergeDagParamV2 &dag_merge_param)
{
  int ret = OB_SUCCESS;

  ObLSID ls_id;
  ObTabletID tablet_id;
  ObWriteTabletParam           *tablet_param = nullptr;
  ObDDLTabletContext::MergeCtx *merge_ctx    = nullptr;

  ObLSService *ls_service = MTL(ObLSService*);
  ObLSHandle ls_handle;

  ObDDLKV *ddl_kv = nullptr;
  ObTabletHandle tablet_handle;
  blocksstable::ObSSTable *sstable = nullptr;
  ObArray<ObTableHandleV2> *sstable_handles = nullptr;

  /* check arg valid & prepare param*/
  if (!dag_merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(dag_merge_param));
  } else if (OB_FAIL(dag_merge_param.get_tablet_param(ls_id, tablet_id, tablet_param))) {
    LOG_WARN("failed to get tablet ctx", K(ret), K(dag_merge_param));
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(ls_id, tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet handle", K(ret), K(ls_id), K(tablet_id));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet handle is invalid", K(ret));
  } else if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls_id, ls_handle, ObLSGetMod::DDL_MOD))) {
    LOG_WARN("failed to get ls", K(ret), K(dag_merge_param));
  } else if (OB_FAIL(dag_merge_param.get_merge_ctx(merge_ctx))) {
    LOG_WARN("failed to get merge ctx", K(ret));
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be null", K(ret));
  } else if (OB_FAIL(merge_ctx->slice_cg_sstables_.get_refactored(0 /*slice_id*/, sstable_handles))) {
    LOG_WARN("failed to get refactor", K(ret), K(dag_merge_param));
  } else if (0 == sstable_handles->count()) {
    LOG_INFO("no sstable need to be merge", K(ret));
  } else if (OB_ISNULL(sstable = static_cast<ObSSTable*>(sstable_handles->at(0).get_table()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sstable should not be null", K(ret));
  }

  /* update table store */
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(ddl_kv = merge_ctx->ddl_kv_handles_.at(0).get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ddl kv should not be null", K(ret), K(dag_merge_param));
  } else {
     ObUpdateTableStoreParam table_store_param(max(ddl_kv->get_snapshot_version(), tablet_handle.get_obj()->get_snapshot_version()),
                                                   tablet_handle.get_obj()->get_multi_version_start(),
                                                   tablet_param->storage_schema_,
                                                   ls_handle.get_ls()->get_rebuild_seq(),
                                                   sstable);
    if (OB_FAIL(table_store_param.init_with_compaction_info(ObCompactionTableStoreParam(compaction::MINI_MERGE,
                                                                                        share::SCN::min_scn(),
                                                                                        false /* not need report */,
                                                                                        false /* has truncate info*/)))) {
      LOG_WARN("failed to init with compaction info", K(ret));
    } else {
      table_store_param.compaction_info_.clog_checkpoint_scn_ = sstable->get_end_scn();
      table_store_param.ha_info_.need_check_transfer_seq_ = true;
      table_store_param.ha_info_.transfer_seq_ = tablet_handle.get_obj()->get_tablet_meta().transfer_info_.transfer_seq_;
      ObTabletHandle new_tablet_handle;
      if (OB_FAIL(ls_handle.get_ls()->update_tablet_table_store(tablet_id, table_store_param, new_tablet_handle))) {
        LOG_WARN("failed to update tablet table store", K(ret), K(dag_merge_param), K(table_store_param));
      } else {
        FLOG_INFO("ddl update table store success", KPC(new_tablet_handle.get_obj()), K(table_store_param));
      }
#ifdef OB_BUILD_SHARED_STORAGE
      if (OB_SUCC(ret) && GCTX.is_shared_storage_mode()) {
        ObSSTableUploadRegHandle upload_register_handle;
        if (OB_FAIL(ls_handle.get_ls()->prepare_register_sstable_upload(upload_register_handle))) {
          LOG_WARN("fail to prepare register sstable upload", KR(ret));
        } else {
          SCN snapshot_version(SCN::min_scn());
          if (OB_FAIL(new_tablet_handle.get_obj()->get_snapshot_version(snapshot_version))) {
            LOG_WARN("get snapshot version failed", K(new_tablet_handle));
          } else {
            ASYNC_UPLOAD_INC_SSTABLE(SSIncSSTableType::MINI_SSTABLE,
                                     upload_register_handle,
                                     sstable->get_key(),
                                     snapshot_version);
          }
        }
      }
#endif
    }
  }

  /* release ddl memtable */
  if (OB_SUCC(ret)) {
    int tmp_ret = OB_SUCCESS;
    ObTabletHandle new_tablet_handle;
    if (OB_TMP_FAIL(ObDDLUtil::ddl_get_tablet(ls_handle,
                                              tablet_id,
                                              new_tablet_handle,
                                              ObMDSGetTabletMode::READ_ALL_COMMITED))) {
      LOG_WARN("failed to get tablet", K(tmp_ret), K(dag_merge_param));
    } else if (OB_TMP_FAIL(new_tablet_handle.get_obj()->release_memtables(new_tablet_handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_))) {
      LOG_WARN("failed to release memtable", K(tmp_ret),
        "clog_checkpoint_scn", new_tablet_handle.get_obj()->get_tablet_meta().clog_checkpoint_scn_);
    }
  }
  return ret;
}

} // namespace  storage
} // namespace oceanbase
