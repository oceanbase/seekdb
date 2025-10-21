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

#ifndef OB_ALL_VIRTUAL_DIAG_INDEX_SCAN_H_
#define OB_ALL_VIRTUAL_DIAG_INDEX_SCAN_H_

#include "lib/stat/ob_session_stat.h"
#include "lib/container/ob_se_array.h"
#include "common/ob_range.h"
#include "sql/session/ob_sql_session_mgr.h"


namespace oceanbase
{
namespace observer
{

class ObAllVirtualDiagIndexScan
{
typedef  common::ObSEArray<int64_t, 16> ObIndexArray;
public:
  ObAllVirtualDiagIndexScan() : index_ids_() {}
  virtual ~ObAllVirtualDiagIndexScan() { index_ids_.reset(); }
  int set_index_ids(const common::ObIArray<common::ObNewRange> &ranges);
  // get server sid if sid is client sid
  int get_server_sid_by_client_sid(sql::ObSQLSessionMgr* mgr, uint64_t &sid);
  inline ObIndexArray &get_index_ids() { return index_ids_; }
private:
  ObIndexArray index_ids_;
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualDiagIndexScan);
};

} /* namespace observer */
} /* namespace oceanbase */

#endif /* OB_ALL_VIRTUAL_DIAG_INDEX_SCAN_H_ */
