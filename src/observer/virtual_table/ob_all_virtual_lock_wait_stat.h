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

#ifndef OB_ALL_VIRTUAL_LOCK_WAIT_STAT_H_
#define OB_ALL_VIRTUAL_LOCK_WAIT_STAT_H_

#include "share/ob_virtual_table_scanner_iterator.h"
#include "observer/omt/ob_multi_tenant_operator.h"
#include "rpc/ob_request.h"
#include "observer/omt/ob_multi_tenant.h"

namespace oceanbase
{
namespace observer
{
class ObAllVirtualLockWaitStat : public common::ObVirtualTableScannerIterator,
                                 public omt::ObMultiTenantOperator
{
public:
  ObAllVirtualLockWaitStat() : node_iter_(nullptr) {}
  virtual ~ObAllVirtualLockWaitStat() { reset(); }

public:
  int inner_get_next_row(common::ObNewRow *&row) override;
  void reset() override;
private:
  bool is_need_process(uint64_t tenant_id) override;
  int process_curr_tenant(common::ObNewRow *&row) override;
  void release_last_tenant() override;

  int get_lock_type(int64_t hash, int &type);
  int get_rowkey_holder(int64_t hash, transaction::ObTransID &holder);
  int make_this_ready_to_read();
private:
  enum {
    SVR_IP = common::OB_APP_MIN_COLUMN_ID,
    SVR_PORT,
    TENANT_ID,
    TABLET_ID,
    ROWKEY,
    ADDR,
    NEED_WAIT,
    RECV_TS,
    LOCK_TS,
    ABS_TIMEOUT,
    TRY_LOCK_TIMES,
    TIME_AFTER_RECV,
    SESSION_ID,
    BLOCK_SESSION_ID,
    TYPE,
    LMODE,
    LAST_COMPACT_CNT,
    TOTAL_UPDATE_CNT,
    TRANS_ID,
    HOLDER_TRANS_ID,
    HOLDER_SESSION_ID,
    LS_ID,
    ASSOC_SESS_ID,
    WAIT_TIMEOUT,
    TX_ACTIVE_TS,
    NODE_ID,
    NODE_TYPE,
    REMTOE_ADDR,
    IS_PLACEHOLDER,
  };
  rpc::ObLockWaitNode *node_iter_;
  rpc::ObLockWaitNode cur_node_;
  char rowkey_[common::MAX_LOCK_ROWKEY_BUF_LENGTH];
  char lock_mode_[common::MAX_LOCK_MODE_BUF_LENGTH];
  char remote_addr_[common::MAX_LOCK_REMOTE_ADDR_BUF_LENGTH];

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualLockWaitStat);
};
}
}
#endif /* OB_ALL_VIRTUAL_LOCK_WAIT_STAT_H */
