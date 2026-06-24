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

#ifndef __COMMON_OB_MYSQL_CONNECTION_POOL__
#define __COMMON_OB_MYSQL_CONNECTION_POOL__

#include <mysql.h>
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_link_hashmap.h"                   // ObLinkHashMap
#include "lib/task/ob_timer.h"
#include "lib/list/ob_list.h"
#include "lib/allocator/ob_cached_allocator.h"
#include "lib/mysqlclient/ob_mysql_connection.h"
#include "lib/net/ob_addr.h"
#include "lib/mysqlclient/ob_isql_connection_pool.h"

namespace oceanbase
{
namespace common
{

class ObMySQLProxy;
class ObMySQLProxyUtil;
class ObMySQLTransaction;
class ObCommonMySQLProvider;
namespace sqlclient
{
class ObServerConnectionPool;
class ObMySQLServerProvider;
enum MySQLConnectionPoolType
{
  SERVER_POOL = 0,
  TENANT_POOL
};
// Tenant level Server Connection Pool
// hold pointer list of server_conn_pool and provide ServerPool by round-robin
typedef common::ObSEArray<ObServerConnectionPool *, 16> TenantServerConnArray;
class ObTenantServerConnectionPool
{
public:
  ObTenantServerConnectionPool();
  ~ObTenantServerConnectionPool();
  void reset();
public:
  int get_server_pool(ObServerConnectionPool *&server_pool);
  int renew(const TenantServerConnArray &new_server_conn_pool_list);
  TO_STRING_KV(K_(cursor), "tenant_server_cnt", server_pool_list_.count());
private:
  int64_t                 cursor_;
  TenantServerConnArray   server_pool_list_;
};

typedef common::ObList<ObServerConnectionPool *, common::ObArenaAllocator> ServerList;
class ObMySQLConnectionPool : public common::ObTimerTask, public ObISQLConnectionPool
{
public:
  friend class common::ObMySQLProxy;
  friend class common::ObMySQLProxyUtil;
  friend class common::ObMySQLTransaction;

  static const char *const DEFAULT_DB_USER;
  static const char *const DEFAULT_DB_PASS;
  static const char *const DEFAULT_DB_NAME;
  static const int64_t DEFAULT_TRANSACTION_TIMEOUT_US = 100 * 1000 * 1000;
public:
  ObMySQLConnectionPool();
  ~ObMySQLConnectionPool();

  void set_server_provider(ObMySQLServerProvider *provider);
  void update_config(const ObConnPoolConfigParam &config) { config_ = config; }
  const ObConnPoolConfigParam &get_config() const { return config_; }
  int set_db_param(const char *db_user = DEFAULT_DB_USER,
                    const char *db_pass = DEFAULT_DB_PASS, const char *db_name = DEFAULT_DB_NAME);
  int set_db_param(const ObString &db_user, const ObString &db_pass,
                    const ObString &db_name);
  int start(int tg_id);
  void stop();
  void signal_refresh();
  void close_all_connection();
  bool is_updated() const { return is_updated_; }
  bool is_use_ssl() const { return is_use_ssl_; }
  void disable_ssl() { is_use_ssl_ = false; }
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    databuff_printf(buf, buf_len, pos, "connection pool task");
    return pos;
  }
  int64_t get_server_count() const;

  virtual int escape(const char *from, const int64_t from_size,
      char *to, const int64_t to_size, int64_t &out_size);

  virtual int acquire(ObISQLConnection *&conn, ObISQLClient *client_addr, const int32_t group_id) override;
  virtual int release(ObISQLConnection *conn, const bool success) override;

  virtual int on_client_inactive(ObISQLClient *client_addr) override
  {
    UNUSED(client_addr);
    return OB_SUCCESS;
  }
  virtual ObSQLConnPoolType get_type() override { return MYSQL_POOL; }

  void set_mode(const ObMySQLConnection::Mode mode) { mode_ = mode; }
  ObMySQLConnection::Mode get_mode() const { return mode_; }
  void set_pool_type(const MySQLConnectionPoolType pool_type) { pool_type_ = pool_type; }
  MySQLConnectionPoolType get_pool_type() const { return pool_type_; }
  const char* get_db_name() const { return db_name_; }
  const char* get_user_name() const { return db_user_; }
protected:
  // update interval.
  // update ms list in backgroud thread and
  // recycle not-in-use unavaliable ms connections
  //virtual void run(int64_ts);
  virtual void runTimerTask();
  int create_server_connection_pool(const common::ObAddr &server);

  virtual int acquire(ObMySQLConnection *&connection);
  int do_acquire(ObMySQLConnection *&connection);


protected:
  int try_connect(ObMySQLConnection *connection);
  int release(ObMySQLConnection *connection, const bool succ);
  int get_pool(ObServerConnectionPool *&pool);
  int get_tenant_server_pool(ObTenantServerConnectionPool *&tenant_server_pool);
  int purge_connection_pool();
  void mark_all_server_connection_gone();
  int renew_server_connection_pool(common::ObAddr &server);
  int renew_tenant_server_pool_map();
private:
private:
  int renew_tenant_server_pool_();
  int get_server_pool_(const ObAddr &addr, ObServerConnectionPool *&pool);
protected:
  static const int MAX_SERVER_GONE_INTERVAL = 1000 * 1000 * 1; // 1 sec

  bool is_updated_;
  bool is_stop_;
  bool is_use_ssl_;
  ObMySQLConnection::Mode mode_;
  MySQLConnectionPoolType pool_type_;

  int tg_id_;
  ObMySQLServerProvider *server_provider_;
  volatile int64_t busy_conn_count_;

  // user name or password or db maybe modify, add user_info_lock_ to protect user info
  mutable obsys::ObRWLock user_info_lock_;
  char db_user_[OB_MAX_USER_NAME_BUF_LENGTH];
  char db_pass_[OB_MAX_PASSWORD_BUF_LENGTH];
  char db_name_[OB_MAX_DATABASE_NAME_BUF_LENGTH];
  char init_sql_[OB_MAX_SQL_LENGTH];
  ObConnPoolConfigParam config_;
  mutable obsys::ObRWLock get_lock_;
  // ObMySQLConnectionPool::do_acquire use obsys::ObRLockGuard lock(get_lock_)
  // will leading to dead lock
  common::ObArenaAllocator allocator_;
  ServerList server_list_;
  ObTenantServerConnectionPool tenant_server_pool_;
  common::ObCachedAllocator<ObServerConnectionPool> server_pool_;
  bool check_read_consistency_;
};

}
}
}

#endif // __COMMON_OB_MYSQL_CONNECTION__
