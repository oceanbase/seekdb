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
 
#ifndef OCEANBASE_MITTEST_MOCK_OB_META_REPORTER_
#define OCEANBASE_MITTEST_MOCK_OB_META_REPORTER_


#include "observer/report/ob_i_meta_report.h"


namespace oceanbase
{
using namespace observer;
namespace unittest
{
class MockMetaReporter : public ObIMetaReport
{
public:
  MockMetaReporter() { }
  ~MockMetaReporter() { }
  int submit_ls_update_task(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id)
  {
    UNUSEDx(tenant_id, ls_id);
    return OB_SUCCESS;
  }
  int submit_tablet_checksums_task(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id,
      const common::ObTabletID &tablet_id)
  {
    UNUSEDx(tenant_id, ls_id, tablet_id);
    return OB_SUCCESS;
  }
};
}// storage
}// oceanbase
#endif
