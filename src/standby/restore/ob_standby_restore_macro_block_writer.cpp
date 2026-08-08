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
#include "ob_standby_restore_macro_block_writer.h"
#include "share/config/ob_server_config.h"
#include "share/ob_io_device_helper.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace blocksstable;

namespace storage
{
ObStandbyRestoreMacroBlockWriter::ObStandbyRestoreMacroBlockWriter()
 : is_inited_(false),
   tenant_id_(OB_INVALID_ID),
   ls_id_(),
   tablet_id_(),
   copy_id_(),
   sstable_param_(nullptr),
   reader_(NULL),
   index_block_rebuilder_(nullptr),
   macro_checker_(),
   extra_info_(nullptr)
{
}

int ObStandbyRestoreMacroBlockWriter::init(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const common::ObTabletID &tablet_id,
    const share::ObTaskId &copy_id,
    const ObMigrationSSTableParam *sstable_param,
    ObICopyMacroBlockReader *reader,
    ObIndexBlockRebuilder *index_block_rebuilder,
    ObCopyTabletRecordExtraInfo *extra_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("writer should not be init twice", K(ret));
  } else if (OB_INVALID_ID == tenant_id
	          || !ls_id.is_valid()
            || !tablet_id.is_valid()
	          || copy_id.is_invalid()
            || OB_ISNULL(sstable_param)
            || OB_ISNULL(reader)
            || OB_ISNULL(index_block_rebuilder)
            || OB_ISNULL(extra_info))
  {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tenant_id), K(tablet_id), KP(sstable_param),
        KP(reader), KP(index_block_rebuilder), KP(extra_info));
  } else if (OB_FAIL(check_sstable_param_for_init_(sstable_param))) {
    LOG_WARN("failed to check sstable param", K(ret));
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = ls_id;
    tablet_id_ = tablet_id;
    copy_id_.set(copy_id);
    sstable_param_ = sstable_param;
    reader_ = reader;
    index_block_rebuilder_ = index_block_rebuilder;
    extra_info_ = extra_info;
    is_inited_ = true;
  }
  return ret;
}

int ObStandbyRestoreMacroBlockWriter::check_macro_block_(
    const blocksstable::ObBufferReader &data)
{
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (NULL == data.data() || data.length() < 0 || data.length() > data.capacity()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("invalid data", K(ret), K(data));
  } else {
    const ObMacroBlockCheckLevel check_level = ObMacroBlockCheckLevel::CHECK_LEVEL_PHYSICAL;
    if (OB_FAIL(macro_checker_.check(data.data(), data.length(), check_level))) {
      LOG_ERROR("failed to check macro block", K(ret), K(data), K(check_level));
    }
  }

#ifdef ERRSIM
  if (OB_SUCC(ret)) {
    ret = OB_E(EventTable::EN_RESTORE_MACRO_CRC_ERROR) OB_SUCCESS;
    if (OB_FAIL(ret)) {
      LOG_INFO("ERRSIM check_macro_block", K(ret));
    }
  }
#endif
  return ret;
}

