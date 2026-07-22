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
#ifndef _OCEABASE_OBSERVER_OB_SRV_XLATOR_H_
#define _OCEABASE_OBSERVER_OB_SRV_XLATOR_H_

#include "lib/utility/ob_macro_utils.h"
#include "rpc/frame/ob_req_translator.h"
#include "rpc/obmysql/ob_mysql_translator.h"
#include "observer/ob_server_struct.h"

#define MAX_PCODE 0xFFFF
#define CALLP_BUF_SIZE 1280
union EP_CALLP_BUF;
RLOCAL_EXTERN(EP_CALLP_BUF, co_ep_callp_buf);

// obcall RPC dispatch removed: ObSrvRpcXlator (which derived from
// obcall::ObCallTranslator), its DirectHandler/RPCProcessFunc tables and the
// init_srv_xlator_for_* registration helpers are gone — every observer-service
// RPC is dispatched in-process now, so no obcall packets are translated here.
// ObSrvXlator is MySQL-only; it still derives from rpc::frame::ObReqTranslator
// (the framework translator the shared request queue handler needs).

namespace oceanbase { namespace observer {

using rpc::frame::ObReqProcessor;
using obmysql::ObMySQLTranslator;
using common::ObIAllocator;

extern thread_local bool g_in_sync_dispatch;
ObIAllocator &get_sql_arena_allocator();

template <typename T> void worker_allocator_delete(T *&ptr) {
  if (NULL != ptr) { ptr->~T(); get_sql_arena_allocator().free(ptr); ptr = NULL; }
}

class ObSrvMySQLXlator : public ObMySQLTranslator {
public:
  explicit ObSrvMySQLXlator(const ObGlobalContext &gctx) : gctx_(gctx) {}
  int translate(rpc::ObRequest &req, ObReqProcessor *&processor);
protected:
  ObReqProcessor *get_processor(rpc::ObRequest &) { return NULL; }
  int get_mp_connect_processor(ObReqProcessor *&ret_proc);
private:
  const ObGlobalContext &gctx_;
  DISALLOW_COPY_AND_ASSIGN(ObSrvMySQLXlator);
};

class ObSrvXlator : public rpc::frame::ObReqTranslator {
public:
  explicit ObSrvXlator(const ObGlobalContext &gctx)
      : mysql_xlator_(gctx) {}
  int th_init();
  int th_destroy();
  int release(ObReqProcessor *processor);
protected:
  ObReqProcessor *get_processor(rpc::ObRequest &);
private:
  ObReqProcessor *get_error_mysql_processor(const int ret);
  ObSrvMySQLXlator mysql_xlator_;
  DISALLOW_COPY_AND_ASSIGN(ObSrvXlator);
};

} } // namespace observer, oceanbase
#endif
