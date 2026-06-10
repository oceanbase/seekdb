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

#ifndef OBDEV_SRC_SQL_DAS_OB_DAS_ID_RPC_H_
#define OBDEV_SRC_SQL_DAS_OB_DAS_ID_RPC_H_
#include "observer/ob_server_struct.h"
#include "ob_das_id_cache.h"
#include "lib/atomic/ob_atomic.h"
namespace oceanbase
{
namespace sql
{
class ObDASIDRequest
{
  OB_UNIS_VERSION(1);
public:
  ObDASIDRequest() : tenant_id_(0), range_(0) {}
  ~ObDASIDRequest() {}
  int init(const uint64_t tenant_id, const int64_t range);
  bool is_valid() const;
public:
  uint64_t get_tenant_id() const { return tenant_id_; }
  int64_t get_range() const { return range_; }
  TO_STRING_KV(K_(tenant_id), K_(range));
private:
  uint64_t tenant_id_;
  int64_t range_;
};
} // namespace sql

namespace obcall
{
class ObDASIDRpcResult
{
  OB_UNIS_VERSION(1);
public:
  ObDASIDRpcResult() : tenant_id_(0), status_(0), start_id_(0), end_id_(0) {}
  virtual ~ObDASIDRpcResult() {}
  int init(const uint64_t tenant_id, const int status, const int64_t start_id, const int64_t end_id);
  uint64_t get_tenant_id() const { return tenant_id_; }
  int get_status() const { return status_; }
  int64_t get_start_id() const { return start_id_; }
  int64_t get_end_id() const { return end_id_; }
  bool is_valid() const;
  TO_STRING_KV(K_(tenant_id), K_(status), K_(start_id), K_(end_id));
private:
  uint64_t tenant_id_;
  int status_;
  int64_t start_id_;
  int64_t end_id_;
};

} // namespace obcall

namespace sql
{
class ObDASIDRequestRpc
{
public:
  ObDASIDRequestRpc();
  ~ObDASIDRequestRpc() { destroy(); }
  int init(const common::ObAddr &self,
           ObDASIDCache *id_cache);
  void destroy();
  int fetch_new_range(const ObDASIDRequest &msg,
                      obcall::ObDASIDRpcResult &res,
                      const int64_t timeout,
                      const bool force_renew);
private:
  bool is_inited_;
  bool is_running_;
  common::ObAddr self_;
  int64_t local_id_counter_ CACHE_ALIGNED; // Local auto-increment counter for DAS ID, resets to 0 on restart
};
} // namespace sql
} // namespace oceanbase
#endif // OBDEV_SRC_SQL_DAS_OB_DAS_ID_RPC_H_