int ObStandbyRestoreMacroBlockWriter::process(
    blocksstable::ObMacroBlocksWriteCtx &copied_ctx)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  blocksstable::ObBufferReader data(NULL, 0, 0);
  ObStorageObjectOpt opt;
  blocksstable::ObDatumRow macro_meta_row;
  blocksstable::ObStorageObjectWriteInfo write_info;
  blocksstable::ObStorageObjectHandle write_handle;
  ObICopyMacroBlockReader::CopyMacroBlockReadData read_data;
  copied_ctx.reset();
  int64_t write_count = 0;
  int64_t reuse_count = 0;
  int64_t log_seq_num = 0;
  int64_t data_size = 0;
  int64_t write_size = 0;
  int64_t macro_meta_row_pos = 0;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret));
  } else if (OB_FAIL(macro_meta_row.init(OB_MAX_ROWKEY_COLUMN_NUMBER + 1))) {
    // use max row key cnt + 1 as capacity, because meta row is kv
    STORAGE_LOG(WARN, "failed to init macro meta row", K(ret));
  } else {
    STORAGE_LOG(INFO, "macro block writer begin", K_(tenant_id), K_(ls_id), K_(tablet_id),
        K_(copy_id), KPC_(sstable_param));
    while (OB_SUCC(ret)) {
      if (OB_FAIL(LOCAL_DEVICE_INSTANCE.check_space_full(0 /*required_size*/))) {
        STORAGE_LOG(ERROR, "failed to check disk space", K(ret), K_(tenant_id), K_(ls_id),
            K_(tablet_id), K_(copy_id), KPC_(sstable_param));
        break;
      } else if (OB_FAIL(reader_->get_next_macro_block(read_data))) {
        if (OB_ITER_END != ret) {
          LOG_ERROR("failed to get next macro block", K(ret), K_(tenant_id), K_(ls_id),
              K_(tablet_id), K_(copy_id), KPC_(sstable_param), KP_(reader));
        } else {
          LOG_INFO("get next macro block end");
          ret = OB_SUCCESS;
        }
        break;
      } else if (!read_data.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        STORAGE_LOG(ERROR, "invalid read data", K(ret), K(read_data), K_(tenant_id),
            K_(ls_id), K_(tablet_id), K_(copy_id), KPC_(sstable_param));
      } else if (read_data.is_macro_meta()) {
        const MacroBlockId &macro_id = read_data.macro_meta_->get_macro_id();
        if (ObIndexBlockRowHeader::DEFAULT_IDX_ROW_MACRO_ID == macro_id) {
          ret = OB_INVALID_ARGUMENT;
          STORAGE_LOG(ERROR, "invalid macro id (id is default)", K(ret), K(macro_id),
              K(read_data), K_(tenant_id), K_(ls_id), K_(tablet_id), K_(copy_id), KPC_(sstable_param));
        } else if (OB_FAIL(copied_ctx.add_macro_block_id(macro_id))) {
          STORAGE_LOG(ERROR, "fail to add macro id", K(ret), K(macro_id), K_(tenant_id),
              K_(ls_id), K_(tablet_id), K_(copy_id), KPC_(sstable_param));
        } else if (OB_FAIL(index_block_rebuilder_->append_macro_row(*read_data.macro_meta_))) {
          STORAGE_LOG(ERROR, "failed to append macro row", K(ret), KPC(read_data.macro_meta_),
              K_(tenant_id), K_(ls_id), K_(tablet_id), K_(copy_id), KPC_(sstable_param));
        } else {
          copied_ctx.increment_old_block_count();
          ++reuse_count;
        }
      } else if (read_data.is_macro_data()) {
        ObBufferReader data = read_data.macro_data_;
        MacroBlockId macro_block_id = read_data.macro_block_id_;

        if (OB_FAIL(check_macro_block_(data))) {
          STORAGE_LOG(ERROR, "failed to check macro block, fatal error", K(ret), K(write_count),
              K(data), K(macro_block_id), K_(tenant_id), K_(ls_id), K_(tablet_id),
              K_(copy_id), KPC_(sstable_param));
          ret = OB_INVALID_DATA;// overwrite ret
        } else if (!write_handle.is_empty() && OB_FAIL(write_handle.wait())) {
          STORAGE_LOG(ERROR, "failed to wait write handle", K(ret), K(write_info),
              K(macro_block_id), K_(tenant_id), K_(ls_id), K_(tablet_id), K_(copy_id),
              KPC_(sstable_param));
        } else if (OB_FAIL(set_macro_write_info_(macro_block_id, write_info, opt)))  {
          LOG_ERROR("failed to set macro write info", K(ret), K(macro_block_id), K_(tenant_id),
              K_(ls_id), K_(tablet_id), K_(copy_id), KPC_(sstable_param));
        } else if (OB_FAIL(write_macro_block_(opt, write_info, write_handle, copied_ctx, data))) {
          LOG_ERROR("failed to write macro block", K(ret), K(opt), K(macro_block_id),
              K_(tenant_id), K_(ls_id), K_(tablet_id), K_(copy_id), KPC_(sstable_param));
        } else {
          ObTaskController::get().allow_next_syslog();
          ++write_count;
          write_size += data.capacity();
          LOG_INFO("success copy macro block", K(write_count));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(ERROR, "invalid read data", K(ret), K(read_data), K_(tenant_id),
            K_(ls_id), K_(tablet_id), K_(copy_id), KPC_(sstable_param));
      }
    }

    if (!write_handle.is_empty()) {
      int tmp_ret = write_handle.wait();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("failed to wait write handle", K(ret), K(tmp_ret), K(write_info));
        if (OB_SUCC(ret)) {
          ret = tmp_ret;
        }
      }
    }

    data_size = reader_->get_data_size();

    int64_t cost_time_ms = (ObTimeUtility::current_time() - start_time) / 1000;
    int64_t data_size_KB = data_size / 1024;
    int64_t write_size_KB = write_size / 1024;

    int64_t total_speed_KB = 0;
    int64_t write_speed_KB = 0;
    if (cost_time_ms > 0) {
      total_speed_KB = data_size_KB * 1000 / cost_time_ms;
      write_speed_KB = write_size_KB * 1000 / cost_time_ms;
    }

    extra_info_->add_cost_time_ms(cost_time_ms);
    extra_info_->add_total_data_size(data_size);
    extra_info_->add_write_data_size(write_size);

    STORAGE_LOG(INFO, "finish copy macro block data", K(ret),
                "macro_count", copied_ctx.get_macro_block_count(), K(write_count), K(reuse_count),
                K(cost_time_ms), "read_size_B", data_size, K(write_size), K(total_speed_KB), K(write_speed_KB));
  }

  return ret;
}

