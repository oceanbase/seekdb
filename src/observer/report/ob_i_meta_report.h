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

#ifndef OCEANBASE_OBSERVER_OB_I_META_REPORT
#define OCEANBASE_OBSERVER_OB_I_META_REPORT

namespace oceanbase
{
namespace common
{
class ObTabletID;
}
namespace share
{
class ObLSID;
}
namespace observer
{
class ObIMetaReport
{
public:
  ObIMetaReport() {}
  virtual ~ObIMetaReport() {}
  virtual int submit_ls_update_task(
      const uint64_t tenant_id,
      const share::ObLSID &ls_id) = 0;
};

} // end namespace observer
} // end namespace oceanbase
#endif
