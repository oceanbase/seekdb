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
#include "storage/ddl/ob_ddl_merge_helper.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ddl/ob_ddl_merge_task_utils.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "share/ob_structured_event_logger.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablet/ob_tablet_create_sstable_param.h"
#include "storage/blocksstable/index_block/ob_macro_meta_temp_store.h"

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

int ObIDDLMergeHelper::get_merge_helper(ObIAllocator &allocator,
                                        const ObDirectLoadType direct_load_type,
                                        ObIDDLMergeHelper *&helper)
{
  int ret = OB_SUCCESS;
  #define BUILD_MERGE_HELPER(helper_type) \
          if (OB_ISNULL(buf = static_cast<char*>(allocator.alloc(sizeof(helper_type))))) { \
            ret = OB_ALLOCATE_MEMORY_FAILED; \
            LOG_WARN("failed to alloc", K(ret), K(direct_load_type)); \
          } else { \
            helper = new (buf) helper_type(); \
          } \

  char *buf = nullptr;
  switch(direct_load_type) {
    case ObDirectLoadType::IDEM_DIRECT_LOAD_DDL:
      BUILD_MERGE_HELPER(ObSNDDLMergeHelperV2);
      break;
    default:
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("not supported direct load type", K(ret), K(direct_load_type));
      break;
  }
  #undef BUILD_MERGE_HELPER
  return ret;
}

int ObIDDLMergeHelper::freeze_ddl_kv(ObDDLTabletMergeDagParamV2 &param)
{
  int ret = OB_SUCCESS;
  ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
  ObTabletHandle tablet_handle;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ObTabletID target_tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;

  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  } else if (OB_FAIL(param.get_tablet_param(target_tablet_id, tablet_param))) {
  } else if (OB_FAIL(ObDDLMergeTaskUtils::freeze_ddl_kv(target_tablet_id,
                                                        param.direct_load_type_,
                                                        param.start_scn_,
                                                        param.ddl_task_param_.snapshot_version_,
                                                        param.ddl_task_param_.data_format_version_))) {
  }
  return ret;
}

