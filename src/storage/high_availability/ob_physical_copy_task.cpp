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

#define USING_LOG_PREFIX STORAGE
#include "storage/high_availability/ob_physical_copy_task.h"
#include "storage/high_availability/ob_restore_helper.h"
#include "observer/ob_server_event_history_table_operator.h"

namespace oceanbase
{
using namespace share;
using namespace compaction;
namespace storage
{
ObPhysicalCopyTask::ObCopyMacroBlockHelperReader::ObCopyMacroBlockHelperReader()
  : allocator_("HelperMacReader"),
    helper_(nullptr),
    is_inited_(false)
{
}

ObPhysicalCopyTask::ObCopyMacroBlockHelperReader::~ObCopyMacroBlockHelperReader()
{
  if (OB_NOT_NULL(helper_)) {
    helper_->destroy();
    allocator_.free(helper_);
    helper_ = nullptr;
  }
  is_inited_ = false;
  allocator_.reset();
}

int ObPhysicalCopyTask::ObCopyMacroBlockHelperReader::init(
    restore::ObIRestoreHelper *proto_helper,
    const ObITable::TableKey &table_key,
    const ObCopyMacroRangeInfo &range_info,
    const share::SCN &backfill_tx_scn,
    const int64_t data_version,
    ObMacroBlockReuseMgr *reuse_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(proto_helper) || !table_key.is_valid() || !range_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(proto_helper), K(table_key), K(range_info));
  } else if (OB_FAIL(proto_helper->copy_for_task(allocator_, helper_))) {
    LOG_WARN("failed to copy helper for task", K(ret), KP(proto_helper));
  } else if (OB_ISNULL(helper_) || !helper_->is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task helper should not be null or invalid", K(ret), KP(helper_));
  } else if (OB_FAIL(helper_->init_for_macro_block_copy(
      table_key, range_info, backfill_tx_scn, data_version, reuse_mgr))) {
    LOG_WARN("failed to init helper for macro block copy", K(ret), K(table_key), K(range_info));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObPhysicalCopyTask::ObCopyMacroBlockHelperReader::get_next_macro_block(CopyMacroBlockReadData &read_data)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT || OB_ISNULL(helper_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("helper macro reader not init", K(ret), K_(is_inited), KP(helper_));
  } else if (OB_FAIL(helper_->fetch_next_macro_block(read_data))) {
    LOG_WARN("failed to fetch next macro block", K(ret), KP_(helper));
  }
  return ret;
}

/******************ObPhysicalCopyTask*********************/
ObPhysicalCopyTask::ObPhysicalCopyTask()
  : ObITask(TASK_TYPE_MIGRATE_COPY_PHYSICAL),
    is_inited_(false),
    copy_ctx_(nullptr),
    finish_task_(nullptr),
    copy_table_key_(),
    copy_macro_range_info_(),
    task_idx_(0)
{
}

ObPhysicalCopyTask::~ObPhysicalCopyTask()
{
}

int ObPhysicalCopyTask::init(
    ObPhysicalCopyCtx *copy_ctx,
    ObSSTableCopyFinishTask *finish_task)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("physical copy task init tiwce", K(ret));
  } else if (OB_ISNULL(copy_ctx) || !copy_ctx->is_valid() || OB_ISNULL(finish_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("physical copy task get invalid argument", K(ret), KPC(copy_ctx), KPC(finish_task));
  } else if (OB_FAIL(build_macro_block_copy_info_(finish_task))) {
    LOG_WARN("failed to build macro block copy info", K(ret), KPC(copy_ctx));
  } else {
    copy_ctx_ = copy_ctx;
    finish_task_ = finish_task;
    task_idx_ = finish_task->get_next_copy_task_id();
    is_inited_ = true;
  }
  return ret;
}

int ObPhysicalCopyTask::build_macro_block_copy_info_(ObSSTableCopyFinishTask *finish_task)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(finish_task)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("build macro block copy info get invalid argument", K(ret), KP(finish_task));
  } else if (OB_FAIL(finish_task->get_next_macro_block_copy_info(copy_table_key_, copy_macro_range_info_))) {
    if (OB_ITER_END == ret) {
    } else {
      LOG_WARN("failed to get macro block copy info", K(ret));
    }
  } else {
    LOG_INFO("succeed get macro block copy info", K(copy_table_key_), K(copy_macro_range_info_));
  }
  return ret;
}

int ObPhysicalCopyTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObMacroBlocksWriteCtx copied_ctx;
  ObCopyTabletStatus::STATUS status = ObCopyTabletStatus::MAX_STATUS;
  ObTabletCopyFinishTask *tablet_finish_task = nullptr;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else if (copy_ctx_->ha_dag_->get_ha_dag_net_ctx()->is_failed()) {
    FLOG_INFO("ha dag net is already failed, skip physical copy task", KPC(copy_ctx_));
  } else if (OB_FAIL(finish_task_->get_tablet_finish_task(tablet_finish_task))) {
    LOG_WARN("failed to get tablet finish task", K(ret), KPC(copy_ctx_));
  } else if (OB_FAIL(tablet_finish_task->get_tablet_status(status))) {
    LOG_WARN("failed to get tablet status", K(ret), KPC(copy_ctx_));
  } else if (ObCopyTabletStatus::TABLET_NOT_EXIST == status) {
    FLOG_INFO("tablet is not exist in src, skip physical copy task", KPC(copy_ctx_));
  } else {
    if (copy_ctx_->tablet_id_.is_inner_tablet() || copy_ctx_->tablet_id_.is_ls_inner_tablet()) {
    } else {
      DEBUG_SYNC(FETCH_MACRO_BLOCK);
    }

    if (OB_SUCC(ret) && copy_macro_range_info_->macro_block_count_ > 0) {
      if (OB_FAIL(fetch_macro_block_with_retry_(copied_ctx))) {
        LOG_WARN("failed to fetch major block", K(ret), K(copy_table_key_), KPC(copy_macro_range_info_));
      } else if (copy_macro_range_info_->macro_block_count_ != copied_ctx.get_macro_block_count()) {
        ret = OB_ERR_SYS;
        LOG_ERROR("list count not match", K(ret), KPC(copy_macro_range_info_),
            K(copied_ctx.get_macro_block_count()), K(copied_ctx));
      }
    }
    copy_ctx_->total_macro_count_ += copied_ctx.get_macro_block_count(); 
    copy_ctx_->reuse_macro_count_ += copied_ctx.use_old_macro_block_count_;
    LOG_INFO("physical copy task finish", K(ret), KPC(copy_macro_range_info_), KPC(copy_ctx_));
  }

  if (OB_SUCCESS != (tmp_ret = record_server_event_())) {
    LOG_WARN("failed to record server event", K(tmp_ret), K(ret));
  }

  if (OB_FAIL(ret)) {
    if (OB_TABLET_NOT_EXIST == ret) {
      //overwrite ret
      status = ObCopyTabletStatus::TABLET_NOT_EXIST;
      if (OB_FAIL(tablet_finish_task->set_tablet_status(status))) {
        LOG_WARN("failed to set copy tablet status", K(ret), K(status), KPC(copy_ctx_));
      }
    }
  }

  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = ObStorageHADagUtils::deal_with_fo(ret, copy_ctx_->ha_dag_))) {
      LOG_WARN("failed to deal with fo", K(ret), K(tmp_ret), KPC(copy_ctx_));
    }
  }

  return ret;
}

int ObPhysicalCopyTask::fetch_macro_block_with_retry_(
    ObMacroBlocksWriteCtx &copied_ctx)
{
  int ret = OB_SUCCESS;
  int64_t retry_times = 0;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else {
    while (retry_times < MAX_RETRY_TIMES) {
      if (retry_times > 0) {
        LOG_INFO("retry get major block", K(retry_times));
      }
      if (OB_FAIL(fetch_macro_block_(retry_times, copied_ctx))) {
        STORAGE_LOG(WARN, "failed to fetch major block", K(ret), K(retry_times));
      }

      if (OB_SUCC(ret)) {
        break;
      }

      if (OB_FAIL(ret)) {
        if (OB_TABLET_NOT_EXIST == ret) {
          break;
        } else {
          copied_ctx.clear();
          retry_times++;
          ob_usleep(OB_FETCH_MAJOR_BLOCK_RETRY_INTERVAL);
        }
      }
    }
  }

  return ret;
}

