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

#define USING_LOG_PREFIX SQL_EXE

#include "sql/executor/ob_executor_rpc_processor.h"

#include "sql/dtl/ob_dtl_interm_result_manager.h"

namespace oceanbase
{
using namespace share;
using namespace common;
using namespace observer;
using namespace storage;
using namespace transaction;
namespace sql
{

ObWorkerSessionGuard::ObWorkerSessionGuard(ObSQLSessionInfo *session)
{
  THIS_WORKER.set_session(session);
  if (nullptr != session) {
    session->set_thread_id(GETTID());
  }
}

ObWorkerSessionGuard::~ObWorkerSessionGuard()
{
  THIS_WORKER.set_session(NULL);
}

int ObRpcEraseIntermResultP::preprocess_arg()
{
  return OB_SUCCESS;
}

int ObRpcEraseIntermResultP::process()
{
  int ret = OB_SUCCESS;
  LOG_TRACE("receive erase interm result request", K(arg_));
  dtl::ObDTLIntermResultKey dtl_int_key;
  ObIArray<uint64_t> &interm_result_ids = arg_.interm_result_ids_;
  for (int64_t i = 0; OB_SUCC(ret) && i < interm_result_ids.count(); ++i) {
    dtl_int_key.channel_id_ = interm_result_ids.at(i);
    if (OB_FAIL(MTL(dtl::ObDTLIntermResultManager*)->erase_interm_result_info(dtl_int_key))) {
      LOG_WARN("failed to erase interm result info in manager.", K(ret));
    }
  }
  return ret;
}

} /* sql */
} /* oceanbase */
