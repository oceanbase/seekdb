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

#include <gmock/gmock.h>
#include "observer/report/ob_i_meta_report.h"

namespace oceanbase
{
namespace storage
{

class MockObMetaReport : public observer::ObIMetaReport
{
public:
  MOCK_METHOD2(submit_ls_update_task, int(const uint64_t tenant_id,
                                                  const share::ObLSID &ls_id));
};

}
}
