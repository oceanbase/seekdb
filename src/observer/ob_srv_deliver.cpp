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
#include "share/rc/ob_module_provider.h"

#include "util/easy_mod_stat.h"
#include "lib/vtoa/ob_vtoa_util.h"
#include "rpc/frame/ob_req_session_handler.h"
#include "rpc/obmysql/packet/ompk_handshake_response.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include "rpc/frame/ob_net_easy.h"
#include "observer/omt/ob_tenant.h"
#include "lib/stat/ob_diagnostic_info_guard.h" // EVENT_INC, EVENT_ADD
#include "lib/statistic_event/ob_stat_event.h"

using namespace oceanbase::common;

using namespace oceanbase::rpc;
using namespace oceanbase::rpc::frame;
using namespace oceanbase::obcall;
using namespace oceanbase::observer;
using namespace oceanbase::omt;
using namespace oceanbase::memtable;

namespace oceanbase
{
namespace observer
{
ObString extract_user_name(const ObString &in);
int extract_user_tenant(const ObString &in, ObString &user_name, ObString &tenant_name);
}  // namespace observer
int get_endpoint_tenant(char *endpoint_tenant_mapping_buf, const int64_t vid, const ObAddr &vaddr, ObString &tenant_name)
{
  int ret = OB_SUCCESS;
  const char *JSON_VID = "vid";
  const char *JSON_VIP = "vip";
  const char *JSON_VPORT = "vport";
  const char *JSON_CLUSTER_NAME = "cluster_name";
  const char *JSON_TENANT_NAME = "tenant_name";

  const int64_t endpoint_tenant_mapping_buf_len = STRLEN(endpoint_tenant_mapping_buf);
  ObArenaAllocator allocator;
  json::Value* data = NULL;
  json::Parser parser;

  if (0 == endpoint_tenant_mapping_buf_len) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("_endpoint_tenant_mapping is null", K(ret));
  } else if (OB_FAIL(parser.init(&allocator))) {
    LOG_WARN("parser init failed", K(ret));
  } else if (OB_FAIL(parser.parse(endpoint_tenant_mapping_buf, endpoint_tenant_mapping_buf_len, data))) {
    LOG_WARN("parse json failed", K(ret));
  } else if (NULL == data) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("no root value", K(ret));
  } else if (json::JT_ARRAY != data->get_type()) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("error json format", K(ret));
  } else {
    int64_t virtual_id = -1;
    char virtual_ip_buf[MAX_IP_ADDR_LENGTH] = "";
    int32_t virtual_port = -1;
    ObString clustername = ObString::make_empty_string();
    ObString tenantname = ObString::make_empty_string();
    bool is_found = false;
    DLIST_FOREACH_X(it, data->get_array(), OB_SUCC(ret) && !is_found) {
      if (json::JT_OBJECT != it->get_type()) {
        ret = OB_INVALID_CONFIG;
        LOG_WARN("not object in array", K(ret), "type", it->get_type());
        break;
      }
      DLIST_FOREACH(p, it->get_object()) {
        if (p->name_.case_compare(JSON_VID) == 0) {
          if (NULL == p->value_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL value pointer", K(ret));
          } else if (json::JT_NUMBER != p->value_->get_type()) {
            ret = OB_INVALID_CONFIG;
            LOG_WARN("unexpected vid type", K(ret), "type", p->value_->get_type());
          } else {
            virtual_id = p->value_->get_number();
          }
        } else if (p->name_.case_compare(JSON_VIP) == 0) {
          if (NULL == p->value_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL value pointer", K(ret));
          } else if (json::JT_STRING != p->value_->get_type()) {
            ret = OB_INVALID_CONFIG;
            LOG_WARN("unexpected vip type", K(ret), "type", p->value_->get_type());
          } else {
            ObString virtual_ip = p->value_->get_string();
            if (virtual_ip.ptr() != NULL) {
              int64_t data_len = MIN(virtual_ip.length(), sizeof(virtual_ip_buf) - 1);
              MEMCPY(virtual_ip_buf, virtual_ip.ptr(), data_len);
              virtual_ip_buf[data_len] = '\0';
            }
          }
        } else if (p->name_.case_compare(JSON_VPORT) == 0) {
          if (NULL == p->value_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL value pointer", K(ret));
          } else if (json::JT_NUMBER != p->value_->get_type()) {
            ret = OB_INVALID_CONFIG;
            LOG_WARN("unexpected vport type", K(ret), "type", p->value_->get_type());
          } else {
            virtual_port = p->value_->get_number();
          }
        } else if (p->name_.case_compare(JSON_CLUSTER_NAME) == 0) {
          if (NULL == p->value_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL value pointer", K(ret));
          } else if (json::JT_STRING != p->value_->get_type()) {
            ret = OB_INVALID_CONFIG;
            LOG_WARN("unexpected cluster name type", K(ret), "type", p->value_->get_type());
          } else {
            clustername = p->value_->get_string();
          }
        } else if (p->name_.case_compare(JSON_TENANT_NAME) == 0) {
          if (NULL == p->value_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("NULL value pointer", K(ret));
          } else if (json::JT_STRING != p->value_->get_type()) {
            ret = OB_INVALID_CONFIG;
            LOG_WARN("unexpected tenant name type", K(ret), "type", p->value_->get_type());
          } else {
            tenantname = p->value_->get_string();
          }
        }
      }

      if (OB_SUCC(ret)) {
        ObAddr virtual_addr(ObAddr::IPV4, virtual_ip_buf, virtual_port);
        if (vid == virtual_id && virtual_addr == vaddr) {
          if (GCONF.cluster.get_value_string() != clustername) {
            ret = OB_INVALID_CONFIG;
            LOG_WARN("cluster name not match", K(ret), K(GCONF.cluster.get_value_string()), K(clustername));
          } else if (tenantname.empty()) {
            ret = OB_INVALID_CONFIG;
            LOG_WARN("null tenantname is unexpected", K(ret), K(vid), K(vaddr), K(tenantname));
          } else {
            is_found = true;
            tenant_name = tenantname;
          }
        }
      }
    }
    if (OB_SUCC(ret) && !is_found) {
      ret = OB_INVALID_CONFIG;
      LOG_WARN("cannot get tenant name by vid and vaddr", K(ret), K(vid), K(vaddr));
    }
  }
  return ret;
}

