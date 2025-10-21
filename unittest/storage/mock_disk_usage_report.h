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

#ifndef OCEANBASE_STORAGE_MOCK_DISK_USAGE_REPORT_H
#define OCEANBASE_STORAGE_MOCK_DISK_USAGE_REPORT_H

#include "observer/report/ob_i_disk_report.h"

namespace oceanbase
{

namespace storage
{

class MockDiskUsageReport : public observer::ObIDiskReport
{
public:
  MockDiskUsageReport() {}
  virtual ~MockDiskUsageReport() {}
  virtual int delete_tenant_usage_stat(const uint64_t tenant_id)
  {
    UNUSED(tenant_id);
    return OB_SUCCESS;
  }
};

}
}

#endif