int ObPhysicalCopyTask::fetch_macro_block_(
    const int64_t retry_times,
    ObMacroBlocksWriteCtx &copied_ctx)
{
  int ret = OB_SUCCESS;
  ObStorageHAMacroBlockWriter *writer = NULL;
  ObICopyMacroBlockReader *reader = NULL;
  ObIndexBlockRebuilder index_block_rebuilder;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy physical task do not init", K(ret));
  } else {
    LOG_INFO("init reader", K(copy_table_key_));
    if (OB_UNLIKELY(task_idx_ < 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected task_idx_", K(ret), K(task_idx_));
    } else if (OB_FAIL(index_block_rebuilder.init(
            *copy_ctx_->sstable_index_builder_, &task_idx_, copy_ctx_->table_key_))) {
      LOG_WARN("failed to init index block rebuilder", K(ret), K(copy_table_key_));
    } else if (OB_FAIL(get_macro_block_reader_(reader))) {
      LOG_WARN("fail to get macro block reader", K(ret));
    } else if (OB_FAIL(get_macro_block_writer_(reader, &index_block_rebuilder, writer))) {
      LOG_WARN("failed to get macro block writer", K(ret), K(copy_table_key_));
    } else if (OB_FAIL(writer->process(copied_ctx, *copy_ctx_->ha_dag_->get_ha_dag_net_ctx()))) {
      LOG_WARN("failed to process writer", K(ret), K(copy_table_key_));
    } else if (copy_macro_range_info_->macro_block_count_ != copied_ctx.get_macro_block_count()) {
      ret = OB_ERR_SYS;
      LOG_ERROR("list count not match", K(ret), K(copy_table_key_), KPC(copy_macro_range_info_),
          K(copied_ctx.get_macro_block_count()), K(copied_ctx));
    } 

#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = OB_E(EventTable::EN_MIGRATE_FETCH_MACRO_BLOCK) OB_SUCCESS;
      if (OB_FAIL(ret)) {
        if (retry_times == 0) {
        } else {
          ret = OB_SUCCESS;
        }
        STORAGE_LOG(ERROR, "fake EN_MIGRATE_FETCH_MACRO_BLOCK", K(ret));
      }
    }
#endif

    if (FAILEDx(index_block_rebuilder.close())) {
      LOG_WARN("failed to close index block builder", K(ret), K(copied_ctx));
    }

    if (NULL != reader) {
      free_macro_block_reader_(reader);
    }
    if (NULL != writer) {
      free_macro_block_writer_(writer);
    }
  }
  return ret;
}

int ObPhysicalCopyTask::get_macro_block_reader_(
    ObICopyMacroBlockReader *&reader)
{
  int ret = OB_SUCCESS;
  reader = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("physical copy task do not init", K(ret));
  } else if (OB_ISNULL(copy_ctx_) || OB_ISNULL(copy_ctx_->helper_) || OB_ISNULL(copy_macro_range_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("copy ctx/helper/macro range info should not be NULL", K(ret), KP(copy_ctx_), KP(copy_macro_range_info_));
  } else {
    // Helper-driven macro block reader: copy a task-local helper via copy_for_task(), and own its lifecycle.
    ObCopyMacroBlockHelperReader *tmp_reader = MTL_NEW(ObCopyMacroBlockHelperReader, "HelperMacReader");
    if (OB_ISNULL(tmp_reader)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc helper macro reader", K(ret));
    } else {
      const share::SCN backfill_tx_scn = finish_task_->get_sstable_param()->basic_meta_.filled_tx_scn_;
      const int64_t data_version = 0; // TODO: compute data version for macro block reuse if needed
      if (OB_FAIL(tmp_reader->init(copy_ctx_->helper_, copy_table_key_, *copy_macro_range_info_,
                                   backfill_tx_scn, data_version, copy_ctx_->macro_block_reuse_mgr_))) {
        LOG_WARN("failed to init helper macro reader", K(ret));
        MTL_DELETE(ObCopyMacroBlockHelperReader, "HelperMacReader", tmp_reader);
        tmp_reader = nullptr;
      } else {
        reader = tmp_reader;
        tmp_reader = nullptr;
      }
    }
  }
  return ret;
}