int ObSNDDLMergeHelperV2::set_ddl_complete(ObIDag *dag, ObTablet &tablet, ObDDLTabletMergeDagParamV2 &ddl_merge_param)
{
  int ret = OB_SUCCESS;
  ObTabletDDLCompleteArg complete_arg;
  ObArenaAllocator allocator(ObMemAttr("MrgHlpArg"));
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ObStorageSchema *storage_schema = nullptr;
  ObWriteTabletParam *tablet_param = nullptr;
  ObDDLTabletContext *tablet_context = ddl_merge_param.get_tablet_ctx();
  ObTabletID target_tablet_id;
  /* ddl kv has already been freeze in prepare task */
  if (OB_ISNULL(dag) || !ddl_merge_param.is_valid() || OB_ISNULL(tablet_context)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param", K(ret), KP(dag), K(ddl_merge_param), KP(tablet_context));
  } else if (OB_FAIL(tablet.get_ddl_kv_mgr(ddl_kv_mgr_handle, false /* not for repaly*/))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TASK_EXPIRED;
      LOG_INFO("ddl kv mgr not exist", K(ret), K(ddl_merge_param));
    } else {
      LOG_WARN("get ddl kv mgr failed", K(ret), K(ddl_merge_param));
    }
  } else if (OB_FAIL(ddl_merge_param.get_tablet_param(target_tablet_id, tablet_param))) {
  } else if (OB_ISNULL(tablet_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get tablet param", K(ret), K(ddl_merge_param));
  } else if (FALSE_IT(storage_schema = tablet_param ->storage_schema_)) {
  } else if (OB_ISNULL(storage_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage schema should not be null", K(ret), K(ddl_merge_param));
  } else {
    complete_arg.has_complete_          = true;
    complete_arg.tablet_id_             = target_tablet_id;
    complete_arg.direct_load_type_      = ddl_merge_param.direct_load_type_;
    complete_arg.start_scn_             = ddl_merge_param.start_scn_;
    complete_arg.data_format_version_   = ddl_merge_param.ddl_task_param_.data_format_version_;
    complete_arg.snapshot_version_      = ddl_merge_param.ddl_task_param_.snapshot_version_;
    complete_arg.table_key_             = ddl_merge_param.table_key_;
    const ObDDLWriteStat *write_stat = target_tablet_id == tablet_context->lob_meta_tablet_id_ ? &tablet_context->lob_write_stat_ : &tablet_context->write_stat_;
    if (OB_FAIL(complete_arg.set_write_stat(*write_stat))) {
    } else if (OB_FAIL(complete_arg.set_storage_schema(*storage_schema))) {
    } else if (OB_FAIL(ObTabletDDLCompleteMdsHelper::record_ddl_complete_arg_to_mds(complete_arg, allocator))) {
    }
  }
  return ret;
}

int ObSNDDLMergeHelperV2::process_prepare_task(ObIDag *dag,
                                               ObDDLTabletMergeDagParamV2 &ddl_merge_param,
                                               ObIArray<ObDDLSliceRange> &slice_ranges)
{
  int ret = OB_SUCCESS;

  int64_t merge_slice_idx = 0;
  bool for_major = false;

  slice_ranges.reset();
  hash::ObHashSet<int64_t> slice_idxes;
  ObTabletHandle tablet_handle;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ObArray<const ObSSTable*> ddl_sstables;

  ObArray<ObDDLKVHandle> frozen_ddl_kvs;
  const ObSSTable *first_major_sstable = nullptr;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;

  ObTabletID target_tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  ObDDLTabletContext::MergeCtx *merge_ctx = nullptr;
  ObDDLKVQueryParam ddl_kv_query_param;
  if (!ddl_merge_param.is_valid() || nullptr == dag) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_merge_param), KPC(dag));
  } else if (OB_FAIL(ddl_merge_param.get_tablet_param(target_tablet_id, tablet_param))) {
  } else if (FALSE_IT(for_major = ddl_merge_param.for_major_)) {
  } else if (OB_FAIL(slice_idxes.create(DDL_SLICE_BUCKET_NUM, ObMemAttr("slice_idx_set")))) {
  } else if (OB_FAIL(ddl_merge_param.get_merge_ctx(merge_ctx))) {
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be null", K(ret), K(ddl_merge_param));
  }

  /* check major sstable exist */
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(target_tablet_id, tablet_handle))) {
  } else if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
  } else if (OB_FALSE_IT(first_major_sstable = static_cast<ObSSTable *>(
                                                table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)))) {
  }else if (nullptr != first_major_sstable) {          /* if major exist, do nothing */
  } else if (for_major && !ddl_merge_param.for_replay_ && OB_FAIL(set_ddl_complete(dag, *(tablet_handle.get_obj()), ddl_merge_param))) {
    LOG_ERROR("failed to set ddl complete", K(ret));
  }

  /* if for major need to wait ddl complete take effect */
  if (OB_FAIL(ret)) {
  } else if (nullptr != first_major_sstable) {          /* if major exist, do nothing */
  } else if (for_major) {
    ObArenaAllocator arena(ObMemAttr("DDL_Mrg_Pre"));
    ObTabletDDLCompleteMdsUserData user_data;
    if (OB_FAIL(tablet_handle.get_obj()->get_ddl_complete(share::SCN::max_scn(), arena, user_data))) {
      if (OB_EMPTY_RESULT == ret) {
        /* for ddl execute node, should wait take effect */
        ret = ddl_merge_param.for_replay_ ? OB_EAGAIN : OB_DAG_TASK_IS_SUSPENDED;
      }
      LOG_WARN("failed to get ddl complete mds user data", K(ret));
    } else if (!user_data.has_complete_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl complete not take effect", K(ret), K(user_data));
    }
  }

  /* freeze & get frozen ddl kvs */
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TASK_EXPIRED;
      LOG_INFO("ddl kv mgr not exist", K(ret), K(ddl_merge_param));
    } else {
      LOG_WARN("get ddl kv mgr failed", K(ret), K(ddl_merge_param));
    }
  } else if (ddl_merge_param.start_scn_ < tablet_handle.get_obj()->get_tablet_meta().ddl_start_scn_) {
    ret = OB_TASK_EXPIRED;
    LOG_WARN("ddl task expired, skip it", K(ret), K(ddl_merge_param),
            "new_start_scn", tablet_handle.get_obj()->get_tablet_meta().ddl_start_scn_);
  } else if (OB_FALSE_IT(ddl_kv_query_param.ddl_kv_type_ = ObDDLKVType::DDL_KV_FULL)) {
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->get_ddl_kvs(
      true/*frozen_only*/, frozen_ddl_kvs, ddl_kv_query_param))) {
  } else if (OB_FAIL(merge_ctx->ddl_kv_handles_.assign(frozen_ddl_kvs))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < frozen_ddl_kvs.count(); ++i) {
      int max_retry_cnt = 10000;
      while(max_retry_cnt > 0) {
        max_retry_cnt--;
        if (OB_FAIL(frozen_ddl_kvs.at(i).get_obj()->close())) {
          if (OB_EAGAIN != ret) {
            break;
          } else {
            ob_usleep(500);
          }
        } else {
          break;
        }
      }
      if (OB_FAIL(ret)) {
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (nullptr != first_major_sstable) {
    /* do nothing */
  } else if (for_major) {
    if (OB_FAIL(slice_idxes.set_refactored(0))) {
    } else {
      merge_slice_idx = 0; // merge all slice
    }
  } else if (!for_major) {
    if (OB_FAIL(ObDDLMergeTaskUtils::get_merge_slice_idx(frozen_ddl_kvs, merge_slice_idx))) {
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_ddl_memtables(frozen_ddl_kvs, ddl_sstables))) {
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_slice_indexes(ddl_sstables, slice_idxes))) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (nullptr != first_major_sstable) {          /* if major exist, do nothing */
  } else if (OB_FAIL(ddl_merge_param.init_slice_sstable_array(slice_idxes))) {
  } else {
    for (hash::ObHashSet<int64_t>::const_iterator iter = slice_idxes.begin();
        OB_SUCC(ret) && iter != slice_idxes.end();
        ++iter) {
      int64_t start_slice_idx = iter->first;
      int64_t end_slice_idx   = 0 == iter->first ? merge_slice_idx : iter->first;
      if (OB_FAIL(slice_ranges.push_back(ObDDLSliceRange(start_slice_idx, end_slice_idx)))) {
      }
    }
  }

  FLOG_INFO("[DDL_MERGE_TASK][PREPARE] get ddl kv", K(ret), K(frozen_ddl_kvs.count()),  K(merge_slice_idx), K(target_tablet_id));
  return ret;
}

