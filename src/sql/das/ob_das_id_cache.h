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

#ifndef OCEANBASE_OB_DAS_ID_CACHE_H
#define OCEANBASE_OB_DAS_ID_CACHE_H

namespace oceanbase
{
namespace obrpc
{
class ObDASIDRpcProxy;
}
namespace rpc
{
namespace frame
{
class ObReqTransport;
}
}
namespace sql
{
class ObDASIDRequestRpc;

struct IdCache
{
  int64_t start_id CACHE_ALIGNED;
  int64_t end_id;
  TO_STRING_KV(K(start_id), K(end_id));
};
class ObDASIDCache
{
public:
  ObDASIDCache() { reset(); }
  ~ObDASIDCache() { destroy(); }
  int init(const common::ObAddr &server, rpc::frame::ObReqTransport *req_transport);
  void destroy();
  void reset();
  int update_das_id(const int64_t start_id, const int64_t end_id);
  int get_das_id(int64_t &das_id, const bool force_renew);
private:
  void update_preallocate_count_();
  int64_t get_preallocate_count_();
public:
  TO_STRING_KV(K_(is_inited), K_(cur_idx), K_(cache_idx), K_(id_service_leader));
public:
  static const int64_t MIN_PREALLOCATE_COUNT = 1000000; // 1 million
  static const int64_t MAX_PREALLOCATE_COUNT = MIN_PREALLOCATE_COUNT * 10;
  static const int64_t UPDATE_FACTOR = 4;
  static const int64_t MAX_CACHE_NUM = 16;
  static const int64_t PRE_CACHE_NUM = MAX_CACHE_NUM / 4;
  static const int64_t OB_DAS_ID_RPC_TIMEOUT_MIN = 100 * 1000L;       // 100ms
  static const int64_t OB_DAS_ID_RPC_TIMEOUT_MAX = 2 * 1000L * 1000L; // 2s
  static const int64_t UPDATE_PREALLOCATE_COUNT_INTERVAL = OB_DAS_ID_RPC_TIMEOUT_MIN;
private:
  bool is_inited_;
  bool is_requesting_;
  IdCache id_cache_[MAX_CACHE_NUM]; // Segment and scatter the Cache to avoid multiple threads competing concurrently, causing access hotspots
  int64_t cur_idx_;
  int64_t cache_idx_;
  common::ObAddr server_;
  obrpc::ObDASIDRpcProxy *id_rpc_proxy_;
  ObDASIDRequestRpc *id_request_rpc_;
  common::ObAddr id_service_leader_;
  int64_t retry_request_cnt_;
  common::ObLatch lock_;
  int64_t preallocate_count_;
  int64_t last_update_ts_;
  common::ObArenaAllocator alloc_;
};
} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_OB_DAS_ID_CACHE_H