int ObPhysicalCopyTask::get_macro_block_writer_(
    ObICopyMacroBlockReader *reader,
    ObIndexBlockRebuilder *index_block_rebuilder,
    ObStorageHAMacroBlockWriter *&writer)
{
  int ret = OB_SUCCESS;
  ObStorageHAMacroBlockWriter *tmp_writer = nullptr;
  const ObMigrationSSTableParam *sstable_param = nullptr;
  const bool is_shared_storage = GCTX.is_shared_storage_mode(); 
  if (OB_ISNULL(reader) || OB_ISNULL(index_block_rebuilder)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("macro block writer get invalid argument", K(ret), KP(reader), KP(index_block_rebuilder));
  } else if (FALSE_IT(sstable_param = finish_task_->get_sstable_param())) {
  } else if (OB_ISNULL(sstable_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("src sstable param is null", K(ret), KP(finish_task_));
  } else {
    if (!is_shared_storage) {
      tmp_writer = MTL_NEW(ObStorageHALocalMacroBlockWriter, "HAMacroObWriter");
    } else {
#ifdef OB_BUILD_SHARED_STORAGE
      if (sstable_param->is_shared_macro_blocks_sstable()) {
        tmp_writer = MTL_NEW(ObStorageHASharedMacroBlockWriter, "HAMacroObWriter");
      } else {
        tmp_writer = MTL_NEW(ObStorageHALocalMacroBlockWriter, "HAMacroObWriter");
      }
#endif
    }

    if (OB_ISNULL(tmp_writer)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory", K(ret));
    } else if (OB_FAIL(tmp_writer->init(copy_ctx_->tenant_id_, copy_ctx_->ls_id_, copy_ctx_->tablet_id_, 
        this->get_dag()->get_dag_id(), sstable_param, reader, index_block_rebuilder, copy_ctx_->extra_info_))) {
      STORAGE_LOG(WARN, "failed to init macro block writer", K(ret), KPC(copy_ctx_));
    } else {
      writer = tmp_writer;
      tmp_writer = nullptr;
    }

    if (OB_NOT_NULL(tmp_writer)) {
      free_macro_block_writer_(tmp_writer);
    }
  }
  return ret;
}

int ObPhysicalCopyTask::generate_next_task(ObITask *&next_task)
{
  int ret = OB_SUCCESS;
  ObPhysicalCopyTask *tmp_next_task = nullptr;
  bool is_iter_end = false;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not init", K(ret));
  } else if (OB_FAIL(finish_task_->check_is_iter_end(is_iter_end))) {
    LOG_WARN("failed to check is iter end", K(ret));
  } else if (is_iter_end) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(dag_->alloc_task(tmp_next_task))) {
    LOG_WARN("failed to alloc task", K(ret));
  } else if (OB_FAIL(tmp_next_task->init(copy_ctx_, finish_task_))) {
    LOG_WARN("failed to init next task", K(ret), K(*copy_ctx_));
  } else {
    next_task = tmp_next_task;
  }

  return ret;
}

void ObPhysicalCopyTask::free_macro_block_reader_(ObICopyMacroBlockReader *&reader)
{
  if (OB_NOT_NULL(reader)) {
    // Use virtual destructor dispatch to release helper owned by derived reader.
    MTL_DELETE(ObICopyMacroBlockReader, "HelperMacReader", reader);
    reader = nullptr;
  }
}

void ObPhysicalCopyTask::free_macro_block_writer_(ObStorageHAMacroBlockWriter *&writer)
{
  MTL_DELETE(ObStorageHAMacroBlockWriter, "MacroObWriter", writer);
}

int ObPhysicalCopyTask::record_server_event_()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(copy_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("copy ctx should not be null", K(ret), KPC_(copy_ctx));
  } else {
    SERVER_EVENT_ADD("storage_ha", "physical_copy_task",
        "tenant_id", copy_ctx_->tenant_id_,
        "ls_id", copy_ctx_->ls_id_.id(),
        "tablet_id", copy_ctx_->tablet_id_.id(),
        "table_key", copy_table_key_,
        "macro_block_count", copy_macro_range_info_->macro_block_count_);
  } 
  return ret;
}

}
}
