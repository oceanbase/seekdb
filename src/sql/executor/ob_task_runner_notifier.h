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

#ifndef OCEANBASE_SQL_EXECUTOR_OB_TASK_RUNNER_NOTIFIER_
#define OCEANBASE_SQL_EXECUTOR_OB_TASK_RUNNER_NOTIFIER_

#include "share/ob_define.h"
#include "sql/session/ob_sql_session_mgr.h"

namespace oceanbase
{
namespace sql
{
class ObTaskRunnerNotifier
{
public:
  ObTaskRunnerNotifier(ObSQLSessionInfo *session, ObSQLSessionMgr *mgr)
      : session_(session), mgr_(mgr)
  {
  }

  virtual ~ObTaskRunnerNotifier() {}

  virtual int kill()
  {
    int ret = OB_SUCCESS;
    if (NULL != mgr_ && NULL != session_) {
      if (OB_FAIL(mgr_->kill_query(*session_))) {
        SQL_LOG(WARN, "kill query failed", K(ret), K(session_->get_server_sid()));
      }
    }
    return ret;
  }

private:
  ObSQLSessionInfo *session_;
  ObSQLSessionMgr *mgr_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTaskRunnerNotifier);
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_OB_TASK_RUNNER_NOTIFIER_ */
