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

#ifndef OCEANBASE_OBRPC_OB_RPC_KEEPALIVE_H_
#define OCEANBASE_OBRPC_OB_RPC_KEEPALIVE_H_

#include "lib/thread/thread_pool.h"
#include "lib/net/ob_addr.h"
#include "util/easy_inet.h"

namespace oceanbase
{
namespace obrpc
{
struct ObNetKeepAliveData
{
public:
  ObNetKeepAliveData()
    : rs_server_status_(0) {}
  int encode(char *buf, const int64_t buf_len, int64_t &pos) const;
  int decode(const char *buf, const int64_t data_len, int64_t &pos);
  int32_t get_encoded_size() const;
  int32_t rs_server_status_;
  int64_t start_service_time_;
};

class ObNetKeepAlive
{
public:
  static ObNetKeepAlive &get_instance();
  int in_black(const common::ObAddr &addr, bool &in_blacklist, ObNetKeepAliveData *ka_data);
  int get_last_resp_ts(const common::ObAddr &addr, int64_t &last_resp_ts);
};
}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* OCEANBASE_OBRPC_OB_RPC_KEEPALIVE_H_ */
