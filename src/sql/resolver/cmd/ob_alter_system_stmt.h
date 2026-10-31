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
#include "share/io/ob_io_calibration.h"
#include "data_plane/scheduler/ob_sys_task_stat.h"
namespace oceanbase
{
namespace sql
{
struct ObFlushCacheParam
{
  ObFlushCacheParam()
    : cache_type_(CACHE_TYPE_INVALID),
      db_ids_(),
      sql_id_(),
      is_fine_grained_(false),
      ns_type_(ObLibCacheNameSpace::NS_INVALID),
      schema_id_(common::OB_INVALID_ID)
  {}

  int push_database(const uint64_t db_id) { return db_ids_.push_back(db_id); }
  TO_STRING_KV(K_(cache_type), K_(db_ids), K_(sql_id), K_(is_fine_grained),
               K_(ns_type), K_(schema_id));

  ObCacheType cache_type_;
  common::ObSEArray<uint64_t, 8> db_ids_;
  common::ObString sql_id_;
  bool is_fine_grained_;
  ObLibCacheNameSpace ns_type_;
  uint64_t schema_id_;
};

struct ObRefreshIOCalibrationParam
{
  ObRefreshIOCalibrationParam()
    : storage_name_(), only_refresh_(false), calibration_list_()
  {}

  bool is_valid() const
  {
    return !(only_refresh_ && calibration_list_.count() > 0);
  }

  TO_STRING_KV(K_(storage_name), K_(only_refresh), K_(calibration_list));

  common::ObString storage_name_;
  bool only_refresh_;
  common::ObSArray<common::ObIOBenchResult> calibration_list_;
};

class ObFreezeStmt : public ObSystemCmdStmt
{
public:
  ObFreezeStmt()
    : ObSystemCmdStmt(stmt::T_FREEZE),
      major_freeze_(false),
      opt_tablet_id_() {}
  ObFreezeStmt(common::ObIAllocator *name_pool)
    : ObSystemCmdStmt(name_pool, stmt::T_FREEZE),
      major_freeze_(false),
      opt_tablet_id_() {}
  virtual ~ObFreezeStmt() {}

  bool is_major_freeze() const { return major_freeze_; }
  void set_major_freeze(bool major_freeze) { major_freeze_ = major_freeze; }
  inline common::ObTabletID &get_tablet_id() { return opt_tablet_id_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(major_freeze), K(opt_tablet_id_));
private:
  bool major_freeze_;
  common::ObTabletID opt_tablet_id_;
};

class ObFlushCacheStmt : public ObSystemCmdStmt
{
public:
  ObFlushCacheStmt() :
    ObSystemCmdStmt(stmt::T_FLUSH_CACHE),
    flush_cache_arg_()
  {}
  virtual ~ObFlushCacheStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(flush_cache_arg));

  ObFlushCacheParam flush_cache_arg_;
};

class ObFlushKVCacheStmt : public ObSystemCmdStmt
{
public:
  ObFlushKVCacheStmt() : ObSystemCmdStmt(stmt::T_FLUSH_KVCACHE) {}
  virtual ~ObFlushKVCacheStmt() {}

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(cache_name));
  common::ObFixedLengthString<common::OB_MAX_RUNTIME_NAME_LENGTH + 1> cache_name_;
};

class ObFlushIlogCacheStmt : public ObSystemCmdStmt
{
public:
  ObFlushIlogCacheStmt() : ObSystemCmdStmt(stmt::T_FLUSH_ILOGCACHE), file_id_(0) {}
  virtual ~ObFlushIlogCacheStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(file_id));

  int32_t file_id_;
};

class ObFlushDagWarningsStmt : public ObSystemCmdStmt
{
public:
  ObFlushDagWarningsStmt() : ObSystemCmdStmt(stmt::T_FLUSH_DAG_WARNINGS) {}
  virtual ~ObFlushDagWarningsStmt() {}
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_));
};

class ObAdminMergeStmt: public ObSystemCmdStmt
{
public:
  enum class MergeType
  {
    INVALID,
    SUSPEND,
    RESUME,
  };

  ObAdminMergeStmt()
    : ObSystemCmdStmt(stmt::T_ADMIN_MERGE), merge_type_(MergeType::INVALID)
  {}
  virtual ~ObAdminMergeStmt() {}

  MergeType get_merge_type() const { return merge_type_; }
  void set_merge_type(const MergeType merge_type) { merge_type_ = merge_type; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(merge_type));
private:
  MergeType merge_type_;
};

class ObRefreshIOCalibraitonStmt : public ObSystemCmdStmt
{
public:
  ObRefreshIOCalibraitonStmt() : ObSystemCmdStmt(stmt::T_REFRESH_IO_CALIBRATION) {}
  virtual ~ObRefreshIOCalibraitonStmt() {}

  ObRefreshIOCalibrationParam &get_param() { return param_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(param));
private:
  ObRefreshIOCalibrationParam param_;
};

class ObSwitchRoleStmt : public ObSystemCmdStmt
{
public:
  explicit ObSwitchRoleStmt(stmt::StmtType stmt_type = stmt::T_NONE)
    : ObSystemCmdStmt(stmt_type), is_verify_(false)
  {}
  virtual ~ObSwitchRoleStmt() {}
  void set_verify(const bool is_verify) { is_verify_ = is_verify; }
  bool is_verify() const { return is_verify_; }
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(is_verify));
private:
  bool is_verify_;
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

class ObSetTPStmt : public ObSystemCmdStmt
{
public:
  ObSetTPStmt() : ObSystemCmdStmt(stmt::T_ALTER_SYSTEM_SETTP) {}
  virtual ~ObSetTPStmt() {}

  obcall::ObSetTracepointParam &get_param() { return param_; }

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_), K_(param));
private:
  obcall::ObSetTracepointParam param_;
};

class ObClearMergeErrorStmt : public ObSystemCmdStmt
{
public:
  ObClearMergeErrorStmt() : ObSystemCmdStmt(stmt::T_CLEAR_MERGE_ERROR) {}
  virtual ~ObClearMergeErrorStmt() {}

  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_));
};

class ObCancelTaskStmt : public ObSystemCmdStmt
{
public:
  ObCancelTaskStmt()
    : ObSystemCmdStmt(stmt::T_CANCEL_TASK),
      task_id_()
  {
  }
  virtual ~ObCancelTaskStmt() {}
  const common::ObString &get_task_id() { return task_id_; }
  int set_task_id(const common::ObString &task_id)
  {
    int ret = common::OB_SUCCESS;

    if (task_id.length() <= 0) {
      ret = common::OB_INVALID_ARGUMENT;
    } else {
      task_id_ = task_id;
    }

    return ret;
  }

private:
  common::ObString task_id_;
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

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_STMT_
