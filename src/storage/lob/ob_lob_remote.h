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

#include "storage/ob_storage_rpc.h"
#include "storage/lob/ob_lob_rpc_struct.h"
#include "storage/lob/ob_lob_access_param.h"

namespace oceanbase
{
namespace storage
{
class ObLobQueryRemoteReader
{
public:
  ObLobQueryRemoteReader() : rpc_buffer_pos_(0), data_buffer_() {}
  ~ObLobQueryRemoteReader() {}
  int open(ObLobAccessParam& param, common::ObDataBuffer &rpc_buffer);
  int get_next_block(
                     common::ObDataBuffer &rpc_buffer,
                     obrpc::ObStorageRpcProxy::SSHandle<obrpc::OB_LOB_QUERY> &handle,
                     ObString &data);
private:
  int do_fetch_rpc_buffer(
                          common::ObDataBuffer &rpc_buffer,
                          obrpc::ObStorageRpcProxy::SSHandle<obrpc::OB_LOB_QUERY> &handle);
private:
  int64_t rpc_buffer_pos_;
  ObString data_buffer_;
};

struct ObLobRemoteQueryCtx
{
  ObLobRemoteQueryCtx() : handle_(), rpc_buffer_(), query_arg_(), remote_reader_() {}
  obrpc::ObStorageRpcProxy::SSHandle<obrpc::OB_LOB_QUERY> handle_;
  common::ObDataBuffer rpc_buffer_;
  obrpc::ObLobQueryArg query_arg_;
  ObLobQueryRemoteReader remote_reader_;
};


class ObLobRemoteUtil
{
public:
  static int query(ObLobAccessParam& param, const ObLobQueryArg::QueryType qtype, const ObAddr &dst_addr, ObLobRemoteQueryCtx *&ctx);


private:
  static int remote_query_init_ctx(ObLobAccessParam &param, const ObLobQueryArg::QueryType qtype, ObLobRemoteQueryCtx *&ctx);
};

}  // storage
}  // oceanbase

#endif  // OCEANBASE_STORAGE_OB_LOB_REMOTE_H_
