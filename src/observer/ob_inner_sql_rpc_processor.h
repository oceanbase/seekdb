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

#ifndef OBDEV_SRC_OBSERVER_INNER_SQL_RPC_PROCESSOR_H_
#define OBDEV_SRC_OBSERVER_INNER_SQL_RPC_PROCESSOR_H_

#include "share/ob_scanner.h"
#include "observer/ob_server_struct.h"
#include "observer/ob_inner_sql_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_processor.h"
#include "sql/session/ob_sql_session_mgr.h"

namespace oceanbase
{

namespace obrpc
{

class ObInnerSqlRpcP : public obrpc::ObRpcProcessor< obrpc::ObInnerSQLRpcProxy::ObRpc<obrpc::OB_INNER_SQL_SYNC_TRANSMIT> >
{
public:
  ObInnerSqlRpcP(const observer::ObGlobalContext &gctx) : gctx_(gctx) {}
  virtual ~ObInnerSqlRpcP() {}

public:
  virtual int process();
private:
  int create_tmp_session(
      uint64_t tenant_id,
      sql::ObSQLSessionInfo *&tmp_session,
      sql::ObFreeSessionCtx &free_session_ctx,
      const bool is_oracle_mode);
  void cleanup_tmp_session(
      sql::ObSQLSessionInfo *&tmp_session,
      sql::ObFreeSessionCtx &free_session_ctx);

  int process_start_transaction(
      sqlclient::ObISQLConnection *conn,
      const ObSqlString &start_trans_sql,
      const ObInnerSQLTransmitArg &transmit_arg,
      ObInnerSQLTransmitResult &transmit_result);
  int process_register_mds(sqlclient::ObISQLConnection *con,
                           const ObInnerSQLTransmitArg &arg);
  int process_rollback(sqlclient::ObISQLConnection *conn);
  int process_commit(sqlclient::ObISQLConnection *conn);
  int process_write(
      sqlclient::ObISQLConnection *conn,
      const ObSqlString &write_sql,
      const ObInnerSQLTransmitArg &transmit_arg,
      ObInnerSQLTransmitResult &transmit_result);
  int process_read(
      sqlclient::ObISQLConnection *conn,
      const ObSqlString &read_sql,
      const ObInnerSQLTransmitArg &transmit_arg,
      ObInnerSQLTransmitResult &transmit_result);
  int set_session_param_to_conn(
      sqlclient::ObISQLConnection *conn,
      const ObInnerSQLTransmitArg &transmit_arg);
  const observer::ObGlobalContext &gctx_;
  DISALLOW_COPY_AND_ASSIGN(ObInnerSqlRpcP);
};

}
}
#endif /* OBDEV_SRC_OBSERVER_INNER_SQL_RPC_PROCESSOR_H_ */
