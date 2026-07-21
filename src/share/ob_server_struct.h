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
#include "share/ob_lease_struct.h"
#include "lib/net/ob_addr.h"
#include "share/ob_cluster_role.h"              // ObClusterRole
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
class ObITabletScan;
class ObMysqlRandom;
} // end of namespace common

namespace rootserver
{
class ObRootService;
class ObInZoneMaster;
} // end of namespace rootserver

namespace sql
{
class ObSQLSessionMgr;
class ObSql;
class ObExecutorRpcImpl;
class ObDataAccessService;
class ObConnectResourceMgr;
} // end of namespace sql

namespace pl
{
class ObPL;
}

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

namespace omt
{
class ObMultiTenant;
}

namespace logservice
{
class ObServerLogBlockMgr;
}

namespace observer
{
class ObService;
class ObVTIterCreator;
class ObSrvNetworkFrame;
class ObIDiskReport;
class ObResourceInnerSQLConnectionPool;
class ObStartupAccelTaskHandler;
} // end of namespace observer


namespace plugin
{
class ObPluginMgr;
}

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
  rootserver::ObRootService *root_service_;
  rootserver::ObInZoneMaster *in_zone_master_;
  observer::ObService *ob_service_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  common::ObServerConfig *config_;
  common::ObConfigManager *config_mgr_;
  share::ObTabletTableOperator *tablet_operator_;
  share::ObSQLiteConnectionPool *meta_db_pool_;
  sql::ObExecutorRpcImpl *executor_rpc_;
  common::ObMySQLProxy *sql_proxy_;
  common::ObMySQLProxy *ddl_sql_proxy_;
  observer::ObResourceInnerSQLConnectionPool *res_inner_conn_pool_;
  common::ObInOutBandwidthThrottle *bandwidth_throttle_;
  common::ObITabletScan *vt_par_ser_;
  sql::ObSQLSessionMgr *session_mgr_;
  sql::ObSql *sql_engine_;
  pl::ObPL *pl_engine_;
  omt::ObMultiTenant *omt_;
  observer::ObVTIterCreator *vt_iter_creator_;
  int64_t start_time_;
  int64_t *warm_up_start_time_;
  ObServiceStatus status_;
  ObServerMode startup_mode_;
  share::RSServerStatus rs_server_status_;
  int64_t start_service_time_;
  obmysql::ObDiag *diag_;
  common::ObMysqlRandom *scramble_rand_;
  observer::ObSrvNetworkFrame *net_frame_;

  observer::ObIDiskReport *disk_reporter_;
  logservice::ObServerLogBlockMgr *log_block_mgr_;

  bool inited_;
  share::ObSchemaStatusProxy *schema_status_proxy_;
  int64_t ssl_key_expired_time_;
  sql::ObConnectResourceMgr* conn_res_mgr_;

  observer::ObStartupAccelTaskHandler* startup_accel_handler_;
  bool in_bootstrap_;
  bool sys_package_ready_;
  plugin::ObPluginMgr *plugin_mgr_ = nullptr;

  // Primary-Standby configuration
  common::ObClusterRole server_role_;
  
  static ObGlobalContext& get_instance();
  void init();
  bool is_inited() const { return inited_; }
  bool is_embedded_mode() const { return embedded_; }
  void set_embedded_mode(const bool embedded) { embedded_ = embedded; }
  bool is_standby_cluster() const { return common::STANDBY_CLUSTER == server_role_; }
  // Refer to the high availability zone design document
  // 
  /*
  Returns a globally unique, monotonically increasing server ID.
  This ID is unique across the lifetime of the cluster and will not be reused.
  */
  inline uint64_t get_server_id() const { return ATOMIC_LOAD(&server_id_); }
  inline void set_server_id(const uint64_t id) { ATOMIC_SET(&server_id_, id); }
  /*
  Returns a currently unique server index within the cluster.
  This index is unique among current servers in the cluster, but may be reused if a server is removed from the cluster.

  When server ID has size limitation, like only 12 bits are allocated for server ID in session_id
  to ensure its uniqueness, which implies the server ID cannot be greater than or equal to 4096,
  we should consider using server index instead of server ID.
  However, using server index requires a guarantee that no remnants of an old server remain after it is deleted.
  For example, no sessions from the deleted server should exist anymore. If remnants persist after server deletion,
  using server index is not permitted, otherwise, correctness issues may arise.
  In such cases, you need to carefully consider how to resolve this problem by yourself.
  */
  uint64_t get_server_index() const;
  void set_upgrade_stage(obcall::ObUpgradeStage upgrade_stage) { upgrade_stage_ = upgrade_stage; }
  obcall::ObUpgradeStage get_upgrade_stage() { return upgrade_stage_; }
  DECLARE_TO_STRING;
  // instead of self_addr_
  const ObAddr &self_addr() const { return self_addr_seq_.get_addr(); }
  const int64_t &self_seq() const { return self_addr_seq_.get_seq(); }
  bool is_shared_storage_mode() const
  {
    return false;
  }
private:
  ObGlobalContext() { MEMSET(this, 0, sizeof(*this)); init(); }
  ObGlobalContext(const ObGlobalContext &other);
  volatile int64_t server_status_;
  bool has_start_service() const { return 0 < start_service_time_; }

  obcall::ObUpgradeStage upgrade_stage_;
  uint64_t server_id_;
  bool embedded_;
};

} // end of namespace share
} // end of namespace oceanbase

#define GCTX (::oceanbase::share::ObGlobalContext::get_instance())

#endif /* _OCEABASE_SHARE_OB_SERVER_STRUCT_H_ */
