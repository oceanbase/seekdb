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

#include "ob_batch_rpc.h"
#include "lib/thread_local/thread_buffer.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace obrpc
{
static char* get_rpc_buffer(int64_t& size)
{
  return get_tc_buffer(size);
}

static int build_batch_packet(const ObAddr &sender, const uint32_t batch_type, const uint32_t sub_type,
    const ObIFill& req, ObBatchPacket *&pkt,
    bool &is_dynamic_alloc)
{
  int ret = OB_SUCCESS;
  bool is_retry = false;
  ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();
  uint32_t flag = 1;
  while (true) {
    bool need_retry = false;
    ret = OB_SUCCESS;
    int64_t limit = 0;
    ObSimpleReqHeader *req_header = NULL;
    const int64_t header_end_pos = sizeof(*pkt) + sizeof(*req_header);
    if (OB_ISNULL(pkt = (ObBatchPacket *)get_rpc_buffer(limit))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      is_dynamic_alloc = false;
      if (req.get_estimate_size() > (limit * 4 / 5) || is_retry) {
        limit = req.get_req_size() + header_end_pos + 1024;
        if (OB_ISNULL(pkt = (ObBatchPacket *)ob_malloc(limit, SET_USE_500("RPC_BATCH_BUF")))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          is_dynamic_alloc = true;
        }
      }
    }
    if (OB_SUCC(ret)) {
      req_header = (ObSimpleReqHeader*)(pkt + 1);
      char *buf = (char*)pkt + header_end_pos;
      int64_t pos = 0;
      int64_t filled_size = 0;
      int64_t total_req_size = 0;
      if (OB_FAIL(trace_id->serialize(buf, limit - header_end_pos, pos))) {
        RPC_LOG(WARN, "serialize traceid failed", K(ret), K(sender), K(batch_type), K(sub_type));
      } else if (OB_FAIL(req.fill_buffer(buf + pos, limit - header_end_pos - pos, filled_size))) {
        if (OB_SIZE_OVERFLOW != ret) {
          RPC_LOG(WARN, "serialize request failed", K(ret), K(sender), K(batch_type), K(sub_type));
        }
      } else {
        total_req_size = pos + filled_size;
        pkt->set((int32_t)(sizeof(*req_header) + total_req_size), sender, (char*)(pkt + 1));
        req_header->set(flag, batch_type, sub_type, (int32_t)total_req_size);
      }
      if (OB_FAIL(ret) && !is_retry) {
        need_retry = true;
        is_retry = true;
      }
    }
    if (!need_retry) {
      break;
    }
  }
  if (OB_FAIL(ret) && is_dynamic_alloc) {
    ob_free(pkt);
    pkt = NULL;
    is_dynamic_alloc = false;
  }
  return ret;
}

static int build_batch_packet(const ObAddr &sender, const uint32_t batch_type, const int16_t sub_type,
    const ObLSID& ls, const ObIFill& req, ObBatchPacket *&pkt,
    bool &is_dynamic_alloc)
{
  int ret = OB_SUCCESS;
  bool is_retry = false;
  ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();
  uint32_t flag = 1;
  while (true) {
    bool need_retry = false;
    ret = OB_SUCCESS;
    int64_t limit = 0;
    ObSimpleReqHeader *req_header = NULL;
    const int64_t header_end_pos = sizeof(*pkt) + sizeof(*req_header);
    if (OB_ISNULL(pkt = (ObBatchPacket *)get_rpc_buffer(limit))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      is_dynamic_alloc = false;
      if (req.get_estimate_size() > (limit * 4 / 5) || is_retry) {
        limit = req.get_req_size() + header_end_pos + 1024;
        if (OB_ISNULL(pkt = (ObBatchPacket *)ob_malloc(limit, SET_USE_500("RPC_BATCH_BUF")))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          is_dynamic_alloc = true;
        }
      }
    }
    if (OB_SUCC(ret)) {
      req_header = (ObSimpleReqHeader*)(pkt + 1);
      char *buf = (char*)pkt + header_end_pos;
      int64_t pos = 0;
      int64_t filled_size = 0;
      int64_t total_req_size = 0;
      if (OB_FAIL(trace_id->serialize(buf, limit - header_end_pos, pos))) {
        RPC_LOG(WARN, "serialize traceid failed", K(ret), K(sender), K(batch_type), K(sub_type), K(ls));
      } else if (OB_FAIL(ls.serialize(buf, limit - header_end_pos, pos))) {
        RPC_LOG(WARN, "serialize ls failed", K(ret), K(sender), K(batch_type), K(sub_type), K(ls));
      } else if (OB_FAIL(req.fill_buffer(buf + pos, limit - header_end_pos - pos, filled_size))) {
        if (OB_SIZE_OVERFLOW != ret) {
          RPC_LOG(WARN, "serialize request failed", K(ret), K(sender), K(batch_type), K(sub_type), K(ls));
        }
      } else {
        total_req_size = pos + filled_size;
        pkt->set((int32_t)(sizeof(*req_header) + total_req_size), sender, (char*)(pkt + 1));
        req_header->set(flag, batch_type, sub_type, (int32_t)total_req_size);
      }
      if (OB_FAIL(ret) && !is_retry) {
        need_retry = true;
        is_retry = true;
      }
    }
    if (!need_retry) {
      break;
    }
  }
  if (OB_FAIL(ret) && is_dynamic_alloc) {
    ob_free(pkt);
    pkt = NULL;
    is_dynamic_alloc = false;
  }
  return ret;
}

int ObBatchRpc::init(rpc::frame::ObReqTransport *transport, const common::ObAddr &self_addr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(rpc_.init(transport, self_addr))) {
    RPC_LOG(WARN, "rpc init failed", K(ret));
  } else {
    self_ = self_addr;
    is_inited_ = true;
  }
  return ret;
}

