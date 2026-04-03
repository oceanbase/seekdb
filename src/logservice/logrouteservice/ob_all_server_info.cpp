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

#define USING_LOG_PREFIX OBLOG
#include "ob_all_server_info.h"

namespace oceanbase
{
namespace logservice
{
int AllServerRecord::init(const uint64_t svr_id,
    const common::ObAddr &server,
    StatusType &status,
    ObString &zone)
{
  int ret = OB_SUCCESS;
  svr_id_ = svr_id;
  server_ = server;
  status_ = status;

  if (OB_FAIL(zone_.assign(zone))) {
    LOG_ERROR("zone assign fail", KR(ret), K(zone));
  }

  return ret;
}

void ObAllServerInfo::reset()
{
  cluster_id_ = 0;
  all_srv_array_.reset();
}

int ObAllServerInfo::init(const int64_t cluster_id)
{
  int ret = OB_SUCCESS;

  cluster_id_ = cluster_id;
  all_srv_array_.reset();

  return ret;
}

int ObAllServerInfo::add(AllServerRecord &record)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(all_srv_array_.push_back(record))) {
    LOG_ERROR("all_srv_array_ push_back failed", KR(ret), K(record));
  }

  return ret;
}

} // namespace logservice
} // namespace oceanbase
