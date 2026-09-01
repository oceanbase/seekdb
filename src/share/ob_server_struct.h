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

#ifndef _OCEABASE_SHARE_OB_SERVER_STRUCT_H_
#define _OCEABASE_SHARE_OB_SERVER_STRUCT_H_

// DON'T INCLUDE ANY OCEANBASE HEADER EXCEPT FROM LIB DIRECTORY
#include "lib/atomic/ob_atomic.h"
#include "share/ob_lease_struct.h"
#include "lib/net/ob_addr.h"
#include "share/ob_server_role.h"
#include "share/ob_rpc_struct.h"
#include "share/ob_server_status.h"
#include "share/config/ob_config_manager.h"

namespace oceanbase
{
namespace common
{
class ObServerConfig;
class ObConfigManager;
class ObMySQLProxy;
class ObCommonSqlProxy;
class ObTimer;
class ObMysqlRandom;
} // end of namespace common

namespace storage
{
class ObPtfMgr;
}

namespace transaction
{
}

namespace obmysql
{
class ObDiag;
} // end of namespace obmysql

namespace share
{
class ObTabletTableOperator;
class ObSQLiteConnectionPool;
class ObRsMgr;
class ObSchemaStatusProxy;

namespace schema
{
class ObMultiVersionSchemaService;
} // end of namespace schema

struct ObGlobalContext
{
  common::ObAddrWithSeq self_addr_seq_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  common::ObServerConfig *config_;
  common::ObConfigManager *config_mgr_;
  share::ObTabletTableOperator *tablet_operator_;
  share::ObSQLiteConnectionPool *meta_db_pool_;
  common::ObMySQLProxy *sql_proxy_;
  common::ObMySQLProxy *ddl_sql_proxy_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  int64_t start_time_;
  int64_t *warm_up_start_time_;
  ObServiceStatus status_;
  share::RSServerStatus rs_server_status_;
  int64_t start_service_time_;
  obmysql::ObDiag *diag_;
  common::ObMysqlRandom *scramble_rand_;
  bool inited_;
  share::ObSchemaStatusProxy *schema_status_proxy_;
  int64_t ssl_key_expired_time_;
  bool in_bootstrap_;
  bool sys_package_ready_;
  // Process-wide primary/standby mode.
  share::ObServerRole::Role server_role_;
  
  static ObGlobalContext& get_instance();
  void init();
  bool is_inited() const { return inited_; }
  bool is_embedded_mode() const { return embedded_; }
  void set_embedded_mode(const bool embedded) { embedded_ = embedded; }
  int64_t get_effective_mysql_port() const
  {
    return ATOMIC_LOAD(&effective_mysql_port_);
  }
  void set_effective_mysql_port(const int64_t port)
  {
    ATOMIC_STORE(&effective_mysql_port_, port);
  }
  DECLARE_TO_STRING;
  // instead of self_addr_
  const ObAddr &self_addr() const { return self_addr_seq_.get_addr(); }
  const int64_t &self_seq() const { return self_addr_seq_.get_seq(); }
private:
  ObGlobalContext() { MEMSET(this, 0, sizeof(*this)); init(); }
  ObGlobalContext(const ObGlobalContext &other);
  volatile int64_t server_status_;
  bool has_start_service() const { return 0 < start_service_time_; }

  int64_t effective_mysql_port_;
  bool embedded_;
};

} // end of namespace share
} // end of namespace oceanbase

#define GCTX (::oceanbase::share::ObGlobalContext::get_instance())

#endif /* _OCEABASE_SHARE_OB_SERVER_STRUCT_H_ */
