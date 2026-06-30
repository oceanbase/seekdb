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
#include "ob_net_queue_traver.h"
#define USING_LOG_PREFIX RS
namespace oceanbase
{
namespace rpc
{
int ObNetQueueTraver::traverse_one_tenant(oceanbase::omt::ObTenant *tenant_ptr, ObINetTraverProcess &process)
{
  int ret = OB_SUCCESS;
  int32_t group_id = 0;  // default group id for mysql request without resource_mgr is 0.
  if (OB_ISNULL(tenant_ptr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("tenant_ptr is nullptr", K(ret));
  } else if (OB_FAIL(traverse_one_tenant_queue(tenant_ptr->get_req_queue(), group_id, process))) {
  }
  return ret;
}
int ObNetQueueTraver::traverse_one_tenant_queue(oceanbase::omt::ReqQueue &tenant_req_queue, int32_t group_id, ObINetTraverProcess &process)
{
  int ret = OB_SUCCESS;
  int64_t prio_cnt = tenant_req_queue.get_prio_cnt();
  for (int64_t i = 0; OB_SUCC(ret) && i < tenant_req_queue.get_queue_num(); i++) {
    for (int64_t j = 0; OB_SUCC(ret) && j < prio_cnt; j++) {
      ObLinkQueue *link_queue = tenant_req_queue.get_queue(i, j);
      if (OB_ISNULL(link_queue)) {
        ret = OB_NULL_CHECK_ERROR;
        LOG_ERROR("link_queue is nullptr", K(ret));
      } else if (OB_FAIL(traverse_one_tenant_one_link_queue(link_queue, group_id, process))) {
      }
    }
  }
  return ret;
}
int ObNetQueueTraver::traverse_one_tenant_one_link_queue(ObLinkQueue *link_queue, int32_t group_id, ObINetTraverProcess &process)
{
  int ret = OB_SUCCESS;
  const int64_t current_time = ObTimeUtility::current_time();
  ObLink *cur = nullptr;
  ObRequest *req_cur = nullptr;
  bool is_end = false;
  if (OB_ISNULL(link_queue)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("link_queue is nullptr", K(ret));
  } else {
    int64_t size = link_queue->size();
    for (int64_t i = 0; OB_SUCC(ret) && (i < size) && (!is_end); i++) {
      if (OB_FAIL(link_queue->pop(cur))) {
        ret = OB_SUCCESS;
        is_end = true;
      } else {
        if (OB_ISNULL(cur)){
          ret = OB_NULL_CHECK_ERROR;
          LOG_ERROR("cur is nullptr", K(ret));
        } else if (OB_ISNULL(req_cur = static_cast<ObRequest *>(cur))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("req_cur is nullptr", K(ret));
        } else if (req_cur->get_traverse_index() >= current_time) {
          is_end = true;
        } else if (OB_FAIL(req_cur->set_traverse_index(current_time))) {
        } else if (OB_FAIL(process.process_request(req_cur, group_id))) {
        }
        // push req
        if (OB_NOT_NULL(cur) && OB_FAIL(link_queue->push(cur))) {
          LOG_ERROR("link_queue push req failed", K(ret));
        }
      }
    }  // for end
  }
  return ret;
}

int ObNetTraverProcessAutoDiag::process_request(ObRequest *req_cur, int32_t group_id)
{
  int ret = OB_SUCCESS;
  ObNetQueueTraRes tmp_info;
  if (OB_FAIL(get_trav_req_info(req_cur, tmp_info, group_id))) {
  } else {
    net_recorder_(tmp_info);
  }
  return ret;
}
int ObNetTraverProcessAutoDiag::get_trav_req_info(ObRequest *cur, ObNetQueueTraRes &tmp_info, int32_t group_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cur)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("cur is nullptr", K(ret));
  } else {
    tmp_info.type_ = cur->get_type();
    tmp_info.enqueue_timestamp_ = cur->get_enqueue_timestamp();
    tmp_info.trace_id_ = cur->get_trace_id();
    tmp_info.retry_times_ = cur->get_retry_times();
    tmp_info.group_id_ = group_id;
    if (ObRequest::OB_RPC == cur->get_type()) {
      // obcall RPC transport removed (single-replica): OB_RPC requests never
      // enter the queues, so there is nothing to traverse here.
    } else if (ObRequest::OB_MYSQL == cur->get_type()) {
      void *sess = SQL_REQ_OP.get_sql_session(cur);
      oceanbase::observer::ObSMConnection *conn = nullptr;
      const obmysql::ObMySQLRawPacket &pkt =
          reinterpret_cast<const obmysql::ObMySQLRawPacket &>(cur->get_packet());
      if (OB_ISNULL(sess)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("sess is nullptr", K(ret));
      } else {
        conn = static_cast<oceanbase::observer::ObSMConnection *>(sess);
        
        tmp_info.sql_session_id_ = static_cast<uint32_t>(conn->sessid_);
        tmp_info.mysql_cmd_ = pkt.get_cmd();
      }
    }
  }
  return ret;
}
}  // namespace rpc
}  // namespace oceanbase
