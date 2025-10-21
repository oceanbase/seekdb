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

#ifndef OCEANBASE_OBSERVER_MYSQL_OBMP_QUIT_H_
#define OCEANBASE_OBSERVER_MYSQL_OBMP_QUIT_H_

#include "observer/mysql/obmp_base.h"
#include "sql/engine/dml/ob_trigger_handler.h"

namespace oceanbase
{
namespace observer
{

class ObMPQuit
    : public ObMPBase
{
public:
  static const obmysql::ObMySQLCmd COM = obmysql::COM_QUIT;

  explicit ObMPQuit(const ObGlobalContext &gctx)
      : ObMPBase(gctx)
  {}
  virtual ~ObMPQuit() {}

protected:
  int process();
  int deserialize() { return common::OB_SUCCESS; }

private:
  DISALLOW_COPY_AND_ASSIGN(ObMPQuit);
}; // end of class ObMPQuit

int ObMPQuit::process()
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = NULL;
  if (OB_FAIL(get_session(session))) {
    LOG_WARN("fail to get session", K(ret));
  } else {
    // set NORMAL_QUIT state.
    session->set_disconnect_state(NORMAL_QUIT);
  }
  if (NULL != session) {
    revert_session(session);
  }
  disconnect();
  SERVER_LOG(INFO, "quit");
  return ret;
}

} // end of namespace observer
} // end of namespace oceanbase

#endif // OCEANBASE_OBSERVER_MYSQL_OBMP_QUIT_H_
