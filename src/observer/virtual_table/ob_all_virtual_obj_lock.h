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

#ifndef OB_ALL_VIRTUAL_OB_OBJ_LOCK_H_
#define OB_ALL_VIRTUAL_OB_OBJ_LOCK_H_

#include <vector>

#include "storage/tablelock/ob_named_lock_manager.h"
#include "storage/tablelock/ob_obj_lock.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObLS;
}
namespace observer
{

class ObAllVirtualObjLock : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualObjLock();
  virtual ~ObAllVirtualObjLock();
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();
private:
  int get_next_tx_ctx(transaction::ObTxCtx *&tx_ctx);
  int get_next_lock_id(ObLockID &lock_id);
  int get_next_lock_op(transaction::tablelock::ObTableLockOp &lock_op,
                       transaction::tablelock::ObTableLockPriority &priority);
  int get_next_lock_op_iter();
  int get_next_lock_op_iter_from_tx_ctx();
  int get_next_lock_op_iter_from_lock_memtable();
  int prepare_start_to_read();
  int prepare_named_lock_snapshot();
  int get_next_named_lock_op(transaction::tablelock::ObTableLockOp &lock_op,
                             transaction::tablelock::ObTableLockPriority &priority,
                             const transaction::tablelock::NamedLockManager::LockSnapshot *&snapshot);

private:
  enum
  {
    LOCK_ID = OB_APP_MIN_COLUMN_ID,
    LOCK_MODE,
    OWNER_ID,
    CREATE_TRANS_ID,
    OP_TYPE,
    OP_STATUS,
    TRANS_VERSION,
    CREATE_TIMESTAMP,
    CREATE_SCHEMA_VERSION,
    EXTRA_INFO,
    TIME_AFTER_CREATE,
    OBJ_TYPE,
    OBJ_ID,
    OWNER_TYPE,
    PRIORITY,
    WAIT_SEQ,
    OBJ_NAME
  };
private:
  storage::ObLS *ls_;
  transaction::ObTxCtx *tx_ctx_;
  // the tx_ctx of a ls
  transaction::ObLSTxCtxIterator tx_ctx_iter_;
  // the lock id of a ls
  ObLockIDIterator obj_lock_iter_;
  // the lock op of a obj lock
  ObLockOpIterator lock_op_iter_;
  // the priority op
  ObPrioOpIterator prio_op_iter_;
  // whether iterate tx or not now.
  bool is_iter_tx_;
  bool is_iter_named_lock_;
  std::vector<transaction::tablelock::NamedLockManager::LockSnapshot> named_lock_snapshot_;
  int64_t named_lock_snapshot_idx_;
  bool is_iter_priority_list_;
  char lock_id_buf_[common::MAX_LOCK_ID_BUF_LENGTH];
  char lock_mode_buf_[common::MAX_LOCK_MODE_BUF_LENGTH];
  char lock_obj_type_buf_[common::MAX_LOCK_OBJ_TYPE_BUF_LENGTH];
  char lock_op_type_buf_[common::MAX_LOCK_OP_TYPE_BUF_LENGTH];
  char lock_op_status_buf_[common::MAX_LOCK_OP_STATUS_BUF_LENGTH];
  char lock_op_extra_info_[common::MAX_LOCK_OP_EXTRA_INFO_LENGTH];
  char lock_op_priority_buf_[common::MAX_LOCK_OP_PRIORITY_BUF_LENGTH];
private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualObjLock);
};

} // observer
} // oceanbase
#endif