int calc_scn_range(const ObIArray<ObDDLKVHandle> &ddl_kvs,
                   const ObArray<ObSSTable*> &ddl_sstables,
                   share::SCN &start_scn,
                   share::SCN &end_scn,
                   int64_t &snapshot_version)
{
  int ret = OB_SUCCESS;
  /* calc from dump sstalbe */
  if (ddl_kvs.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_kvs));
  } else {
    ObDDLKVHandle first_kv_handle = ddl_kvs.at(0);
    ObDDLKVHandle last_kv_handle = ddl_kvs.at(ddl_kvs.count() - 1);
    start_scn = first_kv_handle.get_obj()->get_start_scn();
    end_scn = last_kv_handle.get_obj()->get_end_scn();
    snapshot_version = first_kv_handle.get_obj()->get_ddl_snapshot_version();
  }

  /* calc from  from dump sstable */
  if (OB_FAIL(ret)) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_sstables.count(); ++i) {
      if (OB_ISNULL(ddl_sstables.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl sstable is null", K(ret), K(i));
      } else if (ObITable::is_ddl_dump_sstable(ddl_sstables.at(i)->get_key().table_type_)) {
        start_scn = share::SCN::min(start_scn, ddl_sstables.at(i)->get_key().scn_range_.start_scn_);
      }
    }
  }
  return ret;
}

