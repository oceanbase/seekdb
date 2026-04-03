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

#ifndef OCEANBASE_ROOTSERVER_OB_SYSTEM_ADMIN_UTIL_H_
#define OCEANBASE_ROOTSERVER_OB_SYSTEM_ADMIN_UTIL_H_

#include <stdlib.h>
#include "lib/hash/ob_hashset.h"
#include "lib/utility/ob_macro_utils.h"
#include "common/ob_role.h"
#include "share/config/ob_server_config.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"

// system admin command (alter system ...) execute

namespace oceanbase
{
namespace common
{
class ObAddr;
class ObMySQLProxy;
class ObConfigManager;
}

namespace obrpc
{
class ObSrvRpcProxy;
class ObCommonRpcProxy;
struct ObAdminChangeReplicaArg;
struct ObAdminMigrateReplicaArg;
struct ObServerZoneArg;
struct ObAdminMergeArg;
struct ObAdminClearRoottableArg;
struct ObAdminRefreshSchemaArg;
struct ObAdminSetConfigArg;
struct ObAdminFlushCacheArg;
struct ObFlushCacheArg;
struct Bool;
}

namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObTableSchema;
class ObSchemaGetterGuard;
}
}

namespace rootserver
{
class ObDDLService;
class ObRootService;
class ObSchemaSplitExecutor;
class ObCreateInnerSchemaExecutor;
class ObRsStatus;
class ObRsGtsManager;
namespace config_error
{
const static char * const INVALID_DISK_WATERLEVEL = "cannot specify disk waterlevel to zero when tenant groups matrix is specified";
const static char * const NOT_ALLOW_MOIDFY_CONFIG_WITHOUT_UPGRADE = "cannot moidfy enable_major_freeze/enable_ddl while enable_upgrade_mode is off";
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT_FOR_PRIMARY = "Cannot enable one phase commit while the primary cluster has standby cluster";
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT_FOR_STANDBY = "Cannot enable one phase commit on standby cluster";
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT_FOR_INVALID = "Cannot enable one phase commit on invalid cluster";
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT = "enable_one_phase_commit not supported";
};

struct ObSystemAdminCtx
{
  ObSystemAdminCtx()
      : rs_status_(NULL), rpc_proxy_(NULL), sql_proxy_(NULL),
      schema_service_(NULL),
      ddl_service_(NULL), config_mgr_(NULL),
      root_service_(NULL),
      create_inner_schema_executor_(nullptr), inited_(false)
  {}

  bool is_inited() const { return inited_; }

  ObRsStatus *rs_status_;
  obrpc::ObSrvRpcProxy *rpc_proxy_;
  common::ObMySQLProxy *sql_proxy_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  ObDDLService *ddl_service_;
  common::ObConfigManager *config_mgr_;
  ObRootService *root_service_;
  ObCreateInnerSchemaExecutor *create_inner_schema_executor_;
  bool inited_;
};

class ObSystemAdminUtil
{
public:
  const static int64_t WAIT_LEADER_SWITCH_TIMEOUT_US = 10 * 1000 * 1000; // 10s
  const static int64_t WAIT_LEADER_SWITCH_INTERVAL_US = 300 * 1000; // 300ms

  explicit ObSystemAdminUtil(const ObSystemAdminCtx &ctx) : ctx_(ctx) {}
  virtual ~ObSystemAdminUtil() {}

protected:
    const ObSystemAdminCtx &ctx_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObSystemAdminUtil);
};

class ObAdminCallServer : public ObSystemAdminUtil
{
public:
  explicit ObAdminCallServer(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminCallServer() {}

  int get_server_list(const obrpc::ObServerZoneArg &arg, ObIArray<ObAddr> &server_list);
  int call_all(const obrpc::ObServerZoneArg &arg);

  virtual int call_server(const common::ObAddr &server) = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminCallServer);
};

class ObAdminRefreshMemStat : public ObAdminCallServer
{
public:
  explicit ObAdminRefreshMemStat(const ObSystemAdminCtx &ctx) : ObAdminCallServer(ctx) {}
  virtual ~ObAdminRefreshMemStat() {}

  int execute(const obrpc::ObAdminRefreshMemStatArg &arg);
  virtual int call_server(const common::ObAddr &server);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminRefreshMemStat);
};

class ObAdminWashMemFragmentation : public ObAdminCallServer
{
public:
  explicit ObAdminWashMemFragmentation(const ObSystemAdminCtx &ctx) : ObAdminCallServer(ctx) {}
  virtual ~ObAdminWashMemFragmentation() {}

  int execute(const obrpc::ObAdminWashMemFragmentationArg &arg);
  virtual int call_server(const common::ObAddr &server);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminWashMemFragmentation);
};

class ObAdminClearMergeError: public ObSystemAdminUtil
{
public:
  explicit ObAdminClearMergeError(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminClearMergeError() {}

  int execute(const obrpc::ObAdminMergeArg &arg);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminClearMergeError);
};

class ObAdminMerge : public ObSystemAdminUtil
{
public:
  explicit ObAdminMerge(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminMerge() {}

  int execute(const obrpc::ObAdminMergeArg &arg);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminMerge);
};

