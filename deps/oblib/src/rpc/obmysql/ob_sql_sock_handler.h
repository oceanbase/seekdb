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

#ifndef OCEANBASE_OBMYSQL_OB_SQL_SOCK_HANDLER_H_
#define OCEANBASE_OBMYSQL_OB_SQL_SOCK_HANDLER_H_
#include "rpc/obmysql/ob_i_sm_conn_callback.h"
#include "rpc/obmysql/ob_i_sql_sock_handler.h"
#include "rpc/obmysql/ob_sql_sock_session.h"

namespace oceanbase
{
namespace rpc {
namespace frame { class ObReqDeliver;};
};
namespace obmysql
{
class ObSqlSockProcessor;
class ObSqlNio;
class ObSqlSockHandler: public ObISqlSockHandler
{
public:
  ObSqlSockHandler(ObISMConnectionCallback& conn_cb, ObSqlSockProcessor& sock_processor, ObSqlNio& nio):
      conn_cb_(conn_cb), sock_processor_(sock_processor), deliver_(nullptr), nio_(&nio) {}
  virtual ~ObSqlSockHandler() {}
  int init(rpc::frame::ObReqDeliver* deliver);
  virtual int on_readable(void* sess) override;
  virtual void on_close(void* sess, int err) override;
  virtual int on_connect(void* sess, int fd, bool is_unix_socket) override;
  virtual void on_flushed(void* sess) override;
private:
  ObISMConnectionCallback& conn_cb_;
  ObSqlSockProcessor& sock_processor_;
  rpc::frame::ObReqDeliver* deliver_;
  ObSqlNio* nio_;
};

}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_OBMYSQL_OB_SQL_SOCK_HANDLER_H_ */