int ObSNDDLMergeHelperV2::merge_slice(ObIDag *dag,
                                     ObDDLTabletMergeDagParamV2 &merge_param,
                                     const int64_t start_slice_idx,
                                     const int64_t end_slice_idx)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObArray<ObSSTable*> ddl_sstables;
  ObArray<ObDDLBlockMeta> sorted_metas;
  ObArray<ObDDLBlockMeta> tmp_metas;
  ObDDLWriteStat write_stat;

  ObTabletID tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  ObDDLTabletContext::MergeCtx *merge_ctx = nullptr;

  ObTabletDDLParam ddl_param;

  ObArenaAllocator arena(ObMemAttr("merge_slice"));
  ObTabletDDLCompleteMdsUserData ddl_data;

  if (OB_ISNULL(dag) || start_slice_idx < 0 || end_slice_idx < start_slice_idx) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid param", K(ret), K(dag), K(start_slice_idx), K(end_slice_idx));
  } else if (OB_FAIL(merge_param.get_tablet_param(tablet_id, tablet_param))) {
  } else  if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(tablet_id, tablet_handle))) {
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected err", K(ret), K(merge_param));
  } else if (OB_FAIL(prepare_ddl_param(merge_param, start_slice_idx, end_slice_idx, ddl_param))) {
  } else if (OB_FAIL(merge_param.get_merge_ctx(merge_ctx))) {
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge_ctx should not be null", K(ret));
  }

  int64_t ddl_dump_table_cnt = 0;
  SMART_VAR(ObTableStoreIterator, ddl_sstable_iter) {
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_sstables(ddl_sstable_iter))) {
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_ddl_tables_from_dump_tables(
                                                                            ddl_sstable_iter,
                                                                            ddl_sstables))) {
    } else if (FALSE_IT(ddl_dump_table_cnt = ddl_sstables.count())) {
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_ddl_tables_from_ddl_kvs(merge_ctx->ddl_kv_handles_,
                                                   start_slice_idx,
                                                   merge_param.for_major_ ? INT64_MAX : end_slice_idx,
                                                   ddl_sstables))) {
    } else if (OB_FAIL(ObDDLMergeTaskUtils::get_sorted_meta_array(*tablet_handle.get_obj(),
                                                                  ddl_param,
                                                                  tablet_param->storage_schema_,
                                                                  ddl_sstables,
                                                                  tablet_handle.get_obj()->get_rowkey_read_info(),
                                                                  arena, tmp_metas))) {
    } else if (OB_FAIL(ObDDLMergeTaskUtils::check_idempodency(tmp_metas, sorted_metas, &write_stat))) {
    } else if (merge_param.for_major_) {
      if (OB_FAIL(tablet_handle.get_obj()->get_ddl_complete(share::SCN::max_scn(), arena, ddl_data))) {
        if (OB_EMPTY_RESULT == ret) {
          /* may read mds failed when tablet is deleted*/
          ret = OB_TASK_EXPIRED;
        }
        LOG_WARN("failed to get ddl complete", K(ret), K(ddl_data));
      } else if (!ddl_data.is_valid() || !ddl_data.has_complete_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl complete has not been set", K(ret), K(ddl_data));
      } else if (ddl_data.write_stat_.row_count_ != write_stat.row_count_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl row count not match", K(ret), K(ddl_data), K(write_stat));
      }
    }

    if (OB_FAIL(ret)) {
    } else if (!merge_param.for_major_ && OB_FAIL(calc_scn_range(merge_ctx->ddl_kv_handles_, ddl_sstables, ddl_param.table_key_.scn_range_.start_scn_,
                                      ddl_param.table_key_.scn_range_.end_scn_, ddl_param.snapshot_version_))) {
      LOG_WARN("failed to calc scn range", K(ret));
    }
  } // ddl_sstable_iter

  LOG_INFO("sn ddl merge helper merge slice", KR(ret), K(ddl_sstables.count()), K(ddl_param));

  /* !!! notice !!!
   * sstable meta info rely on previous ddl dump sstable if exist
   * rember to using dump sstable instead of mem table as first ddl sstabe
   */
  ObTableHandleV2 sstable_handle;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObTabletDDLUtil::create_ddl_sstable(*(tablet_handle.get_obj()),
                                                           ddl_param,
                                                           sorted_metas,
                                                           ObArray<MacroBlockId>(),
                                                           ddl_dump_table_cnt > 0 ? ddl_sstables.at(0) : nullptr, /* if dump exist using meta from dump */
                                                           tablet_param->storage_schema_,
                                                           &merge_ctx->mutex_,
                                                           merge_ctx->arena_,
                                                           sstable_handle))) {
  } else if (OB_FAIL(merge_param.set_slice_sstable(start_slice_idx, sstable_handle))) {
  }
  return ret;
}

