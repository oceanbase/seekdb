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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_READ_TIMESTAMP_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_READ_TIMESTAMP_SERVICE_H_

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace data_plane
{

class ObIReadTimestampService
{
public:
  virtual ~ObIReadTimestampService() {}
  virtual int latest_read_scn(share::SCN &scn) = 0;
  virtual bool is_external_consistent() = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_READ_TIMESTAMP_SERVICE_H_
