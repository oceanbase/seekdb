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

#ifndef OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_STMT_
#define OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_STMT_

#include "sql/resolver/cmd/ob_system_cmd_stmt.h"
#include "share/ob_rpc_struct.h"
#include "share/scheduler/ob_sys_task_stat.h"
#include "share/table/ob_redis_importer.h"

namespace oceanbase
{
namespace sql
{
enum FreezeAllUserOrMeta {
  FREEZE_ALL = 0x01,
  FREEZE_ALL_USER = 0x02,
  FREEZE_ALL_META = 0x04
};

class ObFreezeStmt : public ObSystemCmdStmt
{
public:
  ObFreezeStmt()
    : ObSystemCmdStmt(stmt::T_FREEZE),
      major_freeze_(false),
      freeze_all_flag_(0),
      opt_server_list_(),
      opt_tenant_ids_(),
      opt_tablet_id_(),
      opt_ls_id_(share::ObLSID::INVALID_LS_ID),
      rebuild_column_group_(false) {}
  ObFreezeStmt(common::ObIAllocator *name_pool)
    : ObSystemCmdStmt(name_pool, stmt::T_FREEZE),
      major_freeze_(false),
      freeze_all_flag_(0),
      opt_server_list_(),
      opt_tenant_ids_(),
      opt_tablet_id_(),
      opt_ls_id_(share::ObLSID::INVALID_LS_ID),
      rebuild_column_group_(false) {}
  virtual ~ObFreezeStmt() {}

  bool is_major_freeze() const { return major_freeze_; }
  void set_major_freeze(bool major_freeze) { major_freeze_ = major_freeze; }
  bool is_freeze_all() const { return 0 != (freeze_all_flag_ & FREEZE_ALL); }
  void set_freeze_all() { freeze_all_flag_ |= FREEZE_ALL; }
  bool is_freeze_all_user() const { return 0 != (freeze_all_flag_ & FREEZE_ALL_USER); }
  void set_freeze_all_user() { freeze_all_flag_ |= FREEZE_ALL_USER; }
  bool is_freeze_all_meta() const { return 0 != (freeze_all_flag_ & FREEZE_ALL_META); }
  void set_freeze_all_meta() { freeze_all_flag_ |= FREEZE_ALL_META; }
  bool is_rebuild_column_group() const { return rebuild_column_group_; }
  void set_rebuild_column_group(bool rebuild_column_group) { rebuild_column_group_ = rebuild_column_group; }
  inline obcall::ObServerList &get_ignore_server_list() { return opt_server_list_; }
  inline obcall::ObServerList &get_server_list() { return opt_server_list_; }
  inline common::ObSArray<uint64_t> &get_tenant_ids() { return opt_tenant_ids_; }
  inline common::ObZone &get_zone() { return opt_zone_; }
  inline common::ObTabletID &get_tablet_id() { return opt_tablet_id_; }
  inline int64_t &get_ls_id() { return opt_ls_id_; }
  inline int push_server(const common::ObAddr& server) {
    return opt_server_list_.push_back(server);
  }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(major_freeze), K(freeze_all_flag_), 
               K(opt_server_list_), K(opt_tenant_ids_), K(opt_tablet_id_), K(opt_ls_id_));
private:
  bool major_freeze_;
  // for major_freeze, it is ignore server list
  // for minor_freeze, it is candidate server list
  int freeze_all_flag_;
  // for major_freeze only
  obcall::ObServerList opt_server_list_;
  // for minor_freeze only,
  common::ObSArray<uint64_t> opt_tenant_ids_;
  // for minor_freeze only
  common::ObZone opt_zone_;
  
  // for minor_freeze only
  common::ObTabletID opt_tablet_id_;
  int64_t opt_ls_id_;
  // for major_freeze only
  bool rebuild_column_group_;
};

class ObFlushCacheStmt : public ObSystemCmdStmt
{
public:
  ObFlushCacheStmt() :
    ObSystemCmdStmt(stmt::T_FLUSH_CACHE),
    flush_cache_arg_(),
    is_global_(false)
  {}
  virtual ~ObFlushCacheStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(flush_cache_arg));

  obcall::ObAdminFlushCacheArg flush_cache_arg_;
  bool is_global_;
};

