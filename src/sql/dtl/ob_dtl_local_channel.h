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

#ifndef OB_DTL_LOCAL_CHANNEL_H
#define OB_DTL_LOCAL_CHANNEL_H

#include <stdint.h>
#include <functional>
#include "lib/queue/ob_fixed_queue.h"
#include "lib/queue/ob_link_queue.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_print_utils.h"
#include "sql/dtl/ob_dtl_channel.h"
#include "sql/dtl/ob_dtl_linked_buffer.h"
#include "share/ob_scanner.h"
#include "observer/ob_server_struct.h"
#include "sql/dtl/ob_dtl_rpc_proxy.h"
#include "sql/dtl/ob_dtl_basic_channel.h"
#include "sql/dtl/ob_dtl.h"
#include "ob_dtl_interm_result_manager.h"

namespace oceanbase {
namespace sql {
namespace dtl {

class ObDtlLocalChannel : public ObDtlBasicChannel
{
public:
  explicit ObDtlLocalChannel(const uint64_t tenant_id,
     const uint64_t id, const common::ObAddr &peer, DtlChannelType type);
  explicit ObDtlLocalChannel(const uint64_t tenant_id,
     const uint64_t id, const common::ObAddr &peer, const int64_t hash_val, DtlChannelType type);
  virtual ~ObDtlLocalChannel();

  virtual int init() override;
  virtual void destroy();
  
  virtual int feedup(ObDtlLinkedBuffer *&buffer) override;
  virtual int send_message(ObDtlLinkedBuffer *&buf);
private:
  int send_shared_message(ObDtlLinkedBuffer *&buf);
};

}  // dtl
}  // sql
}  // oceanbase

#endif /* OB_DTL_LOCAL_CHANNEL_H */
