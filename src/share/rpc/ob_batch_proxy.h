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

#ifndef OCEANBASE_RPC_OB_BATCH_PROXY_H_
#define OCEANBASE_RPC_OB_BATCH_PROXY_H_
#include "lib/ob_define.h"
// Preserve the transitive include the legacy proxy header provided so that
// downstream TUs keep seeing obcall types (e.g. obcall::LogMemberGCStat).
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace obcall
{
// Lightweight buffer-fill interface. The obcall ObBatchRpc transport (proxy +
// processor + ObBatchPacket) has been removed for single-replica seekdb, but
// ObIFill is still used as the serialize-into-buffer contract by transaction
// messages (ObTxMsg) and SQL task events.
class ObIFill
{
public:
  ObIFill() {}
  virtual ~ObIFill() {}
  virtual int fill_buffer(char* buf, int64_t size, int64_t &filled_size) const = 0;
  virtual int64_t get_req_size() const = 0;
  virtual int64_t get_estimate_size() const { return 0; }
};

};
};


#endif /* OCEANBASE_RPC_OB_BATCH_PROXY_H_ */
