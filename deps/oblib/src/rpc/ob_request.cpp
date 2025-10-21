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

#include "rpc/ob_request.h"
using namespace oceanbase::common;

namespace oceanbase
{
namespace rpc
{
void __attribute__((weak)) response_rpc_error_packet(ObRequest* req, int ret)
{
  UNUSED(ret);
  RPC_REQ_OP.response_result(req, NULL);
}

void on_translate_fail(ObRequest* req, int ret)
{
  ObRequest::Type req_type = req->get_type();
  if (ObRequest::OB_RPC == req_type) {
    response_rpc_error_packet(req, ret);
  } else if (ObRequest::OB_MYSQL == req_type) {
    SQL_REQ_OP.disconnect_sql_conn(req);
    SQL_REQ_OP.finish_sql_request(req);
    req->reset_diagnostic_info();
  }
}

int ObRequest::set_trace_point(int trace_point)
{
  if (ez_req_ != NULL) {
    if (trace_point != 0) {
      ez_req_->trace_point = trace_point;
    } else {
      snprintf(ez_req_->trace_bt, EASY_REQ_TRACE_BT_SIZE, "%s", lbt());
    }
  } else {
    handling_state_ = trace_point;
  }
  return OB_SUCCESS;
}

int ObRequest::set_traverse_index(int64_t index) {
  int ret = OB_SUCCESS;
  traverse_index_ = index;
  return ret;
}

} //end of namespace rpc
} //end of namespace oceanbase
