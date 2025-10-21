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

#ifndef OCEANBASE_PALF_CLUSTER_RPC_REQ_H_
#define OCEANBASE_PALF_CLUSTER_RPC_REQ_H_

#include "lib/utility/ob_unify_serialize.h"                    // OB_UNIS_VERSION
#include "lib/utility/ob_print_utils.h"                        // TO_STRING_KV
#include "common/ob_member_list.h"                             // ObMemberList
#include "logservice/palf/palf_handle_impl.h"                  // PalfStat
#include "share/scn.h"
#include "logservice/palf/log_writer_utils.h"                  // LogWriteBuf

namespace oceanbase
{
namespace palfcluster
{

struct LogCreateReplicaCmd {
  OB_UNIS_VERSION(1);
public:
  LogCreateReplicaCmd();
  LogCreateReplicaCmd(const common::ObAddr &src,
                  const int64_t ls_id,
                  const common::ObMemberList &member_list,
                  const int64_t replica_num,
                  const int64_t leader_idx);
  ~LogCreateReplicaCmd()
  {
    reset();
  }
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(src), K_(ls_id), K_(member_list), K_(replica_num), K_(leader_idx));
  common::ObAddr src_;
  int64_t ls_id_;
  common::ObMemberList member_list_;
  int64_t replica_num_;
  int64_t leader_idx_;
};

struct SubmitLogCmd {
  OB_UNIS_VERSION(1);
public:
  SubmitLogCmd();
  SubmitLogCmd(const common::ObAddr &src,
               const int64_t ls_id,
               const int64_t client_id,
               const palf::LogWriteBuf &log_buf_);
  ~SubmitLogCmd()
  {
    reset();
  }
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(src), K_(ls_id));
  common::ObAddr src_;
  int64_t ls_id_;
  int64_t client_id_;
  palf::LogWriteBuf log_buf_;
};

struct SubmitLogCmdResp {
  OB_UNIS_VERSION(1);
public:
  SubmitLogCmdResp();
  SubmitLogCmdResp(const common::ObAddr &src,
                   const int64_t ls_id,
                   const int64_t client_id);
  ~SubmitLogCmdResp()
  {
    reset();
  }
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(src), K_(ls_id));
  common::ObAddr src_;
  int64_t ls_id_;
  int64_t client_id_;
};

} // end namespace palfcluster
}// end namespace oceanbase

#endif
