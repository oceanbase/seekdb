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

#ifndef OCEANBASE_SHARE_OB_DISK_USAGE_TABLE_OPERATOR_
#define OCEANBASE_SHARE_OB_DISK_USAGE_TABLE_OPERATOR_

#include <stdint.h>
#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}

namespace storage
{
enum class ObDiskReportFileType : uint8_t
{
  TENANT_DATA = 0,
  TENANT_META_DATA = 1,
  TENANT_INDEX_DATA = 2,
  TENANT_TMP_DATA = 3,
  TENANT_SLOG_DATA = 4,
  TENANT_CLOG_DATA = 5,
  TENANT_MAJOR_SSTABLE_DATA = 6, // shared_data in shared_storage_mode
  TENANT_MAJOR_LOCAL_DATA = 7,
  TENANT_BACKUP_DATA = 8,
  TYPE_MAX
};
struct ObTenantDiskUsage;
}

namespace share
{

class ObDiskUsageTableOperator
{
public:
  ObDiskUsageTableOperator();
  ~ObDiskUsageTableOperator();
public:
  int init(common::ObMySQLProxy &proxy);
  int update_tenant_space_usage(const uint64_t tenant_id,
                                const char *svr_ip,
                                const int32_t svr_port,
                                const int64_t seq_num,
                                const enum storage::ObDiskReportFileType file_type,
                                const uint64_t data_size,
                                const uint64_t used_size);
  int delete_tenant_all(const uint64_t tenant_id,
                        const char *svr_ip,
                        const int32_t svr_port,
                        const int64_t seq_num);

  int get_all_tenant_ids(const char *svr_ip,
                         const int32_t svr_port,
                         const int64_t seq_num,
                         common::ObIArray<uint64_t> &tenant_ids);
private:
  bool is_inited_;
  common::ObMySQLProxy *proxy_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObDiskUsageTableOperator);
};
} // namespace share
} // namespace oceanbase



# endif // OCEANBASE_SHARE_OB_DISK_USAGE_TABLE_OPERATOR_
