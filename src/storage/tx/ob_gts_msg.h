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

#ifndef OCEANBASE_TRANSACTION_OB_GTS_MSG_
#define OCEANBASE_TRANSACTION_OB_GTS_MSG_

#include "share/ob_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/net/ob_addr.h"
#include "ob_gts_define.h"

namespace oceanbase
{
namespace obrpc
{
class ObGtsRpcResult;
}
namespace transaction
{
class ObGtsRequest
{
  OB_UNIS_VERSION(1);
public:
  ObGtsRequest() : tenant_id_(0), srr_(0), range_size_(0), sender_() {}
  ~ObGtsRequest() {}
  int init(const uint64_t tenant_id, const MonotonicTs srr, const int64_t range_size,
      const common::ObAddr &sender);
  bool is_valid() const;
public:
  uint64_t get_tenant_id() const { return tenant_id_; }
  MonotonicTs get_srr() const { return srr_; }
  const common::ObAddr &get_sender() const { return sender_; }
  TO_STRING_KV(K_(tenant_id), K_(srr), K_(range_size), K_(sender));
private:
  uint64_t tenant_id_;
  MonotonicTs srr_;
  int64_t range_size_;
  common::ObAddr sender_;
};

class ObGtsErrResponse
{
  OB_UNIS_VERSION(1);
public:
  ObGtsErrResponse() : tenant_id_(0), srr_(0), status_(0), sender_() {}
  ~ObGtsErrResponse() {}
  int init(const uint64_t tenant_id, const MonotonicTs srr, const int status,
      const common::ObAddr &sender);
  bool is_valid() const;
public:
  uint64_t get_tenant_id() const { return tenant_id_; }
  MonotonicTs get_srr() const { return srr_; }
  int get_status() const { return status_; }
  const common::ObAddr &get_sender() const { return sender_; }
  TO_STRING_KV(K_(tenant_id), K_(srr), K_(status), K_(sender));
private:
  uint64_t tenant_id_;
  MonotonicTs srr_;
  int status_;
  common::ObAddr sender_;
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_GTS_MSG_
