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

#pragma once

#include "storage/ddl/ob_ddl_redo_log_writer.h"
#include "storage/ddl/ob_ddl_inc_clog.h"
#include "storage/ddl/ob_ddl_inc_clog_callback.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/tx/ob_trans_define_v4.h"

namespace oceanbase
{
namespace storage
{

class ObDDLIncLogHandle 
{
public:
  static const int64_t DDL_INC_LOG_TIMEOUT = 60 * 1000 * 1000; // 1min
  static const int64_t CHECK_DDL_INC_LOG_FINISH_INTERVAL = 1000; // 1ms
  ObDDLIncLogHandle();
  ~ObDDLIncLogHandle();
  int wait(const int64_t timeout = DDL_INC_LOG_TIMEOUT);
  void reset();
  bool is_valid() const { return nullptr != cb_  && scn_.is_valid_and_not_min(); }
public:
  ObDDLIncClogCb *cb_;
  share::SCN scn_;
};

class ObDDLIncRedoLogWriter final
{
public:
  static const int64_t DEFAULT_RETRY_TIMEOUT_US = 60L * 1000L * 1000L; // 1min
  ObDDLIncRedoLogWriter();
  ~ObDDLIncRedoLogWriter();
  int init(const ObTabletID &tablet_id);
  void reset();
  static bool need_retry(int ret_code);
  int write_inc_start_log_with_retry(
      const ObTabletID &lob_meta_tablet_id, 
      transaction::ObTxDesc *tx_desc,
      share::SCN &start_scn);
  int write_inc_redo_log_with_retry(
      const storage::ObDDLMacroBlockRedoInfo &redo_info,
      const blocksstable::MacroBlockId &macro_block_id,
      const int64_t task_id,
      transaction::ObTxDesc *tx_desc);
  int wait_inc_redo_log_finish();
  int write_inc_commit_log_with_retry(
      const ObTabletID &lob_meta_tablet_id,
      transaction::ObTxDesc *tx_desc);
private:
  int write_inc_start_log(
      const ObTabletID &lob_meta_tablet_id, 
      transaction::ObTxDesc *tx_desc,
      share::SCN &start_scn);
  int write_inc_redo_log(
      const storage::ObDDLMacroBlockRedoInfo &redo_info,
      const blocksstable::MacroBlockId &macro_block_id,
      const int64_t task_id,
      transaction::ObTxDesc *tx_desc);
  int write_inc_commit_log(
      const ObTabletID &lob_meta_tablet_id,
      transaction::ObTxDesc *tx_desc);
  int get_write_store_ctx_guard(
      transaction::ObTxDesc *tx_desc,
      ObStoreCtxGuard &ctx_guard);
  int local_write_inc_start_log(
      ObDDLIncStartLog &log,
      transaction::ObTxDesc *tx_desc,
      share::SCN &start_scn);
  int local_write_inc_redo_log(
      const storage::ObDDLMacroBlockRedoInfo &redo_info,
      const blocksstable::MacroBlockId &macro_block_id,
      const int64_t task_id,
      transaction::ObTxDesc *tx_desc);
  int local_write_inc_commit_log(
      ObDDLIncCommitLog &log,
      transaction::ObTxDesc *tx_desc);
private:
  bool is_inited_;
  ObTabletID tablet_id_;
  ObDDLIncLogHandle ddl_inc_log_handle_;
  char *buffer_;
};

class ObDDLIncRedoLogWriterCallback : public blocksstable::ObIMacroBlockFlushCallback
{
public:
  ObDDLIncRedoLogWriterCallback();
  virtual ~ObDDLIncRedoLogWriterCallback();
  int init(ObDDLRedoLogWriterCallbackInitParam &param);
  int write(
      const blocksstable::ObStorageObjectHandle &macro_handle,
      const blocksstable::ObLogicMacroBlockId &logic_id,
      char *buf,
      const int64_t buf_len,
      const int64_t row_count);
  int wait();
private:
  bool is_inited_;
  storage::ObDDLMacroBlockRedoInfo redo_info_;
  blocksstable::MacroBlockId macro_block_id_;
  ObDDLIncRedoLogWriter ddl_inc_writer_;
  storage::ObDDLMacroBlockType block_type_;
  ObITable::TableKey table_key_;
  int64_t task_id_;
  share::SCN start_scn_;
  uint64_t data_format_version_;
  storage::ObDirectLoadType direct_load_type_;
  transaction::ObTxDesc *tx_desc_;
  transaction::ObTransID trans_id_;
  int64_t parallel_cnt_;
  transaction::ObTxSEQ seq_no_; // for incremental direct load
};

}  // end namespace storage
}  // end namespace oceanbase
