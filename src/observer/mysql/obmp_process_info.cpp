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

#define USING_LOG_PREFIX SERVER

#include "observer/mysql/obmp_process_info.h"

namespace oceanbase
{
using namespace common;
using namespace obmysql;
using namespace rpc;

namespace observer
{
ObMPProcessInfo::ObMPProcessInfo(const ObGlobalContext &gctx)
    : ObMPQuery(gctx)
{
}

ObMPProcessInfo::~ObMPProcessInfo()
{
}

int ObMPProcessInfo::deserialize()
{
  int ret = OB_SUCCESS;
  const char *process_info_sql = "/*+TRIGGERED BY COM_PROCESS_INFO*/ SHOW PROCESSLIST";
  if ( (OB_ISNULL(req_)) || (req_->get_type() != ObRequest::OB_MYSQL)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid request", K(ret), K(req_));
  } else {
    assign_sql(process_info_sql, STRLEN(process_info_sql));
  }
  return ret;
}


} // namespace observer
} // namespace oceanbase
