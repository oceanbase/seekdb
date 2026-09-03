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

#ifndef OCEANBASE_QUERY_CHANGE_STREAM_OB_CHANGE_STREAM_SERVICE_H_
#define OCEANBASE_QUERY_CHANGE_STREAM_OB_CHANGE_STREAM_SERVICE_H_

#include <cstdint>

namespace oceanbase
{
namespace palf
{
struct LSN;
}
namespace common
{
class ObMySQLProxy;
}
namespace query
{

class ObIChangeStreamService
{
public:
  virtual ~ObIChangeStreamService() = default;
  virtual int wait_until_refreshed(
      common::ObMySQLProxy &mysql_proxy,
      int64_t timeout_us) = 0;
  virtual int get_min_dep_lsn(palf::LSN &min_dep_lsn) = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_CHANGE_STREAM_OB_CHANGE_STREAM_SERVICE_H_
