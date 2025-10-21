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

#ifndef OB_ALL_VIRTUAL_WEAK_READ_STAT_H_
#define OB_ALL_VIRTUAL_WEAK_READ_STAT_H_


#include "share/ob_virtual_table_scanner_iterator.h"
#include "share/ob_scanner.h"
#include "common/row/ob_row.h"
#include "share/ob_define.h"
#include "storage/tx/wrs/ob_tenant_weak_read_stat.h"
#include "storage/tx/wrs/ob_tenant_weak_read_service.h"

namespace oceanbase
{
namespace transaction{
  class ObTenantWeakReadStat;
}
namespace observer
{
class ObAllVirtualWeakReadStat : public common::ObVirtualTableScannerIterator
{
enum columns{TENANT_ID, SERVER_IP, SERVER_PORT, SERVER_VERSION, SERVER_VERSION_DELTA,
  LOCAL_CLUSTER_VERSION, LOCAL_CLUSTER_VERSION_DELTA, TOTAL_PART_COUNT, VALID_INNER_PART_COUNT,
  VALID_USER_PART_COUNT, CLUSTER_MASTER_IP, CLUSTER_MASTER_PORT, CLUSTER_HEART_BEAT_TS,
  CLUSTER_HEART_BEAT_COUNT, CLUSTER_HEART_BEAT_SUCC_TS, CLUSTER_HEART_BEAT_SUCC_COUNT,
  SELF_CHECK_TS, LOCAL_CURRENT_TS, IN_CLUSTER_SERVICE, IS_CLUSTER_MASTER, CLUSTER_MASTER_EPOCH,
  CLUSTER_SERVERS_COUNT, CLUSTER_SKIPPED_SERVERS_COUNT, CLUSTER_VERSION_GEN_TS, CLUSTER_VERSION,
  CLUSTER_VERSION_DELTA, MIN_CLUSTER_VERSION, MAX_CLUSTER_VERSION};
public:
ObAllVirtualWeakReadStat();
~ObAllVirtualWeakReadStat();
void reset();
int inner_get_next_row(common::ObNewRow *&row);
protected:
bool start_to_read_;
};

} //observer
} //oceanbase
#endif /* OB_ALL_VIRTUAL_WEAK_READ_STAT_H_ */
