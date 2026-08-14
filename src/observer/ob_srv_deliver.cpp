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

#include "observer/ob_srv_deliver.h"
#include "rpc/ob_sql_request_operator.h"

#include "util/easy_mod_stat.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include "observer/omt/ob_server_runtime.h"
#include "lib/stat/ob_diagnostic_info_guard.h" // EVENT_INC, EVENT_ADD
#include "lib/statistic_event/ob_stat_event.h"

using namespace oceanbase::common;

using namespace oceanbase::rpc;
using namespace oceanbase::rpc::frame;
using namespace oceanbase::observer;
using namespace oceanbase::omt;
using namespace oceanbase::memtable;

namespace oceanbase
{
int dispatch_req(ObRequest &req)
{
  int ret = OB_SUCCESS;
  
  SERVER_MODULE_SCOPE {
    ObServerRuntime *runtime = static_cast<ObServerRuntime *>(share::server_runtime());
    if (OB_ISNULL(runtime)) {
      ret = OB_SERVER_RUNTIME_NOT_READY;
      LOG_WARN("server runtime is NULL", K(ret));
    } else if (runtime->has_stopped()) {
      ret = OB_SERVER_RUNTIME_NOT_READY;
      LOG_WARN("server runtime is stopped", K(ret));
    } else if (OB_FAIL(runtime->recv_request(req))) {
      LOG_WARN("dispatch request fail", K(ret), K(req));
      if (OB_SIZE_OVERFLOW == ret) {
        LOG_DBA_ERROR_V2(OB_SERVER_REQUEST_QUEUE_FULL, ret,
          "deliver mysql request to runtime: ", runtime->id(), " queue failed, the queue is full. ",
          "[suggestion] check T", runtime->id(), "_L0_G0 thread stack to see which "
          "procedure is taking too long or is blocked.");
      }
    }
  } else {
    LOG_WARN("cannot enter server runtime", K(ret));
  }

  return ret;
}

} // namespace oceanbase

ObSrvDeliver::ObSrvDeliver(ObiReqQHandler &qhandler)
    : ObReqQDeliver(qhandler)
{}

int ObSrvDeliver::deliver_mysql_request(ObRequest &req)
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = NULL;
  ObSMConnection *conn = SQL_REQ_OP.get_sql_session(&req);
  if (NULL != conn) {
    runtime = conn->runtime_;
    req.set_group_id(conn->group_id_);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session from request is NULL", K(req), K(ret));
  }

  req.set_trace_point(ObRequest::OB_REQUEST_MYSQL_DELIVER);

  if (OB_SUCC(ret)) {
    const bool need_update_stat = (ObRequest::OB_MYSQL == req.get_type()) &&
                                  !req.is_retry_on_lock();
    // auth request
    if (NULL == runtime) {
      const obmysql::ObMySQLRawPacket &pkt
          = reinterpret_cast<const obmysql::ObMySQLRawPacket &>(req.get_packet());
      if (need_update_stat) {
        EVENT_INC(MYSQL_PACKET_IN);
        EVENT_ADD(MYSQL_PACKET_IN_BYTES, pkt.get_wire_bytes());
        conn->connect_in_bytes_ = static_cast<int64_t>(pkt.get_wire_bytes());
      }

      if (OB_FAIL(dispatch_req(req))) {
      }
    } else {
      const obmysql::ObMySQLRawPacket &pkt
          = reinterpret_cast<const obmysql::ObMySQLRawPacket &>(req.get_packet());

      if (need_update_stat) {
        EVENT_INC(MYSQL_PACKET_IN);
        EVENT_ADD(MYSQL_PACKET_IN_BYTES, pkt.get_wire_bytes());
      }
      // Runtime validity was checked when the connection was authenticated.

      if (runtime->has_stopped()) {
        ret = OB_SERVER_RUNTIME_NOT_READY;
        LOG_WARN("server runtime is stopped", K(ret), K(runtime->id()));
      } else if (OB_FAIL(runtime->recv_request(req))) {
        EVENT_INC(MYSQL_DELIVER_FAIL);
        LOG_ERROR("deliver request fail", K(req), K(ret), K(*runtime));
        if (OB_SIZE_OVERFLOW == ret) {
          LOG_DBA_ERROR_V2(OB_SERVER_REQUEST_QUEUE_FULL, ret,
            "deliver mysql request to runtime: ", runtime->id(), " queue failed, the queue is full. ",
            "[suggestion] check T", runtime->id(), "_L0_G0 thread stack to see which "
            "procedure is taking too long or is blocked.");
        }
      }
    }
  }
  return ret;
}

int ObSrvDeliver::repost(void* p)
{
  rpc::ObRequest* req = CONTAINER_OF((const ObLockWaitNode *)p, rpc::ObRequest, lock_wait_node_);
  return deliver(*req);
}

int ObSrvDeliver::deliver(rpc::ObRequest &req)
{
  int ret = OB_SUCCESS;
  RequestLockWaitStat::RequestStat req_stat = req.lock_wait_node_.request_stat_.state_;
  if (OB_UNLIKELY(req_stat == RequestLockWaitStat::RequestStat::INQUEUE)) {
#ifdef OB_BUILD_PACKAGE // serious env, just WARN
    LOG_WARN("deliver request in unexpected state", KP(&req), K(req_stat));
#else
    LOG_WARN("deliver request in unexpected state", KP(&req), K(req_stat), K(lbt()));
#endif
  }
  if (ObRequest::OB_MYSQL == req.get_type()) {
    if (OB_FAIL(deliver_mysql_request(req))) {
      LOG_WARN("deliver mysql request fail", K(req), K(ret));
      //If it is a lock conflict repost request, if the deliver fails, the link is broken,
      //Normal requests will break the link at the upper level
      if (req.is_retry_on_lock()) {
        on_translate_fail(&req, ret);
      }
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ignore unknown request", K(req), K(ret));
  }

  return ret;
}