int ObIDDLMergeHelper::prepare_ddl_param(const ObDDLTabletMergeDagParamV2 &merge_param,
                                         ObTabletDDLParam &ddl_param)
{
  int ret = OB_SUCCESS;
  ObTabletID tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;
  if (!merge_param.is_valid() || !is_supported_direct_load_type(merge_param.direct_load_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_param));
  } else if (OB_FAIL(merge_param.get_tablet_param(tablet_id, tablet_param))) {
  } else if (OB_ISNULL(tablet_param) || OB_ISNULL(tablet_param->storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param and storage schema should not be null", K(ret), KPC(tablet_param));
  } else {
    ddl_param.direct_load_type_    = merge_param.direct_load_type_;
    ddl_param.table_key_           = merge_param.table_key_;
    ddl_param.start_scn_           = merge_param.start_scn_;
    ddl_param.snapshot_version_    = merge_param.ddl_task_param_.snapshot_version_;
    ddl_param.data_format_version_ = merge_param.ddl_task_param_.data_format_version_;
    LOG_INFO("prepare_ddl_param", K(ddl_param));
  }
  return ret;
}

int ObIDDLMergeHelper::prepare_ddl_param(const ObDDLTabletMergeDagParamV2 &merge_param,
                                         const int64_t start_slice_idx,
                                         const int64_t end_slice_idx,
                                         ObTabletDDLParam &ddl_param)
{
  int ret = OB_SUCCESS;
  if (!merge_param.is_valid() ||
      !is_supported_direct_load_type(merge_param.direct_load_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_param), K(merge_param.is_valid()));
  } else if (OB_FAIL(prepare_ddl_param(merge_param, ddl_param))) {
  } else if (merge_param.need_merge_all_slice() && !merge_param.for_major_) {
    //let is_merge_slice()=true, currently only used for inc-major direct load
    ddl_param.table_key_.slice_range_.start_slice_idx_ = 0;
    ddl_param.table_key_.slice_range_.end_slice_idx_   = INT32_MAX;
  } else {
    ddl_param.table_key_.slice_range_.start_slice_idx_ = start_slice_idx;
    ddl_param.table_key_.slice_range_.end_slice_idx_   = end_slice_idx;
  }
  return ret;
}

int ObIDDLMergeHelper::get_rec_scn_from_ddl_kvs(ObDDLTabletMergeDagParamV2 &merge_param)
{
  int ret = OB_SUCCESS;
  // for empty tablet, rec_scn should equal to start_scn
  share::SCN rec_scn = share::SCN::max(merge_param.rec_scn_, merge_param.start_scn_);
  ObTabletHandle tablet_handle;

  ObDDLTabletContext::MergeCtx *merge_ctx = nullptr;
  ObWriteTabletParam *tablet_param = nullptr;
  ObTabletID target_tablet_id;

  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_param));
  } else if (OB_FAIL(merge_param.get_tablet_param(target_tablet_id, tablet_param))) {
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(target_tablet_id, tablet_handle))) {
  } else if (OB_FAIL(merge_param.get_merge_ctx(merge_ctx))) {
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge_ctx should not be null", K(ret));
  } else {
    int64_t ddl_kv_count = merge_ctx->ddl_kv_handles_.count();
    int64_t ddl_dump_count = 0;
    /* get rec scn from ddl kv */
    for (int64_t i = 0; OB_SUCC(ret) && i < merge_ctx->ddl_kv_handles_.count(); ++i) {
      if (!merge_ctx->ddl_kv_handles_.at(i).is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid ddl kv handle", K(ret), K(i));
      } else {
        rec_scn = share::SCN::max(rec_scn, merge_ctx->ddl_kv_handles_.at(i).get_obj()->get_end_scn());
      }
    }
    /* get rec scn from ddl sstable, since ddl kv may be null */
    SMART_VAR(ObTableStoreIterator, ddl_sstable_iter) {
      if (OB_FAIL(tablet_handle.get_obj()->get_ddl_sstables(ddl_sstable_iter))) {
      }
      while(OB_SUCC(ret)) {
        ObITable *table = nullptr;
        if (OB_FAIL(ddl_sstable_iter.get_next(table))) {
          if (OB_ITER_END != ret) {
            LOG_WARN("get next table failed", K(ret));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else if (nullptr == table || OB_UNLIKELY(!table->is_sstable())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected error, table is nullptr", K(ret), KPC(table));
        } else {
          rec_scn = share::SCN::max(rec_scn, table->get_key().scn_range_.end_scn_);
          ddl_dump_count++;
        }
      }
    }
    if (OB_SUCC(ret)) {
      merge_param.rec_scn_ = rec_scn;
      FLOG_INFO("[DDL_MERGE_TASK]get rec scn", K(ret), K(target_tablet_id), K(ddl_kv_count), K(ddl_dump_count), K(rec_scn));
    }
  }
  return ret;
}

int ObIDDLMergeHelper::remove_tablet_from_log_handler(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObSEArray<ObTabletID, 1> tablet_ids;
  ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (nullptr == ls_service) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret));
  } else if (OB_FAIL(tablet_ids.push_back(tablet_id))) {
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
  } else if (OB_FAIL(ls->get_ddl_log_handler()->del_tablets(tablet_ids))) {
  }
  return ret;
}

