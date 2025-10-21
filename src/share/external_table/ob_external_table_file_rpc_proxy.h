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

#ifndef OBDEV_SRC_EXTERNAL_TABLE_FILE_RPC_PROXY_H_
#define OBDEV_SRC_EXTERNAL_TABLE_FILE_RPC_PROXY_H_
#include "share/ob_define.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "share/external_table/ob_external_table_file_task.h"
#include "observer/ob_server_struct.h"
namespace oceanbase
{
namespace obrpc
{
class ObExtenralTableRpcProxy : public obrpc::ObRpcProxy
{
public:
  DEFINE_TO(ObExtenralTableRpcProxy);
  virtual ~ObExtenralTableRpcProxy() {}
  // sync rpc for das task result
  RPC_AP(PR5 flush_file_kvcahce, obrpc::OB_FLUSH_EXTERNAL_TABLE_FILE_CACHE, (share::ObFlushExternalTableFileCacheReq), share::ObFlushExternalTableFileCacheRes);
  RPC_AP(PR5 load_external_file_list, obrpc::OB_LOAD_EXTERNAL_FILE_LIST, (share::ObLoadExternalFileListReq), share::ObLoadExternalFileListRes);
};
}  // namespace obrpc


}  // namespace oceanbase
#endif /* OBDEV_SRC_EXTERNAL_TABLE_FILE_RPC_PROXY_H_ */
