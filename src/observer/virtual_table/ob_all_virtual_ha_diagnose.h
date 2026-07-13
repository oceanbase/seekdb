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

#ifndef OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_HA_DIAGNOSE_H_
#define OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_HA_DIAGNOSE_H_

#include "common/row/ob_row.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "sql/ob_scanner.h"
#include "storage/ls/ob_ls.h"

namespace oceanbase
{
namespace observer
{
enum IOStatColumn
{
  PALF_STATE = common::OB_APP_MIN_COLUMN_ID,
  MAX_APPLIED_SCN,
  MAX_REPLAYED_LSN,
  MAX_REPLAYED_SCN,
  REPLAY_DIAGNOSE_INFO,
  CHECKPOINT_SCN,
  MIN_REC_SCN,
  MIN_REC_SCN_LOG_TYPE,
  READ_TX,
};

class ObAllVirtualHADiagnose : public common::ObVirtualTableScannerIterator
{
public:
  virtual int inner_get_next_row(common::ObNewRow *&row);
private:
  int insert_stat_(storage::DiagnoseInfo &diagnose_info);
private:
  static const int64_t VARCHAR_32 = 32;
  char min_rec_log_scn_log_type_str_[VARCHAR_32] = {'\0'};
};
} // namespace observer
} // namespace oceanbase
#endif /* OCEANBASE_OBSERVER_OB_ALL_VIRTUAL_HA_DIAGNOSE_H_ */
