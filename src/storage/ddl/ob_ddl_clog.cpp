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

#include "ob_ddl_clog.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ddl/ob_ddl_merge_schedule.h"
#include "storage/ddl/ob_tablet_fork_task.h"
namespace oceanbase
{

using namespace blocksstable;
using namespace share;
using namespace common;

namespace storage
{

ObDDLClogCbStatus::ObDDLClogCbStatus()
  : the_other_release_this_(false), state_(ObDDLClogState::STATE_INIT), ret_code_(OB_SUCCESS)
{
}

bool ObDDLClogCbStatus::try_set_release_flag()
{
  return ATOMIC_BCAS(&the_other_release_this_, false, true);
}

ObDDLClogCb::ObDDLClogCb()
  : status_()
{
}

int ObDDLClogCb::on_success()
{
  status_.set_state(STATE_SUCCESS);
  try_release();
  return OB_SUCCESS;
}

int ObDDLClogCb::on_failure()
{
  status_.set_state(STATE_FAILED);
  try_release();
  return OB_SUCCESS;
}

void ObDDLClogCb::try_release()
{
  if (status_.try_set_release_flag()) {
  } else {
    op_free(this);
  }
}

ObDDLMacroBlockClogCb::ObDDLMacroBlockClogCb()
  : is_inited_(false), status_(), macro_block_id_(),
    data_buffer_lock_(), is_data_buffer_freed_(false), ddl_macro_block_(), snapshot_version_(0),
    data_format_version_(0), direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID),
    block_checksum_(0), is_macro_block_exist_(false)
{}

ObDDLMacroBlockClogCb::~ObDDLMacroBlockClogCb()
{
  int ret = OB_SUCCESS;
  if (macro_block_id_.is_valid() && OB_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(macro_block_id_))) {
    LOG_ERROR("dec ref failed", K(ret), K(macro_block_id_), K(common::lbt()));
  }
  macro_block_id_.reset();
}

int ObDDLMacroBlockClogCb::init(const storage::ObDDLMacroBlockRedoInfo &redo_info,
                                const blocksstable::MacroBlockId &macro_block_id,
                                ObTabletHandle &tablet_handle,
                                const ObDirectLoadType &direct_load_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_UNLIKELY(!redo_info.is_valid() || !macro_block_id.is_valid()
                         || !is_valid_direct_load(direct_load_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(redo_info), K(macro_block_id));
  } else if (OB_FAIL(OB_STORAGE_OBJECT_MGR.inc_ref(macro_block_id))) {
  } else {
    macro_block_id_ = macro_block_id;
    tablet_handle_ = tablet_handle;
    snapshot_version_ = redo_info.table_key_.get_snapshot_version();
    data_format_version_ = redo_info.data_format_version_;
    direct_load_type_ = direct_load_type;
    if (OB_FAIL(ddl_macro_block_.block_handle_.set_block_id(macro_block_id_))) {
    } else if (OB_FAIL(ddl_macro_block_.set_data_macro_meta(macro_block_id_, 
                                                            redo_info.data_buffer_.ptr(),
                                                            redo_info.data_buffer_.length(),
                                                            redo_info.block_type_))) {
    } else {
      ddl_macro_block_.block_type_ = redo_info.block_type_;
      ddl_macro_block_.logic_id_ = redo_info.logic_id_;
      ddl_macro_block_.ddl_start_scn_ = redo_info.start_scn_;
      ddl_macro_block_.table_key_ = redo_info.table_key_;
      ddl_macro_block_.merge_slice_idx_ = redo_info.merge_slice_idx_;
    }
  }
  ObTablet *tablet = nullptr;
  ObDDLKvMgrHandle kv_mgr_handle;
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(tablet = tablet_handle_.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is nullptr", K(ret));
  } else if (is_idem_type(direct_load_type_)) {
    /* check idempotence, if already exist, skip set macro block in ddl kv */
    if (OB_FAIL(tablet->get_ddl_kv_mgr(kv_mgr_handle))) {
    } else if (!kv_mgr_handle.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl kv mgr handle not valid", K(ret));
    } else if (OB_FAIL(kv_mgr_handle.get_obj()->calc_idem_block_checksum(redo_info.block_type_,
                                                                         direct_load_type_,
                                                                         redo_info.data_buffer_.ptr(),
                                                                         redo_info.data_buffer_.length(),
                                                                         block_checksum_))) {
    } else if (OB_FAIL(kv_mgr_handle.get_obj()->check_idem_block_exist(ddl_macro_block_.block_type_,
                                                                       direct_load_type_,
                                                                       ddl_macro_block_.logic_id_,
                                                                       block_checksum_,
                                                                       ddl_macro_block_.table_key_.table_type_,
                                                                       is_macro_block_exist_))) {
    }
  }
  return ret;
}