class ObFlushKVCacheStmt : public ObSystemCmdStmt
{
public:
  ObFlushKVCacheStmt() : ObSystemCmdStmt(stmt::T_FLUSH_KVCACHE) {}
  virtual ~ObFlushKVCacheStmt() {}

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(tenant_name), K_(cache_name));
  common::ObFixedLengthString<common::OB_MAX_TENANT_NAME_LENGTH + 1> tenant_name_;
  common::ObFixedLengthString<common::OB_MAX_TENANT_NAME_LENGTH + 1> cache_name_;
};

class ObFlushIlogCacheStmt : public ObSystemCmdStmt
{
public:
  ObFlushIlogCacheStmt() : ObSystemCmdStmt(stmt::T_FLUSH_ILOGCACHE), file_id_(0) {}
  virtual ~ObFlushIlogCacheStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(file_id));

  int32_t file_id_;
};

class ObFlushSSMicroCacheStmt : public ObSystemCmdStmt
{
public:
  ObFlushSSMicroCacheStmt() : ObSystemCmdStmt(stmt::T_FLUSH_SS_MICRO_CACHE) {}
  virtual ~ObFlushSSMicroCacheStmt() {}

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(tenant_name));
  common::ObFixedLengthString<common::OB_MAX_TENANT_NAME_LENGTH + 1> tenant_name_;
};

class ObFlushDagWarningsStmt : public ObSystemCmdStmt
{
public:
  ObFlushDagWarningsStmt() : ObSystemCmdStmt(stmt::T_FLUSH_DAG_WARNINGS) {}
  virtual ~ObFlushDagWarningsStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_));
};

class ObAdminServerStmt : public ObSystemCmdStmt
{
public:
  ObAdminServerStmt()
      : ObSystemCmdStmt(stmt::T_ADMIN_SERVER), op_(obcall::ObAdminServerArg::ADD)
  {
  }

  ObAdminServerStmt(common::ObIAllocator *name_pool)
      : ObSystemCmdStmt(name_pool, stmt::T_ADMIN_SERVER)
  {
  }

  virtual ~ObAdminServerStmt() {}

  inline obcall::ObServerList &get_server_list() { return server_list_; }
  inline const common::ObZone &get_zone() const { return zone_; }
  inline void set_zone(const common::ObZone &zone) { zone_ = zone; }
  inline obcall::ObAdminServerArg::AdminServerOp get_op() const { return op_; }
  inline void set_op(const obcall::ObAdminServerArg::AdminServerOp op) { op_ = op; }
private:
  obcall::ObAdminServerArg::AdminServerOp op_;
  obcall::ObServerList server_list_;
  common::ObZone zone_;
};

class ObAdminMergeStmt: public ObSystemCmdStmt
{
public:
  ObAdminMergeStmt() : ObSystemCmdStmt(stmt::T_ADMIN_MERGE) {}
  virtual ~ObAdminMergeStmt() {}

  obcall::ObAdminMergeArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminMergeArg rpc_arg_;
};

class ObAdminRecoveryStmt: public ObSystemCmdStmt
{
public:
  ObAdminRecoveryStmt() : ObSystemCmdStmt(stmt::T_ADMIN_RECOVERY) {}
  virtual ~ObAdminRecoveryStmt() {}

  obcall::ObAdminRecoveryArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminRecoveryArg rpc_arg_;
};

class ObClearRoottableStmt : public ObSystemCmdStmt
{
public:
  ObClearRoottableStmt() : ObSystemCmdStmt(stmt::T_CLEAR_ROOT_TABLE) {}
  virtual ~ObClearRoottableStmt() {}

  obcall::ObAdminClearRoottableArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminClearRoottableArg rpc_arg_;
};

class ObRefreshSchemaStmt : public ObSystemCmdStmt
{
public:
  ObRefreshSchemaStmt() : ObSystemCmdStmt(stmt::T_REFRESH_SCHEMA) {}
  virtual ~ObRefreshSchemaStmt() {}

  obcall::ObAdminRefreshSchemaArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminRefreshSchemaArg rpc_arg_;
};

class ObRefreshMemStatStmt : public ObSystemCmdStmt
{
public:
  ObRefreshMemStatStmt() : ObSystemCmdStmt(stmt::T_REFRESH_MEMORY_STAT) {}
  virtual ~ObRefreshMemStatStmt() {}

  obcall::ObAdminRefreshMemStatArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminRefreshMemStatArg rpc_arg_;
};

class ObWashMemFragmentationStmt : public ObSystemCmdStmt
{
public:
  ObWashMemFragmentationStmt() : ObSystemCmdStmt(stmt::T_WASH_MEMORY_FRAGMENTATION) {}
  virtual ~ObWashMemFragmentationStmt() {}

  obcall::ObAdminWashMemFragmentationArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminWashMemFragmentationArg rpc_arg_;
};

class ObRefreshIOCalibraitonStmt : public ObSystemCmdStmt
{
public:
  ObRefreshIOCalibraitonStmt() : ObSystemCmdStmt(stmt::T_REFRESH_IO_CALIBRATION) {}
  virtual ~ObRefreshIOCalibraitonStmt() {}

  obcall::ObAdminRefreshIOCalibrationArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminRefreshIOCalibrationArg rpc_arg_;
};

class ObSetConfigStmt : public ObSystemCmdStmt
{
public:
  ObSetConfigStmt() : ObSystemCmdStmt(stmt::T_ALTER_SYSTEM_SET_PARAMETER) {}
  virtual ~ObSetConfigStmt() {}

  obcall::ObAdminSetConfigArg &get_rpc_arg() { return rpc_arg_; }
  
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminSetConfigArg rpc_arg_;
};

class ObChangeExternalStorageDestStmt : public ObSystemCmdStmt
{
public:
  ObChangeExternalStorageDestStmt() : ObSystemCmdStmt(stmt::T_CHANGE_EXTERNAL_STORAGE_DEST) {}
  virtual ~ObChangeExternalStorageDestStmt() {}

  obcall::ObAdminSetConfigArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminSetConfigArg rpc_arg_;
};

class ObSetTPStmt : public ObSystemCmdStmt
{
public:
  ObSetTPStmt() : ObSystemCmdStmt(stmt::T_ALTER_SYSTEM_SETTP) {}
  virtual ~ObSetTPStmt() {}

  obcall::ObAdminSetTPArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminSetTPArg rpc_arg_;
};

class ObClearMergeErrorStmt : public ObSystemCmdStmt
{
public:
  ObClearMergeErrorStmt() : ObSystemCmdStmt(stmt::T_CLEAR_MERGE_ERROR) {}
  virtual ~ObClearMergeErrorStmt() {}

  obcall::ObAdminMergeArg &get_rpc_arg() { return rpc_arg_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminMergeArg rpc_arg_;
};

class ObUpgradeVirtualSchemaStmt : public ObSystemCmdStmt
{
public:
  ObUpgradeVirtualSchemaStmt() : ObSystemCmdStmt(stmt::T_UPGRADE_VIRTUAL_SCHEMA) {}
  virtual ~ObUpgradeVirtualSchemaStmt() {}
};

class ObAdminUpgradeCmdStmt : public ObSystemCmdStmt
{
public:
  enum AdminUpgradeOp
  {
    BEGIN = 1,
    END = 2,
  };
  ObAdminUpgradeCmdStmt() : ObSystemCmdStmt(stmt::T_ADMIN_UPGRADE_CMD), op_(BEGIN) {}
  virtual ~ObAdminUpgradeCmdStmt() {}

  inline const AdminUpgradeOp &get_op() const { return op_; }
  inline void set_op(const AdminUpgradeOp op) { op_ = op; }
private:
  AdminUpgradeOp op_;
};

class ObAdminRollingUpgradeCmdStmt : public ObSystemCmdStmt
{
public:
  enum AdminUpgradeOp
  {
    BEGIN = 1,
    END = 2,
  };
  ObAdminRollingUpgradeCmdStmt() : ObSystemCmdStmt(stmt::T_ADMIN_ROLLING_UPGRADE_CMD), op_(BEGIN) {}
  virtual ~ObAdminRollingUpgradeCmdStmt() {}

