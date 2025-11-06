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

#ifndef __COMMON_OB_SERVER_CONNECTION_POOL__
#define __COMMON_OB_SERVER_CONNECTION_POOL__

#include <mysql.h>
#include "lib/lock/ob_spin_lock.h"
#include "lib/mysqlclient/ob_connection_allocator.h"
#include "lib/mysqlclient/ob_mysql_connection.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObMySQLConnectionPool;
class ObServerConnectionPool : public ObCommonServerConnectionPool
{
public:
  ObServerConnectionPool();
  ~ObServerConnectionPool();
  int acquire(ObMySQLConnection *&connection, uint32_t sessid);
  int release(common::sqlclient::ObISQLConnection *conn, const bool succ) override;
  uint64_t get_busy_count(void) const;
public:
  int init(ObMySQLConnectionPool *root,
           const common::ObAddr &server,
           int64_t max_allowed_conn_count);
  int destroy();
  void reset();
  void renew();
  int64_t last_renew_time(void) const;
  void reset_idle_conn_to_sys_tenant();
  void set_server_gone(bool gone);
  const char *get_db_user() const;   const char *get_db_pass() const;
  const char *get_db_name() const;
  int32_t get_port() const;
  common::ObAddr &get_server();
  ObMySQLConnectionPool *get_root();
  void close_all_connection();
  void dump();
  TO_STRING_KV(K_(server),
               K_(free_conn_count),
               K_(busy_conn_count));
  uint64_t get_tenant_id() const { return tenant_id_; }
  void set_tenant_id(uint64_t v) { tenant_id_ = v; }
private:
  ObSimpleConnectionAllocator<ObMySQLConnection> connection_pool_;
  ObIConnectionAllocator<ObMySQLConnection> *connection_pool_ptr_;
  ObMySQLConnectionPool *root_;
  uint64_t tenant_id_;
  char db_user_[OB_MAX_USER_NAME_LENGTH + OB_MAX_TENANT_NAME_LENGTH + OB_MAX_CLUSTER_NAME_LENGTH + 1];
  char db_pass_[OB_MAX_PASSWORD_LENGTH];
  char db_name_[OB_MAX_DATABASE_NAME_LENGTH];
  int32_t port_; // used by dblink to connect, instead of using server_ to connect
  common::ObAddr server_; // shared by connections in this pool
  common::ObSpinLock pool_lock_;
  int64_t last_renew_timestamp_;
  int64_t connection_version_;
  uint64_t max_allowed_conn_count_;
  bool server_not_available_;
};

inline int32_t ObServerConnectionPool::get_port() const
{
  return port_;
}

inline void ObServerConnectionPool::set_server_gone(bool gone)
{
  server_not_available_ = gone;
}

inline const char *ObServerConnectionPool::get_db_user() const
{
  return db_user_;
}

inline const char *ObServerConnectionPool::get_db_pass() const
{
  return db_pass_;
}
inline const char *ObServerConnectionPool::get_db_name() const
{
  return db_name_;
}

inline void ObServerConnectionPool::renew()
{
  server_not_available_ = false;
  last_renew_timestamp_ = ::oceanbase::common::ObTimeUtility::current_time();
}
inline int64_t ObServerConnectionPool::last_renew_time(void) const
{
  return last_renew_timestamp_;
}
inline common::ObAddr &ObServerConnectionPool::get_server()
{
  return server_;
}
inline ObMySQLConnectionPool *ObServerConnectionPool::get_root()
{
  return root_;
}


}
}
}

#endif // __COMMON_OB_SERVER_CONNECTION_POOL__