class ObAdminClearRoottable: public ObSystemAdminUtil
{
public:
  explicit ObAdminClearRoottable(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminClearRoottable() {}

  int execute(const obrpc::ObAdminClearRoottableArg &arg);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminClearRoottable);
};

class ObAdminRefreshSchema: public ObAdminCallServer
{
public:
  explicit ObAdminRefreshSchema(const ObSystemAdminCtx &ctx)
      : ObAdminCallServer(ctx), schema_version_(0), schema_info_() {}
  virtual ~ObAdminRefreshSchema() {}

  int execute(const obrpc::ObAdminRefreshSchemaArg &arg);

  virtual int call_server(const common::ObAddr &server);
private:
  int64_t schema_version_;
  share::schema::ObRefreshSchemaInfo schema_info_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminRefreshSchema);
};

class ObAdminSetConfig : public ObSystemAdminUtil
{
public:
  static const uint64_t OB_PARAMETER_SEED_ID = UINT64_MAX;
  explicit ObAdminSetConfig(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminSetConfig() {}

  int execute(obrpc::ObAdminSetConfigArg &arg);

private:
  class ObServerConfigChecker : public common::ObServerConfig
  {
  };

private:
  int verify_config(obrpc::ObAdminSetConfigArg &arg);
  int update_config(obrpc::ObAdminSetConfigArg &arg);
  int update_sys_config_(const obrpc::ObAdminSetConfigItem &item);

private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminSetConfig);
};

class ObAdminUpgradeCmd : public ObSystemAdminUtil
{
public:
  explicit ObAdminUpgradeCmd(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminUpgradeCmd() {}

  int execute(const obrpc::Bool &upgrade);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminUpgradeCmd);
};

class ObAdminRollingUpgradeCmd : public ObSystemAdminUtil
{
public:
  explicit ObAdminRollingUpgradeCmd(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminRollingUpgradeCmd() {}

  int execute(const obrpc::ObAdminRollingUpgradeArg &arg);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminRollingUpgradeCmd);
};

#define OB_INNER_JOB_DEF(JOB)                                \
    JOB(INVALID_INNER_JOB, = 0)                              \
    JOB(ROOT_INSPECTION,)                                    \
    JOB(UPGRADE_STORAGE_FORMAT_VERSION,)                     \
    JOB(STOP_UPGRADE_STORAGE_FORMAT_VERSION,)                \
    JOB(CREATE_INNER_SCHEMA,)                                \
    JOB(IO_CALIBRATION,)                                     \
    JOB(MAX_INNER_JOB,)

DECLARE_ENUM(ObInnerJob, inner_job, OB_INNER_JOB_DEF);

class ObAdminRefreshIOCalibration : public ObAdminCallServer
{
public:
  explicit ObAdminRefreshIOCalibration(const ObSystemAdminCtx &ctx)
    : ObAdminCallServer(ctx) {}
  virtual ~ObAdminRefreshIOCalibration() {}

  int execute(const obrpc::ObAdminRefreshIOCalibrationArg &arg);
  int call_server(const common::ObAddr &server);

private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminRefreshIOCalibration);
};

class ObTenantServerAdminUtil : public ObSystemAdminUtil
{
public:
  explicit ObTenantServerAdminUtil(const ObSystemAdminCtx &ctx)
            : ObSystemAdminUtil(ctx)
  {}

  int get_all_servers(common::ObIArray<ObAddr> &servers);
  int get_tenant_servers(const uint64_t tenant_id, common::ObIArray<ObAddr> &servers);

private:
  DISALLOW_COPY_AND_ASSIGN(ObTenantServerAdminUtil);
};

class ObAdminFlushCache : public ObTenantServerAdminUtil
{
public:
  explicit ObAdminFlushCache(const ObSystemAdminCtx &ctx)
    : ObTenantServerAdminUtil(ctx)
  {}
  virtual ~ObAdminFlushCache() {}

  int call_server(const common::ObAddr &addr, const obrpc::ObFlushCacheArg &arg);

  int execute(const obrpc::ObAdminFlushCacheArg &arg);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminFlushCache);
};

class ObAdminSetTP : public ObAdminCallServer
{
public:
  explicit ObAdminSetTP(const ObSystemAdminCtx &ctx,
                        obrpc::ObAdminSetTPArg arg)
     : ObAdminCallServer(ctx),
       arg_(arg)
       {}
  virtual ~ObAdminSetTP() {}

  int execute(const obrpc::ObAdminSetTPArg &arg);
  virtual int call_server(const common::ObAddr &server);
private:
  obrpc::ObAdminSetTPArg arg_;
  DISALLOW_COPY_AND_ASSIGN(ObAdminSetTP);
};

class ObAdminSyncRewriteRules : public ObTenantServerAdminUtil
{
public:
  explicit ObAdminSyncRewriteRules(const ObSystemAdminCtx &ctx)
    : ObTenantServerAdminUtil(ctx)
  {}
  virtual ~ObAdminSyncRewriteRules() {}

  int call_server(const common::ObAddr &server,
                  const obrpc::ObSyncRewriteRuleArg &arg);

  int execute(const obrpc::ObSyncRewriteRuleArg &arg);

private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminSyncRewriteRules);
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_SYSTEM_ADMIN_UTIL_H_