int get_user_tenant(ObRequest &req, char *user_name_buf, char *tenant_name_buf)
{
  int ret = OB_SUCCESS;

  ObString user_name = ObString::make_empty_string();
  ObString tenant_name = ObString::make_empty_string();

  int fd = req.get_connfd();
  bool is_slb = false;
  int64_t vid = -1;
  ObAddr vaddr;
  char *endpoint_tenant_mapping_buf = nullptr;

  obmysql::OMPKHandshakeResponse hsr = static_cast<const obmysql::OMPKHandshakeResponse &>(req.get_packet());
  int tmp_ret = OB_SUCCESS;
  if (OB_TMP_FAIL(hsr.decode())) {
    // ignore error and handle in ObMPConnect
    LOG_WARN("decode hsr fail", K(tmp_ret));
  } else if (OB_TMP_FAIL(extract_user_tenant(hsr.get_username(), user_name, tenant_name))) {
    // ignore error and handle in ObMPConnect
    LOG_WARN("parse user@tenant fail", K(ret), "str", hsr.get_username());
  } else if (!tenant_name.empty()) {
    // use this tenant_name
  } else {
    if (OB_TMP_FAIL(lib::ObVTOAUtility::get_virtual_addr(fd, is_slb, vid, vaddr))) {
      LOG_WARN("failed to get virtual addr", K(tmp_ret), K(fd));
    }
    if (is_slb) {
      const int64_t endpoint_tenant_mapping_buf_len = STRLEN(GCONF._endpoint_tenant_mapping.str());
      endpoint_tenant_mapping_buf =
          static_cast<char *>(common::ob_malloc(sizeof(char) * (endpoint_tenant_mapping_buf_len + 1), "EndpointTenant"));
      if (OB_ISNULL(endpoint_tenant_mapping_buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        OB_LOG(ERROR, "failed to alloc memory", K(ret));
      } else {
        MEMCPY(endpoint_tenant_mapping_buf, GCONF._endpoint_tenant_mapping.str(), endpoint_tenant_mapping_buf_len);
        endpoint_tenant_mapping_buf[endpoint_tenant_mapping_buf_len] = '\0';
        if (OB_FAIL(get_endpoint_tenant(endpoint_tenant_mapping_buf, vid, vaddr, tenant_name))) {
          LOG_WARN("fail to get tenant name by vaddr", K(ret), K(vid), K(vaddr));
        } else {
          ObSMConnection *conn = static_cast<ObSMConnection *>(SQL_REQ_OP.get_sql_session(&req));
          conn->vid_ = vid;
          vaddr.ip_to_string(conn->vip_buf_, sizeof(conn->vip_buf_));
          conn->vport_ = vaddr.get_port();
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (user_name.length() > OB_MAX_USER_NAME_LENGTH) {
    ret = OB_WRONG_USER_NAME_LENGTH;
    LOG_WARN("username is too long", K(ret));
  } else if (tenant_name.length() > OB_MAX_TENANT_NAME_LENGTH) {
    ret = OB_INVALID_TENANT_NAME;
    LOG_WARN("tenant name is too long", K(ret));
  } else {
    MEMCPY(user_name_buf, user_name.ptr(), user_name.length());
    user_name_buf[user_name.length()] = '\0';
    MEMCPY(tenant_name_buf, tenant_name.ptr(), tenant_name.length());
    tenant_name_buf[tenant_name.length()] = '\0';
  }

  if (OB_NOT_NULL(endpoint_tenant_mapping_buf)) {
    ob_free(endpoint_tenant_mapping_buf);
  }
  return ret;
}

int dispatch_req(ObRequest &req)
{
  int ret = OB_SUCCESS;
  
  MOD_SCOPE {
    ObTenant *tenant = static_cast<ObTenant *>(MTL_CTX());
    if (OB_ISNULL(tenant)) {
      ret = OB_TENANT_NOT_IN_SERVER;
      LOG_WARN("tenant is NULL", K(ret));
    } else if (tenant->has_stopped()) {
      ret = OB_TENANT_NOT_IN_SERVER;
      LOG_WARN("tenant is stopped", K(ret));
    } else if (OB_FAIL(tenant->recv_request(req))) {
      LOG_WARN("dispatch request fail", K(ret), K(req));
      if (OB_SIZE_OVERFLOW == ret) {
        LOG_DBA_ERROR_V2(OB_TENANT_REQUEST_QUEUE_FULL, ret,
          "deliver mysql request to tenant: ", tenant->id(), " queue failed, the queue is full. ",
          "[suggestion] check T", tenant->id(), "_L0_G0 thread stack to see which "
          "procedure is taking too long or is blocked or check __all_virtual_thread view.");
      }
    }
  } else {
    LOG_WARN("cannot switch to tenant", K(ret));
  }

  return ret;
}

} // namespace oceanbase

ObSrvDeliver::ObSrvDeliver(ObiReqQHandler &qhandler,
                           ObReqSessionHandler &session_handler,
                           ObGlobalContext &gctx)
    : ObReqQDeliver(qhandler),
      is_inited_(false),
      stop_(true),
      host_(),
      ddl_queue_(NULL),
      ddl_parallel_queue_(NULL),
      diagnose_queue_(NULL),
      session_handler_(session_handler),
      gctx_(gctx)
{}

int ObSrvDeliver::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_queue_threads())) {
    SERVER_LOG(ERROR, "init queue threads fail", K(ret));
  } else {
    SERVER_LOG(INFO, "init ObSrvDeliver done");
    is_inited_ = true;
    stop_ = false;
  }
  return ret;
}

void ObSrvDeliver::stop()
{
  stop_ = true;
  if (NULL != diagnose_queue_) {
    // stop sql service first
    diagnose_queue_->stop();
    diagnose_queue_->wait();
  }
  if (NULL != ddl_queue_) {
    ddl_queue_->stop();
    ddl_queue_->wait();
  }
  if (NULL != ddl_parallel_queue_) {
    TG_STOP(lib::TGDefIDs::DDLPQueueTh);
    TG_WAIT(lib::TGDefIDs::DDLPQueueTh);
  }
}

int ObSrvDeliver::create_queue_thread(int tg_id, const char *thread_name, QueueThread *&qthread)
{
  int ret = OB_SUCCESS;
  qthread = OB_NEW(QueueThread, ObModIds::OB_RPC, thread_name);
  if (OB_ISNULL(qthread)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(qthread->init())) {
    LOG_WARN("init qthread failed", K(ret));
  } else {
    qthread->queue_.set_qhandler(&qhandler_);
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(qthread)) {
    qthread->tg_id_ = tg_id;
    ret = TG_SET_RUNNABLE_AND_START(tg_id, qthread->thread_);
  }
  return ret;
}

int ObSrvDeliver::init_queue_threads()
{
  int ret = OB_SUCCESS;

  // TODO: fufeng, make it configurable
  if (OB_FAIL(create_queue_thread(lib::TGDefIDs::DDLQueueTh, "DDLQueueTh", ddl_queue_))) {
  } else if (OB_FAIL(create_queue_thread(lib::TGDefIDs::DDLPQueueTh, PARALLEL_DDL_THREAD_NAME, ddl_parallel_queue_))) {
  } else if (OB_FAIL(create_queue_thread(lib::TGDefIDs::DiagnoseQueueTh,
                                         "DiagnoseQueueTh", diagnose_queue_))) {
  } else {
    LOG_INFO("queue thread create successfully", K_(host));
  }

  return ret;
}

int ObSrvDeliver::deliver_mysql_request(ObRequest &req)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = NULL;
  void *sess = SQL_REQ_OP.get_sql_session(&req);
  ObSMConnection *conn = NULL;
  if (NULL != sess) {
    conn = static_cast<ObSMConnection *>(sess);
    tenant = conn->tenant_;
    req.set_group_id(conn->group_id_);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session from request is NULL", K(req), K(ret));
  }

  req.set_trace_point(ObRequest::OB_EASY_REQUEST_MYSQL_DELIVER);

  if (OB_SUCC(ret)) {
    const bool need_update_stat = (ObRequest::OB_MYSQL == req.get_type()) &&
                                  !req.is_retry_on_lock();
    // auth request
    if (NULL == tenant) {
      const obmysql::ObMySQLRawPacket &pkt
          = reinterpret_cast<const obmysql::ObMySQLRawPacket &>(req.get_packet());
      if (need_update_stat) {
        EVENT_INC(common::ObStatEventIds::MYSQL_PACKET_IN);
        EVENT_ADD(common::ObStatEventIds::MYSQL_PACKET_IN_BYTES, pkt.get_clen() + OB_MYSQL_HEADER_LENGTH);
        conn->connect_in_bytes_ = pkt.get_clen() + OB_MYSQL_HEADER_LENGTH;
      }

      if (OB_UNLIKELY(NULL != diagnose_queue_ && SQL_REQ_OP.get_peer(&req).get_port() <= 0)) {
        LOG_INFO("receive login request from unix domain socket");
        if (!diagnose_queue_->queue_.push(&req, MAX_QUEUE_LEN)) {
          ret = OB_QUEUE_OVERFLOW;
          EVENT_INC(common::ObStatEventIds::MYSQL_DELIVER_FAIL);
          LOG_ERROR("deliver request fail", K(req));
        }
      } else {
        char user_name_buf[OB_MAX_USER_NAME_BUF_LENGTH] = "";
        char tenant_name_buf[OB_MAX_TENANT_NAME_LENGTH + 1] = "";
        if (OB_FAIL(get_user_tenant(req, user_name_buf, tenant_name_buf))) {
          LOG_WARN("fail to get username and tenant name", K(ret), K(req));
        } else if (0 != STRLEN(user_name_buf)) {
          if (0 == STRCMP(tenant_name_buf, OB_DIAG_TENANT_NAME)) {
            MEMCPY(tenant_name_buf, user_name_buf, STRLEN(user_name_buf));
            tenant_name_buf[STRLEN(user_name_buf)] = '\0';
            conn->group_id_ = share::OBCG_DIAG_TENANT;
          }
          MEMCPY(conn->user_name_buf_, user_name_buf, STRLEN(user_name_buf));
          conn->user_name_buf_[STRLEN(user_name_buf)] = '\0';
          MEMCPY(conn->tenant_name_buf_, tenant_name_buf, STRLEN(tenant_name_buf));
          conn->tenant_name_buf_[STRLEN(tenant_name_buf)] = '\0';
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(dispatch_req(req))) {
            LOG_ERROR("deliver request in dispatch_req fail", K(ret), K(req));
          }
        }
      }
    } else {
      const obmysql::ObMySQLRawPacket &pkt
          = reinterpret_cast<const obmysql::ObMySQLRawPacket &>(req.get_packet());

      if (need_update_stat) {
        EVENT_INC(common::ObStatEventIds::MYSQL_PACKET_IN);
        EVENT_ADD(common::ObStatEventIds::MYSQL_PACKET_IN_BYTES, pkt.get_clen() + OB_MYSQL_HEADER_LENGTH);
      }
      // The tenant check has been done in the recv_request method. For performance considerations, the check here is removed;

      if (OB_FAIL(ret)) {
            // do nothing
      } else if (tenant->has_stopped()) {
        ret = OB_TENANT_NOT_IN_SERVER;
        LOG_WARN("tenant is stopped", K(ret), K(tenant->id()));
      } else if (OB_FAIL(tenant->recv_request(req))) {
        EVENT_INC(common::ObStatEventIds::MYSQL_DELIVER_FAIL);
        LOG_ERROR("deliver request fail", K(req), K(ret), K(*tenant));
        if (OB_SIZE_OVERFLOW == ret) {
          LOG_DBA_ERROR_V2(OB_TENANT_REQUEST_QUEUE_FULL, ret,
            "deliver mysql request to tenant: ", tenant->id(), " queue failed, the queue is full. ",
            "[suggestion] check T", tenant->id(), "_L0_G0 thread stack to see which "
            "procedure is taking too long or is blocked or check __all_virtual_thread view.");
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
    LOG_WARN("deliver rpc request in unexpected state", KP(&req), K(req_stat));
#else
    LOG_WARN("deliver rpc request in unexpected state", KP(&req), K(req_stat), K(lbt()));
#endif
  }
  LOG_DEBUG("deliver ob_request:", K(req));
  if (ObRequest::OB_RPC == req.get_type()) {
    // obcall RPC transport removed (single-replica): no OB_RPC request path.
    ret = OB_NOT_SUPPORTED;
    if (REACH_TIME_INTERVAL(5 * 1000 * 1000)) {
      LOG_WARN("OB_RPC request after rpc removal, dropping", KP(&req), K(ret));
    }
    on_translate_fail(&req, ret);
  } else if (ObRequest::OB_MYSQL == req.get_type()) {
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
