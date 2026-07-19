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

#ifndef OCEANBASE_STORAGE_OB_LOB_REMOTE_H_
#define OCEANBASE_STORAGE_OB_LOB_REMOTE_H_

#include "storage/lob/ob_lob_rpc_struct.h"
#include "storage/lob/ob_lob_access_param.h"

namespace oceanbase
{
namespace storage
{
class ObLobQueryIter;

// cross-tenant LOB obcall RPC removed: the cross-tenant LOB read used to loop back to
// this same machine via the OB_LOB_QUERY streaming RPC.
// It is now executed fully in-process under MTL_SWITCH to the lob's tenant, driving the same
// local ObLobQueryIter the OB_LOB_QUERY processor used. ObLobRemoteQueryCtx therefore owns the
// in-process iterator (READ) / cached length (GET_LENGTH) instead of an SSHandle stream.

struct ObLobRemoteQueryCtx
{
  ObLobRemoteQueryCtx()
    : qtype_(obcall::ObLobQueryArg::QueryType::READ),
      query_iter_(nullptr), length_(0), read_buf_(nullptr), read_buf_len_(0) {}
  ~ObLobRemoteQueryCtx();
  // get next block of lob data for READ; runs the in-process iterator under the lob's tenant.
  int get_next_block(ObString &data);

  obcall::ObLobQueryArg::QueryType qtype_;
  ObLobQueryIter *query_iter_; // READ: in-process lob query iterator (owned)
  uint64_t length_;            // GET_LENGTH: lob length
  char *read_buf_;             // READ: output buffer fed to query_iter_->get_next_row
  int64_t read_buf_len_;
};


class ObLobRemoteUtil
{
public:
  static int query(ObLobAccessParam& param, const obcall::ObLobQueryArg::QueryType qtype, const ObAddr &dst_addr, ObLobRemoteQueryCtx *&ctx);


private:
  static int remote_query_init_ctx(ObLobAccessParam &param, const obcall::ObLobQueryArg::QueryType qtype, ObLobRemoteQueryCtx *&ctx);
};

}  // storage
}  // oceanbase

#endif  // OCEANBASE_STORAGE_OB_LOB_REMOTE_H_
