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

#include "storage/tablet/ob_batch_create_tablet_pretty_arg.h"
#include "share/ob_rpc_struct.h"

using namespace oceanbase::obrpc;

namespace oceanbase
{
namespace storage
{
ObBatchCreateTabletPrettyArg::ObBatchCreateTabletPrettyArg(const ObBatchCreateTabletArg &arg)
  : arg_(arg)
{
}

int64_t ObBatchCreateTabletPrettyArg::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len <= 0)) {
    // do nothing
  } else {
    J_OBJ_START();
    J_KV("ls_id", arg_.id_,
         "major_frozen_scn", arg_.major_frozen_scn_,
         "total_tablet_cnt", arg_.get_tablet_count());
    J_COMMA();

    BUF_PRINTF("tablets");
    J_COLON();
    J_OBJ_START();
    for (int64_t i = 0; i < arg_.tablets_.count(); ++i) {
      const ObCreateTabletInfo &info = arg_.tablets_.at(i);
      ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();
      J_NEWLINE();
      BUF_PRINTF("[%ld][%s][T%ld] [", GETTID(), GETTNAME(), GET_TENANT_ID());
      BUF_PRINTO(PC(trace_id));
      BUF_PRINTF("] ");
      J_KV("data_tablet_id", info.data_tablet_id_,
           "tablet_ids", info.tablet_ids_,
           "compat_mode", info.compat_mode_,
           "is_create_bind_hidden_tablets", info.is_create_bind_hidden_tablets_,
           "has_cs_replica", info.has_cs_replica_);
    }
    J_NEWLINE();
    J_OBJ_END();
    J_OBJ_END();
  }
  return pos;
}
} // namespace storage
} // namespace oceanbase
