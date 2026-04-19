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
#define USING_LOG_PREFIX RPC_OBRPC
#endif
#include "rpc/obrpc/ob_poc_rpc_server.h"
#include "rpc/obrpc/ob_net_keepalive.h"

#define rk_log_macro(level, ret, format, ...) _OB_LOG_RET(level, ret, "PNIO " format, ##__VA_ARGS__)
#include "lib/lock/ob_futex.h"
#ifdef _WIN32
static inline int cfgi_impl(const char *k, const char *v) {
  const char *e = getenv(k);
  return atoi(e ? e : v);
}
#define cfgi(k, v) cfgi_impl(k, v)
#else
#define cfgi(k, v) atoi(getenv(k)?:v)
#endif
namespace oceanbase
{
namespace obrpc
{
extern const int easy_head_size;
int64_t  OB_WEAK_SYMBOL get_max_rpc_packet_size() {
  return OB_MAX_RPC_PACKET_LENGTH;
}
void OB_WEAK_SYMBOL stream_rpc_register(const int64_t pkt_id, int64_t send_time_us)
{
  UNUSED(pkt_id);
  UNUSED(send_time_us);
  RPC_LOG_RET(WARN, OB_ERR_UNEXPECTED, "should not reach here");
}
void OB_WEAK_SYMBOL stream_rpc_unregister(const int64_t pkt_id)
{
  UNUSED(pkt_id);
  RPC_LOG_RET(WARN, OB_ERR_UNEXPECTED, "should not reach here");
}
int OB_WEAK_SYMBOL stream_rpc_reverse_probe(const ObRpcReverseKeepaliveArg& reverse_keepalive_arg)
{
  UNUSED(reverse_keepalive_arg);
  return OB_ERR_UNEXPECTED;
}
}; // end namespace obrpc
}; // end namespace oceanbase

using namespace oceanbase::common;
using namespace oceanbase::obrpc;
using namespace oceanbase::rpc;


extern "C" {
int tranlate_to_ob_error(int err) {
  int ret = OB_SUCCESS;
  if (0 == err) {
  } else if (ENOMEM == err || -ENOMEM == err) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (EINVAL == err || -EINVAL == err) {
    ret = OB_INVALID_ARGUMENT;
  } else if (EIO == err || -EIO == err) {
    ret = OB_IO_ERROR;
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}
};
