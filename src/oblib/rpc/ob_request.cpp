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

#ifdef _WIN32
#define USING_LOG_PREFIX RPC
#endif
#include "rpc/ob_request.h"
#include "rpc/ob_sql_request_operator.h"
using namespace oceanbase::common;

namespace oceanbase
{
namespace rpc
{
common::ObAddr g_server_self_addr;

void on_translate_fail(ObRequest* req, int)
{
  if (ObRequest::OB_MYSQL == req->get_type()) {
    const uint64_t generation = req->get_nio_request_generation();
    (void)SQL_REQ_OP.disconnect_sql_conn(req, generation);
    (void)SQL_REQ_OP.finish_sql_request(req, generation);
  }
}

int ObRequest::set_trace_point(int trace_point)
{
  handling_state_ = trace_point;
  return OB_SUCCESS;
}

int ObRequest::set_traverse_index(int64_t index) {
  int ret = OB_SUCCESS;
  traverse_index_ = index;
  return ret;
}

} //end of namespace rpc
} //end of namespace oceanbase
