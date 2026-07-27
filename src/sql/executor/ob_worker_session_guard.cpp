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

#include "sql/executor/ob_worker_session_guard.h"

#include "lib/worker.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
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

} /* sql */
} /* oceanbase */