void ObDDLMacroBlockClogCb::try_release()
{
  {
    ObSpinLockGuard data_buffer_guard(data_buffer_lock_);
    is_data_buffer_freed_ = true;
  }
  if (status_.try_set_release_flag()) {
  } else {
    op_free(this);
  }
}

int ObDDLMacroBlockClogCb::on_success()
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  ObDDLKvMgrHandle kv_mgr_handle;
  if (OB_ISNULL(tablet = tablet_handle_.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is nullptr", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (is_macro_block_exist_) {
    /* do nothing skip relay it*/
  } else if (FALSE_IT(ddl_macro_block_.scn_ = __get_scn())) {
  } else if (OB_FAIL(ObDDLKVPendingGuard::set_macro_block(
      tablet, ddl_macro_block_, snapshot_version_, data_format_version_, direct_load_type_))) {
    if (OB_ENTRY_EXIST == ret && is_idem_type(direct_load_type_)) {
      ret = OB_SUCCESS;
      LOG_INFO("receive repeat macro block, skip", K(ret), K(ddl_macro_block_));
    } else {
      LOG_WARN("set macro block into ddl kv failed", K(ret), KPC(tablet), K(ddl_macro_block_),
              K(snapshot_version_), K(data_format_version_), K(direct_load_type_));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_idem_type(direct_load_type_) && !is_macro_block_exist_) {
    /* set checksum */
    if (OB_FAIL(tablet->get_ddl_kv_mgr(kv_mgr_handle))) {
    } else if (!kv_mgr_handle.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl kv mgr handle not valid", K(ret));
    } else if (OB_FAIL(kv_mgr_handle.get_obj()->set_idem_block_checksum(ddl_macro_block_.block_type_,
                                                                        direct_load_type_,
                                                                        ddl_macro_block_.logic_id_,
                                                                        block_checksum_,
                                                                        ddl_macro_block_.table_key_.table_type_))) {
   } else {
    FLOG_INFO("set block checksum success", K(ret), K(ddl_macro_block_), K(block_checksum_));
   }
  }

  status_.set_ret_code(ret);
  status_.set_state(STATE_SUCCESS);
  try_release();
  return OB_SUCCESS; // force return success
}

int ObDDLMacroBlockClogCb::on_failure()
{
  status_.set_state(STATE_FAILED);
  try_release();
  return OB_SUCCESS;
}

DEFINE_SERIALIZE(ObDDLClogHeader)
{
  int ret = OB_SUCCESS;
  int64_t tmp_pos = pos;

  if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(buf_len));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, tmp_pos, static_cast<int64_t>(ddl_clog_type_)))) {
  } else {
    pos = tmp_pos;
  }
  return ret;
}

DEFINE_DESERIALIZE(ObDDLClogHeader)
{
  int ret = OB_SUCCESS;
  int64_t tmp_pos = pos;

  int64_t log_type = 0;
  if (OB_ISNULL(buf) || data_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(data_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, data_len, tmp_pos, &log_type))) {
  } else {
    ddl_clog_type_ = static_cast<ObDDLClogType>(log_type);
    pos = tmp_pos;
  }

  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObDDLClogHeader)
{
  int64_t size = 0;
  size += serialization::encoded_length_i64(static_cast<int64_t>(ddl_clog_type_));
  return size;
}

ObDDLRedoLog::ObDDLRedoLog()
  : redo_info_()
{
}

int ObDDLRedoLog::init(const storage::ObDDLMacroBlockRedoInfo &redo_info)
{
  int ret = OB_SUCCESS;
  if (!redo_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(redo_info));
  } else {
    redo_info_ = redo_info;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObDDLRedoLog, redo_info_);

ObTabletSchemaVersionChangeLog::ObTabletSchemaVersionChangeLog()
  : tablet_id_(), schema_version_(-1)
{
}

int ObTabletSchemaVersionChangeLog::init(const ObTabletID &tablet_id, const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  if (!tablet_id.is_valid() || schema_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(schema_version));
  } else {
    tablet_id_ = tablet_id;
    schema_version_ = schema_version;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTabletSchemaVersionChangeLog, tablet_id_, schema_version_);

OB_SERIALIZE_MEMBER(ObTableForkFreezeLog, tablet_ids_);
OB_SERIALIZE_MEMBER(ObTableForkStartLog, fork_info_);
OB_SERIALIZE_MEMBER(ObTableForkFinishLog, fork_info_);

} // namespace storage
} // namespace oceanbase