int ObSNDDLMergeHelperV2::get_rec_scn(ObDDLTabletMergeDagParamV2 &merge_param)
{
  return ObIDDLMergeHelper::get_rec_scn_from_ddl_kvs(merge_param);
}

int ObSNDDLMergeHelperV2::assemble_sstable(ObDDLTabletMergeDagParamV2 &merge_param)
{
  int ret = OB_SUCCESS;

  ObTabletID target_tablet_id;
  ObWriteTabletParam *tablet_param = nullptr;

  ObTabletHandle tablet_handle;
  ObSSTable *major_sstable = nullptr;
  ObDDLKvMgrHandle ddl_kv_mgr_handle;
  ObTablesHandleArray slice_sstable_array;
  ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
  const ObSSTable *first_major_sstable = nullptr;

  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_param));
  } else if (OB_FAIL(merge_param.get_tablet_param(target_tablet_id, tablet_param))) {
  }

  /* check major sstable exist */
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(target_tablet_id, tablet_handle))) {
  } else if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
  } else if (OB_FALSE_IT(first_major_sstable = static_cast<ObSSTable *>(
                                                table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)))) {
  }

  /* update table store */
  if (OB_FAIL(ret)) {
  } else if (nullptr != first_major_sstable) { /* do nothing when major sstable exist */
  } else if (OB_FAIL(ObDDLMergeTaskUtils::build_sstable(merge_param, slice_sstable_array, major_sstable)))  {
  } else if (OB_FAIL(ObDDLMergeTaskUtils::update_tablet_table_store(merge_param, slice_sstable_array, major_sstable))) {
  }

  /* report check sum */
  if (OB_FAIL(ret)) {
  } else if (merge_param.for_major_ &&
             !merge_param.for_replay_ &&
             !merge_param.for_lob_ &&
             OB_FAIL(ObDDLStorageUtil::report_ddl_checksum_from_major_sstable(target_tablet_id,
                                                                       merge_param.ddl_task_param_.target_table_id_,
                                                                       merge_param.ddl_task_param_.execution_id_,
                                                                       merge_param.ddl_task_param_.ddl_task_id_,
                                                                       merge_param.ddl_task_param_.data_format_version_))) {
    LOG_WARN("failed to report ddl checksum", K(ret), K(merge_param));
  }

  /* release ddl kv when build major sstable */
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObDirectLoadMgrUtil::get_tablet_handle(target_tablet_id, tablet_handle))) {
  } else if (OB_FAIL(tablet_handle.get_obj()->get_ddl_kv_mgr(ddl_kv_mgr_handle))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_TASK_EXPIRED;
      LOG_INFO("ddl kv mgr not exist", K(ret), K(merge_param));
    } else {
      LOG_WARN("get ddl kv mgr failed", K(ret), K(merge_param));
    }
  } else if (OB_FAIL(ddl_kv_mgr_handle.get_obj()->release_ddl_kvs(ObDDLKVType::DDL_KV_FULL, merge_param.for_major_ ? share::SCN::max_scn() : merge_param.rec_scn_))) {
  }

  /* remove tablet from log handler */
  if (OB_FAIL(ret)) {
  } else if (merge_param.for_major_ && OB_FAIL(ObIDDLMergeHelper::remove_tablet_from_log_handler(target_tablet_id))) {
      LOG_ERROR("failed to remove tablet from log handler", K(ret), K(target_tablet_id));
  }
  return ret;
}

bool ObSNDDLMergeHelperV2::is_supported_direct_load_type(const ObDirectLoadType direct_load_type)
{
  return ObDirectLoadType::IDEM_DIRECT_LOAD_DDL == direct_load_type;
}


} // namespace  storage
} // namespace oceanbase
