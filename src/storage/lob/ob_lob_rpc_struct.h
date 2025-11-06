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

#ifndef OCEANBASE_OB_LOB_RPC_STRUCT_
#define OCEANBASE_OB_LOB_RPC_STRUCT_

#include "share/ob_define.h"
#include "rpc/obrpc/ob_rpc_packet.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_processor.h"
#include "rpc/obrpc/ob_rpc_result_code.h"
#include "share/ob_rpc_struct.h"
#include "share/config/ob_server_config.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{

namespace obrpc
{

struct ObLobQueryBlock
{
  OB_UNIS_VERSION(1);
public:
  ObLobQueryBlock();
  virtual ~ObLobQueryBlock() {}
  void reset();
  bool is_valid() const;

  TO_STRING_KV(K_(size));
  int64_t size_;
};

class ObLobQueryArg final
{
  OB_UNIS_VERSION(1);
public:
  enum QueryType {
    READ = 0,
    GET_LENGTH
  };
  ObLobQueryArg();
  ~ObLobQueryArg();
  TO_STRING_KV(K_(tenant_id), K_(offset), K_(len), K_(cs_type), K_(qtype), K_(scan_backward), K_(lob_locator),
      K_(enable_remote_retry));
public:
  static const int64_t OB_LOB_QUERY_BUFFER_LEN = 256*1024L;
  static const int64_t OB_LOB_QUERY_OLD_LEN_REFACTOR = 8;
  uint64_t tenant_id_;
  uint64_t offset_; // char offset
  uint64_t len_; // char len
  common::ObCollationType cs_type_;
  bool scan_backward_;
  QueryType qtype_;
  ObLobLocatorV2 lob_locator_;
  bool enable_remote_retry_;
  DISALLOW_COPY_AND_ASSIGN(ObLobQueryArg);
};

} // obrpc

} // oceanbase

#endif // OCEANBASE_OB_LOB_RPC_STRUCT_
