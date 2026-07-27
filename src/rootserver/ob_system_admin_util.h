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
class ObMySQLProxy;
class ObConfigManager;
}

namespace obcall
{
struct ObAdminSetConfigArg;
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
class ObLocalManagementService;
namespace config_error
{
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT_FOR_PRIMARY = "Cannot enable one phase commit while the primary cluster has standby cluster";
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT_FOR_STANDBY = "Cannot enable one phase commit on standby cluster";
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT_FOR_INVALID = "Cannot enable one phase commit on invalid cluster";
const static char * const NOT_ALLOW_ENABLE_ONE_PHASE_COMMIT = "enable_one_phase_commit not supported";
};

struct ObSystemAdminCtx
{
  ObSystemAdminCtx()
      : sql_proxy_(NULL),
      schema_service_(NULL),
      ddl_service_(NULL), config_mgr_(NULL),
      local_management_service_(NULL), inited_(false)
  {}

  bool is_inited() const { return inited_; }

  common::ObMySQLProxy *sql_proxy_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  ObDDLService *ddl_service_;
  common::ObConfigManager *config_mgr_;
  ObLocalManagementService *local_management_service_;
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

class ObAdminSetConfig : public ObSystemAdminUtil
{
public:
  static const uint64_t OB_PARAMETER_SEED_ID = UINT64_MAX;
  explicit ObAdminSetConfig(const ObSystemAdminCtx &ctx) : ObSystemAdminUtil(ctx) {}
  virtual ~ObAdminSetConfig() {}

  int execute(obcall::ObAdminSetConfigArg &arg);

private:
  class ObServerConfigChecker : public common::ObServerConfig
  {
  };

private:
  int verify_config(obcall::ObAdminSetConfigArg &arg);
  int update_config(obcall::ObAdminSetConfigArg &arg);
  int update_sys_config_(const obcall::ObAdminSetConfigItem &item);

private:
  DISALLOW_COPY_AND_ASSIGN(ObAdminSetConfig);
};

#define OB_INNER_JOB_DEF(JOB)                                \
    JOB(INVALID_INNER_JOB, = 0)                              \
    JOB(ROOT_INSPECTION,)                                    \
    JOB(IO_CALIBRATION,)                                      \
    JOB(MAX_INNER_JOB,)

DECLARE_ENUM(ObInnerJob, inner_job, OB_INNER_JOB_DEF);

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_SYSTEM_ADMIN_UTIL_H_
