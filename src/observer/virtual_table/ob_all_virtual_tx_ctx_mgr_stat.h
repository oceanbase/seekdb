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

#ifndef OB_ALL_VIRTUAL_TX_CTX_MGR_STAT
#define OB_ALL_VIRTUAL_TX_CTX_MGR_STAT

#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"
#include "lib/container/ob_se_array.h"
#include "common/ob_simple_iterator.h"
#include "storage/tx/ob_tx_ls_log_writer.h"
#include "storage/tx/ob_trans_ctx.h"
#include "storage/tx/ob_trans_ctx_mgr_v4.h"

namespace oceanbase
{
namespace transaction
{
class ObTransService;
class ObLSTxCtxMgrStat;
}
namespace observer
{
class ObGVTxCtxMgrStat: public common::ObVirtualTableScannerIterator
{
public:
  explicit ObGVTxCtxMgrStat(transaction::ObTransService *trans_service)
      : trans_service_(trans_service) { reset(); }
  virtual ~ObGVTxCtxMgrStat() { destroy(); }
public:
  int inner_get_next_row(common::ObNewRow *&row);
  void reset();
  void destroy();
private:
  int prepare_start_to_read_();
private:
  char ip_buffer_[common::OB_IP_STR_BUFF];
  char memstore_version_buffer_[common::MAX_VERSION_LENGTH];
private:
  transaction::ObTransService *trans_service_;
  transaction::ObTxCtxMgrStatIterator tx_ctx_mgr_stat_iter_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObGVTxCtxMgrStat);
};

}
}
#endif /* OB_ALL_VIRTUAL_TX_CTX_MGR_STAT */
