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

#ifndef OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_POOL_H_
#define OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_POOL_H_

#include "lib/list/ob_dlist.h"
#include "common/mysqlclient/ob_isql_connection_pool.h"
#include "lib/lock/ob_thread_cond.h"
#include "ob_inner_sql_connection.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObISQLConnection;
}
class ObServerConfig;
}
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace sql
{
class ObSql;
}
namespace observer
{
class ObVTIterCreator;

class ObInnerSQLConnectionPool : public common::sqlclient::ObISQLConnectionPool
{
public:
  friend class ObInnerSQLConnection;

  ObInnerSQLConnectionPool();
  virtual ~ObInnerSQLConnectionPool();

  int init(share::schema::ObMultiVersionSchemaService *schema_service,
           sql::ObSql *ob_sql,
           ObVTIterCreator *vt_iter_creator_,
           common::ObServerConfig *config = NULL,
           const bool is_ddl = false);

  virtual void stop() { stop_ = true; }
  // wait all connection been released
  virtual int wait();

  // sql string escape
  virtual int escape(const char *from, const int64_t from_size,
      char *to, const int64_t to_size, int64_t &out_size);

  // acquired connection must be released
  virtual int acquire(common::sqlclient::ObISQLConnection *&conn, ObISQLClient *client_addr, const int32_t group_id) override;
  virtual int release(common::sqlclient::ObISQLConnection *conn, const bool success);
  int acquire_spi_conn(sql::ObSQLSessionInfo *session_info, observer::ObInnerSQLConnection *&conn);
  int acquire(sql::ObSQLSessionInfo *session_info,
      common::sqlclient::ObISQLConnection *&conn);

  virtual int on_client_inactive(common::ObISQLClient *client_addr) override;
  virtual common::sqlclient::ObSQLConnPoolType get_type() override { return common::sqlclient::INNER_POOL; }
  void dump_used_conn_list();

  // Dozens of connections may be acquired by one worker because agent virtual
  // tables need inner connections. Too many connections warning may be
  // triggered by parallel execution of complicated sys table queries.
  //
  // 100000 = 50 connections * 2000 workers.
  const static int64_t WARNNING_CONNECTION_CNT = 100000;
  const static int64_t MAX_DUMP_SIZE = 20;

private:
  // allocate a new connection
  int alloc_conn(ObInnerSQLConnection *&conn);
  // destroy and free a connection
  int free_conn(ObInnerSQLConnection *conn);
  // revert connection, called by ObInnerSQLConnection::unref()
  int revert(ObInnerSQLConnection *conn);

  int add_to_used_conn_list(ObInnerSQLConnection *conn);
  int remove_from_used_conn_list(ObInnerSQLConnection *conn);

  bool inited_;
  volatile bool stop_;
  common::ObThreadCond cond_;
  int64_t total_conn_cnt_;
  common::ObDList<ObInnerSQLConnection> used_conn_list_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  sql::ObSql *ob_sql_;
  ObVTIterCreator *vt_iter_creator_;
  common::ObServerConfig *config_;
  bool is_ddl_;

  DISALLOW_COPY_AND_ASSIGN(ObInnerSQLConnectionPool);
};

} // end namespace observer
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_INNER_SQL_CONNECTION_POOL_H_