int ObBatchRpc::post(const uint64_t tenant_id, const common::ObAddr &dest, const int64_t dst_cluster_id,
                     const uint32_t batch_type, const uint32_t sub_type,
                     const Req& req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_CLUSTER_ID == dst_cluster_id
        || !is_valid_tenant_id(tenant_id)
        || !dest.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBatchPacket *pkt = NULL;
    bool is_dynamic_alloc = false;
    if (OB_FAIL(build_batch_packet(self_, batch_type, sub_type, req, pkt, is_dynamic_alloc))) {
      RPC_LOG(WARN, "build_batch_packet fail", K(ret));
    } else if (OB_ISNULL(pkt)) {
      ret = OB_ERR_UNEXPECTED;
      RPC_LOG(WARN, "pkt is NULL", K(ret));
    } else {
      if (OB_FAIL(rpc_.post_batch(tenant_id, dest, dst_cluster_id, batch_type, *pkt))) {
        RPC_LOG(WARN, "post_batch fail", K(ret));
      }
      if (OB_UNLIKELY(is_dynamic_alloc)) {
        ob_free(pkt);
        pkt = NULL;
      }
    }
  }
  return ret;
}

int ObBatchRpc::post(const uint64_t tenant_id, const common::ObAddr &dest, const int64_t dst_cluster_id,
                     const uint32_t batch_type, const int16_t sub_type, const ObLSID& ls,
                     const Req& req)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_INVALID_CLUSTER_ID == dst_cluster_id
        || !is_valid_tenant_id(tenant_id)
        || !dest.is_valid()
        || !ls.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObBatchPacket *pkt = NULL;
    bool is_dynamic_alloc = false;
    if (OB_FAIL(build_batch_packet(self_, batch_type, sub_type, ls, req, pkt, is_dynamic_alloc))) {
      RPC_LOG(WARN, "build_batch_packet fail", K(ret));
    } else if (OB_ISNULL(pkt)) {
      ret = OB_ERR_UNEXPECTED;
      RPC_LOG(WARN, "pkt is NULL", K(ret));
    } else {
      if (OB_FAIL(rpc_.post_batch(tenant_id, dest, dst_cluster_id, batch_type, *pkt))) {
        RPC_LOG(WARN, "post_batch fail", K(ret));
      }
      if (OB_UNLIKELY(is_dynamic_alloc)) {
        ob_free(pkt);
        pkt = NULL;
      }
    }
  }
  return ret;
}

}; // end namespace obrpc
}; // end namespace oceanbase