  inline const AdminUpgradeOp &get_op() const { return op_; }
  inline void set_op(const AdminUpgradeOp op) { op_ = op; }
private:
  AdminUpgradeOp op_;
};

class ObRunUpgradeJobStmt : public ObSystemCmdStmt
{
public:
  ObRunUpgradeJobStmt() : ObSystemCmdStmt(stmt::T_ADMIN_RUN_UPGRADE_JOB) {}
  virtual ~ObRunUpgradeJobStmt() {}

  obcall::ObUpgradeJobArg &get_rpc_arg() { return rpc_arg_; }
private:
  obcall::ObUpgradeJobArg rpc_arg_;
};

class ObStopUpgradeJobStmt : public ObSystemCmdStmt
{
public:
  ObStopUpgradeJobStmt() : ObSystemCmdStmt(stmt::T_ADMIN_STOP_UPGRADE_JOB) {}
  virtual ~ObStopUpgradeJobStmt() {}

  obcall::ObUpgradeJobArg &get_rpc_arg() { return rpc_arg_; }
private:
  obcall::ObUpgradeJobArg rpc_arg_;
};

class ObCancelTaskStmt : public ObSystemCmdStmt
{
public:
  ObCancelTaskStmt()
    : ObSystemCmdStmt(stmt::T_CANCEL_TASK),
      task_type_(share::MAX_SYS_TASK_TYPE),
      task_id_()
  {
  }
  virtual ~ObCancelTaskStmt() {}
  const share::ObSysTaskType &get_task_type() { return task_type_; }
  const common::ObString &get_task_id() { return task_id_; }
  int set_param(const share::ObSysTaskType &task_type, const common::ObString &task_id)
  {
    int ret = common::OB_SUCCESS;

    if (task_type < 0 || task_type> share::MAX_SYS_TASK_TYPE || task_id.length() <= 0) {
      ret = common::OB_INVALID_ARGUMENT;
    } else {
      task_type_ = task_type;
      task_id_ = task_id;
    }

    return ret;
  }

private:
  share::ObSysTaskType task_type_;
  common::ObString task_id_;
};

class ObSetDiskValidStmt : public ObSystemCmdStmt
{
public:
  ObSetDiskValidStmt():
    ObSystemCmdStmt(stmt::T_SET_DISK_VALID),
    server_()
  {}
  virtual ~ObSetDiskValidStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(server));

  common::ObAddr server_;
};

class ObAddDiskStmt : public ObSystemCmdStmt
{
public:
  ObAddDiskStmt():
    ObSystemCmdStmt(stmt::T_ALTER_DISKGROUP_ADD_DISK)
  {}
  virtual ~ObAddDiskStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(arg));

  obcall::ObAdminAddDiskArg arg_;
};

class ObDropDiskStmt : public ObSystemCmdStmt
{
public:
  ObDropDiskStmt():
    ObSystemCmdStmt(stmt::T_ALTER_DISKGROUP_DROP_DISK)
  {}
  virtual ~ObDropDiskStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(arg));

  obcall::ObAdminDropDiskArg arg_;
};

class ObEnableSqlThrottleStmt
    : public ObSystemCmdStmt
{
public:
  ObEnableSqlThrottleStmt()
      : ObSystemCmdStmt(stmt::T_ENABLE_SQL_THROTTLE),
        priority_(99),
        rt_(-1.),
        io_(-1),
        network_(-1.),
        cpu_(-1.),
        logical_reads_(-1),
        queue_time_(-1.)
  {}
  void set_priority(int64_t priority) { priority_ = priority; }
  void set_rt(double rt) { rt_ = rt; }
  void set_io(int64_t io) { io_ = io; }
  void set_network(double network) { network_ = network; }
  void set_cpu(double cpu) { cpu_ = cpu; }
  void set_logical_reads(int64_t logical_reads) { logical_reads_ = logical_reads; }
  void set_queue_time(double queue_time) { queue_time_ = queue_time; }

  int64_t get_priority() const { return priority_; }
  double get_rt() const { return rt_; }
  int64_t get_io() const { return io_; }
  double get_network() const { return network_; }
  double get_cpu() const { return cpu_; }
  int64_t get_logical_reads() const { return logical_reads_; }
  double get_queue_time() const { return queue_time_; }

  TO_STRING_KV(
      N_STMT_TYPE, ((int)stmt_type_),
      K_(priority),
      K_(rt),
      K_(io),
      K_(network),
      K_(cpu),
      K_(logical_reads),
      K_(queue_time));

private:
  int64_t priority_;
  double rt_;
  int64_t io_;
  double network_;
  double cpu_;
  int64_t logical_reads_;
  double queue_time_;
};