int ObStandbyRestoreMacroBlockWriter::write_macro_block_(
    const ObStorageObjectOpt &opt,
    blocksstable::ObStorageObjectWriteInfo &write_info,
    blocksstable::ObStorageObjectHandle &write_handle,
    blocksstable::ObMacroBlocksWriteCtx &copied_ctx,
    blocksstable::ObBufferReader &data)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else {
    write_info.buffer_ = data.data();
    write_info.size_ = data.upper_align_length();
    write_handle.reset();

    if (OB_FAIL(ObObjectManager::async_write_object(opt, write_info, write_handle))) {
      LOG_WARN("fail to async write block", K(ret), K(write_info), K(write_handle));
    } else if (OB_FAIL(copied_ctx.add_macro_block_id(write_handle.get_macro_id()))) {
      LOG_WARN("fail to add macro id", K(ret), "macro id", write_handle.get_macro_id());
    } else if (OB_FAIL(append_macro_row_(data.data(), data.capacity(), write_handle.get_macro_id()))) {
      LOG_WARN("failed to append macro row", K(ret), K(write_handle));
    }
  }
  return ret;
}


// ObStandbyRestoreLocalMacroBlockWriter
int ObStandbyRestoreLocalMacroBlockWriter::check_sstable_param_for_init_(const ObMigrationSSTableParam *sstable_param) const
{
  UNUSED(sstable_param);
  return OB_SUCCESS;
}

int ObStandbyRestoreLocalMacroBlockWriter::set_macro_write_info_(
    const MacroBlockId &macro_block_id,
    blocksstable::ObStorageObjectWriteInfo &write_info,
    blocksstable::ObStorageObjectOpt &opt)
{
  int ret = OB_SUCCESS;
  UNUSED(macro_block_id);
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret));
  } else if (OB_ISNULL(index_block_rebuilder_)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "index_block_rebuilder_ should not be nullptr", KR(ret), KP(index_block_rebuilder_));
  } else {
    write_info.io_desc_.set_wait_event(ObWaitEventIds::DB_FILE_COMPACT_WRITE);
    write_info.io_desc_.set_sys_module_id(ObIOModule::SSTABLE_MACRO_BLOCK_WRITE_IO);
    write_info.io_desc_.set_sealed();
    write_info.io_timeout_ms_ = (GCONF._data_storage_io_timeout / 1000L);
    write_info.offset_ = 0;
    opt.set_data_macro_object_opt();
  }
  return ret;
}

int ObStandbyRestoreLocalMacroBlockWriter::append_macro_row_(
    const char *buf,
    const int64_t size,
    const blocksstable::MacroBlockId &macro_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(index_block_rebuilder_->append_macro_row(buf, size, macro_id, -1 /*absolute_row_offset*/))) {
    LOG_WARN("failed to append macro row", K(ret), K(macro_id));
  }
  return ret;
}



} // storage
} // oceanbase