class ObDisableSqlThrottleStmt
  : public ObSystemCmdStmt
{
public:
  ObDisableSqlThrottleStmt()
    : ObSystemCmdStmt(stmt::T_DISABLE_SQL_THROTTLE)
    {}
};

class ObCancelRestoreStmt : public ObSystemCmdStmt
{
public:
  ObCancelRestoreStmt()
    : ObSystemCmdStmt(stmt::T_CANCEL_RESTORE),
      drop_tenant_arg_() {}
  virtual ~ObCancelRestoreStmt() {}
  obcall::ObDropTenantArg &get_drop_tenant_arg() { return drop_tenant_arg_; }
	TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(drop_tenant_arg));
private:
  obcall::ObDropTenantArg drop_tenant_arg_;
};

class ObTableTTLStmt : public ObSystemCmdStmt {
public:
  ObTableTTLStmt()
    : ObSystemCmdStmt(stmt::T_TABLE_TTL),
      type_(obcall::ObTTLRequestArg::TTL_INVALID_TYPE),
      opt_tenant_ids_(),
      ttl_all_(false)
  {}
  virtual ~ObTableTTLStmt()
  {}

  obcall::ObTTLRequestArg::TTLRequestType get_type() const
  {
    return type_;
  }
  int set_type(const int64_t type)
  {
    int ret = common::OB_SUCCESS;

    if (type < 0 || type >= obcall::ObTTLRequestArg::TTL_MOVE_TYPE) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "invalid args", K(type));
    } else {
      type_ = static_cast<obcall::ObTTLRequestArg::TTLRequestType>(type);
    }

    return ret;
  }
  inline common::ObSArray<uint64_t> &get_tenant_ids() { return opt_tenant_ids_; }
  bool is_ttl_all() const { return ttl_all_; }
  void set_ttl_all(bool ttl_all) { ttl_all_ = ttl_all; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(tenant_id), K_(type),
               K_(opt_tenant_ids), K_(ttl_all));

private:
  uint64_t tenant_id_;
  obcall::ObTTLRequestArg::TTLRequestType type_;
  common::ObSArray<uint64_t> opt_tenant_ids_;
  bool ttl_all_;
};
class ObCheckpointSlogStmt : public ObSystemCmdStmt
{
public:
  ObCheckpointSlogStmt()
    : ObSystemCmdStmt(stmt::T_CHECKPOINT_SLOG),
      tenant_id_(common::OB_INVALID_TENANT_ID),
      server_()
  {}
  virtual ~ObCheckpointSlogStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(tenant_id), K_(server));

  uint64_t tenant_id_;
  common::ObAddr server_;
};

class ObRecoverTableStmt : public ObSystemCmdStmt
{
public:
  ObRecoverTableStmt()
    : ObSystemCmdStmt(stmt::T_RECOVER_TABLE), rpc_arg_() {}
  virtual ~ObRecoverTableStmt() {}
  obcall::ObRecoverTableArg &get_rpc_arg() { return rpc_arg_; }
private:
  obcall::ObRecoverTableArg rpc_arg_;
};



class ObResetConfigStmt : public ObSystemCmdStmt
{
public:
  ObResetConfigStmt() : ObSystemCmdStmt(stmt::T_ALTER_SYSTEM_RESET_PARAMETER) {}
  virtual ~ObResetConfigStmt() {}
  obcall::ObAdminSetConfigArg &get_rpc_arg() { return rpc_arg_; }
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(rpc_arg));
private:
  obcall::ObAdminSetConfigArg rpc_arg_;
};

class ObModuleDataStmt : public ObSystemCmdStmt
{
public:
  ObModuleDataStmt() : ObSystemCmdStmt(stmt::T_MODULE_DATA), arg_() {}
  virtual ~ObModuleDataStmt() {}

  OB_INLINE table::ObModuleDataArg &get_arg() { return arg_; }
  OB_INLINE const table::ObModuleDataArg &get_arg() const { return arg_; }
  
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(arg));
private:
  table::ObModuleDataArg arg_;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_STMT_
