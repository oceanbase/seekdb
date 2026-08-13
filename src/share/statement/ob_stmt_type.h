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

#ifdef OB_STMT_TYPE_DEF
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_NONE, err_stmt_type_priv, 0)
OB_STMT_TYPE_DEF(T_SELECT, get_dml_stmt_need_privs, 1, ACTION_TYPE_SELECT)
OB_STMT_TYPE_DEF(T_INSERT, get_dml_stmt_need_privs, 2, ACTION_TYPE_INSERT)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_REPLACE, get_dml_stmt_need_privs, 3)
OB_STMT_TYPE_DEF(T_DELETE, get_dml_stmt_need_privs, 4, ACTION_TYPE_DELETE)
OB_STMT_TYPE_DEF(T_UPDATE, get_dml_stmt_need_privs, 5, ACTION_TYPE_UPDATE)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_EXPLAIN, err_stmt_type_priv, 7)
OB_STMT_TYPE_DEF(T_CREATE_TABLE, get_create_table_stmt_need_privs, 20, ACTION_TYPE_CREATE_TABLE)
OB_STMT_TYPE_DEF(T_DROP_TABLE, get_drop_table_stmt_need_privs, 21, ACTION_TYPE_DROP_TABLE)
OB_STMT_TYPE_DEF(T_ALTER_TABLE, get_alter_table_stmt_need_privs, 22, ACTION_TYPE_ALTER_TABLE)
OB_STMT_TYPE_DEF(T_CREATE_INDEX, get_create_index_stmt_need_privs, 23, ACTION_TYPE_CREATE_INDEX)
OB_STMT_TYPE_DEF(T_DROP_INDEX, get_drop_index_stmt_need_privs, 24, ACTION_TYPE_DROP_INDEX)
OB_STMT_TYPE_DEF(T_CREATE_VIEW, err_stmt_type_priv, 25, ACTION_TYPE_CREATE_VIEW)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ALTER_VIEW, err_stmt_type_priv, 26)
OB_STMT_TYPE_DEF(T_DROP_VIEW, err_stmt_type_priv, 27, ACTION_TYPE_DROP_VIEW)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_TABLES, err_stmt_type_priv, 29)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_DATABASES, err_stmt_type_priv, 30)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_COLUMNS, err_stmt_type_priv, 31)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_VARIABLES, err_stmt_type_priv, 32)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_TABLE_STATUS, err_stmt_type_priv, 33)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_SCHEMA, err_stmt_type_priv, 34)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CREATE_DATABASE, err_stmt_type_priv, 35)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CREATE_TABLE, err_stmt_type_priv, 36)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CREATE_VIEW, err_stmt_type_priv, 37)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CREATE_PROCEDURE, err_stmt_type_priv, 38)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CREATE_FUNCTION, err_stmt_type_priv, 39)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_PARAMETERS, err_stmt_type_priv, 40)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_SERVER_STATUS, err_stmt_type_priv, 41)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_INDEXES, err_stmt_type_priv, 42)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_WARNINGS, err_stmt_type_priv, 43)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_ERRORS, err_stmt_type_priv, 44)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_PROCESSLIST, err_stmt_type_priv, 45)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CHARSET, err_stmt_type_priv, 46)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_COLLATION, err_stmt_type_priv, 47)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_STATUS, err_stmt_type_priv, 49)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_TRACE, err_stmt_type_priv, 52)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_ENGINES, err_stmt_type_priv, 53)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_PRIVILEGES, err_stmt_type_priv, 54)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_PROCEDURE_STATUS, err_stmt_type_priv, 55)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_FUNCTION_STATUS, err_stmt_type_priv, 56)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_GRANTS, err_stmt_type_priv, 57)
OB_STMT_TYPE_DEF(T_CREATE_USER, get_create_user_privs, 58, ACTION_TYPE_CREATE_USER)
OB_STMT_TYPE_DEF(T_DROP_USER, get_create_user_privs, 59, ACTION_TYPE_DROP_USER)
OB_STMT_TYPE_DEF(T_SET_PASSWORD, get_create_user_privs, 60, ACTION_TYPE_PASSWORD_CHANGE)
OB_STMT_TYPE_DEF(T_LOCK_USER, get_create_user_privs, 61, ACTION_TYPE_LOCK)
OB_STMT_TYPE_DEF(T_RENAME_USER, get_create_user_privs, 62, ACTION_TYPE_RENAME)
OB_STMT_TYPE_DEF(T_GRANT, get_grant_stmt_need_privs, 63, ACTION_TYPE_GRANT_OBJECT)
OB_STMT_TYPE_DEF(T_REVOKE, get_revoke_stmt_need_privs, 64, ACTION_TYPE_REVOKE_OBJECT)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_PREPARE, no_priv_needed, 65)
OB_STMT_TYPE_DEF(T_VARIABLE_SET, get_variable_set_stmt_need_privs, 66, ACTION_TYPE_ALTER_SYSTEM)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_EXECUTE, no_priv_needed, 67)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_DEALLOCATE, no_priv_needed, 68)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_START_TRANS, no_priv_needed, 69)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_END_TRANS, no_priv_needed, 70)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_KILL, no_priv_needed, 71)
OB_STMT_TYPE_DEF(T_ALTER_SYSTEM, get_server_super_priv, 72, ACTION_TYPE_ALTER_SYSTEM)
OB_STMT_TYPE_DEF(T_ALTER_SYSTEM_SETTP, get_server_alter_system_priv, 73, ACTION_TYPE_ALTER_SYSTEM)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_BOOTSTRAP, get_boot_strap_stmt_need_privs, 77)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_TABLET_CMD, err_stmt_type_priv, 79)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_FREEZE, get_server_alter_system_priv, 84)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_FLUSH_CACHE, get_server_alter_system_priv, 85)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_FLUSH_KVCACHE, get_server_alter_system_priv, 86)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_FLUSH_ILOGCACHE, get_server_alter_system_priv, 87)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_DROP_MEMTABLE, err_stmt_type_priv, 88)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CLEAR_MEMTABLE, err_stmt_type_priv, 89)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_PRINT_ROOT_TABLE, err_stmt_type_priv, 90)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CHECK_ROOT_TABLE, err_stmt_type_priv, 93)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CLEAR_ROOT_TABLE, get_server_alter_system_priv, 94)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_REFRESH_SCHEMA, get_server_alter_system_priv, 95)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CREATE_DATABASE, get_create_database_stmt_need_privs, 96)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_USE_DATABASE, no_priv_needed, 97)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ADMIN_MERGE, get_server_alter_system_priv, 104)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ALTER_DATABASE, get_alter_database_stmt_need_privs, 105)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_DROP_DATABASE, get_drop_database_stmt_need_privs, 106)
OB_STMT_TYPE_DEF(T_TRUNCATE_TABLE, get_truncate_table_stmt_need_privs, 110, ACTION_TYPE_TRUNCATE_TABLE)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_RENAME_TABLE, get_rename_table_stmt_need_privs, 111)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CREATE_TABLE_LIKE, get_create_table_like_stmt_need_privs, 112)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SET_NAMES, no_priv_needed, 113)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CLEAR_LOCATION_CACHE, get_server_alter_system_priv, 114)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CLEAR_MERGE_ERROR, get_server_alter_system_priv, 119)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_EMPTY_QUERY, no_priv_needed, 123)
OB_STMT_TYPE_DEF(T_CREATE_OUTLINE, get_create_outline_stmt_need_privs, 124, ACTION_TYPE_CREATE_OUTLINE)
OB_STMT_TYPE_DEF(T_ALTER_OUTLINE, get_alter_outline_stmt_need_privs, 125, ACTION_TYPE_ALTER_OUTLINE)
OB_STMT_TYPE_DEF(T_DROP_OUTLINE, get_drop_outline_stmt_need_privs, 126, ACTION_TYPE_DROP_OUTLINE)
OB_STMT_TYPE_DEF(T_FORK_TABLE, get_fork_table_stmt_need_privs, 127, ACTION_TYPE_FORK_TABLE)
OB_STMT_TYPE_DEF(T_FORK_DATABASE, get_fork_database_stmt_need_privs, 128, ACTION_TYPE_FORK_DATABASE)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_RECYCLEBIN_RESTORE_DATABASE, get_restore_database_stmt_need_privs, 131)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_RECYCLEBIN_RESTORE_TABLE, get_restore_table_stmt_need_privs, 132)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_PURGE_RECYCLEBIN, get_purge_recyclebin_stmt_need_privs, 134)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_PURGE_DATABASE, get_purge_database_stmt_need_privs, 136)
OB_STMT_TYPE_DEF(T_PURGE_TABLE, get_purge_table_stmt_need_privs, 137, ACTION_TYPE_PURGE_TABLE)
OB_STMT_TYPE_DEF(T_PURGE_INDEX, get_purge_index_stmt_need_privs, 138, ACTION_TYPE_PURGE_INDEX)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_RECYCLEBIN, err_stmt_type_priv, 139)
OB_STMT_TYPE_DEF(T_CREATE_ROUTINE, get_routine_stmt_need_privs, 141, ACTION_TYPE_OB_CREATE_ROUTINE)
OB_STMT_TYPE_DEF(T_DROP_ROUTINE, get_routine_stmt_need_privs, 142, ACTION_TYPE_OB_DROP_ROUTINE)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ALTER_ROUTINE, get_routine_stmt_need_privs, 143)
OB_STMT_TYPE_DEF(T_CALL_PROCEDURE, no_priv_needed, 144, ACTION_TYPE_EXECUTE_PROCEDURE)
OB_STMT_TYPE_DEF(T_ANONYMOUS_BLOCK, no_priv_needed, 145, ACTION_TYPE_EXECUTE_PROCEDURE)
OB_STMT_TYPE_DEF(T_CREATE_PACKAGE, no_priv_needed, 146, ACTION_TYPE_CREATE_PACKAGE)
OB_STMT_TYPE_DEF(T_CREATE_PACKAGE_BODY, no_priv_needed, 147, ACTION_TYPE_CREATE_PACKAGE_BODY)
OB_STMT_TYPE_DEF(T_DROP_PACKAGE, no_priv_needed, 149, ACTION_TYPE_DROP_PACKAGE)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_LOAD_TIME_ZONE_INFO, get_server_alter_system_priv, 150)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CANCEL_TASK, get_server_alter_system_priv, 151)
// 155-156: CREATE/DROP SYNONYM abandoned, ids reserved
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_BUILD_INDEX_SSTABLE, get_server_super_priv, 158)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ANALYZE, no_priv_needed, 159)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_LOAD_DATA, get_load_data_stmt_need_privs, 161)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_STMT_TYPE_MAX, get_server_super_priv, 162)
OB_STMT_TYPE_DEF(T_SET_TABLE_COMMENT, no_priv_needed, 168, ACTION_TYPE_COMMENT)
OB_STMT_TYPE_DEF(T_SET_COLUMN_COMMENT, no_priv_needed, 169, ACTION_TYPE_COMMENT)
// 171-172: CREATE/DROP TYPE abandoned, ids reserved
OB_STMT_TYPE_DEF(T_ALTER_SYSTEM_SET_PARAMETER, get_server_alter_system_priv, 177, ACTION_TYPE_ALTER_SYSTEM)
OB_STMT_TYPE_DEF(T_OPTIMIZE_TABLE, no_priv_needed, 178, ACTION_TYPE_ALTER_TABLE)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CREATE_SAVEPOINT, no_priv_needed, 181)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ROLLBACK_SAVEPOINT, no_priv_needed, 182)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_RELEASE_SAVEPOINT, no_priv_needed, 183)
// 186-187: Oracle CREATE/DROP TABLESPACE abandoned, ids reserved
OB_STMT_TYPE_DEF(T_CREATE_TRIGGER, get_trigger_stmt_need_privs, 188, ACTION_TYPE_CREATE_TRIGGER)
OB_STMT_TYPE_DEF(T_DROP_TRIGGER, get_trigger_stmt_need_privs, 189, ACTION_TYPE_DROP_TRIGGER)
OB_STMT_TYPE_DEF(T_CREATE_ROLE, get_role_privs, 191, ACTION_TYPE_CREATE_ROLE)
OB_STMT_TYPE_DEF(T_DROP_ROLE, get_role_privs, 192, ACTION_TYPE_DROP_ROLE)
OB_STMT_TYPE_DEF(T_ALTER_ROLE, no_priv_needed, 193, ACTION_TYPE_ALTER_ROLE)
OB_STMT_TYPE_DEF(T_SET_ROLE, no_priv_needed, 194, ACTION_TYPE_SET_ROLE)
OB_STMT_TYPE_DEF(T_SYSTEM_GRANT, no_priv_needed, 195, ACTION_TYPE_SYSTEM_GRANT)
OB_STMT_TYPE_DEF(T_SYSTEM_REVOKE, no_priv_needed, 196, ACTION_TYPE_SYSTEM_REVOKE)
// 197: Oracle USER PROFILE abandoned, id reserved
OB_STMT_TYPE_DEF(T_ALTER_USER_ROLE, get_create_user_privs, 198, ACTION_TYPE_ALTER_USER)
// 199: Oracle AUDIT abandoned, id reserved
OB_STMT_TYPE_DEF(T_LOGIN, no_priv_needed, 200, ACTION_TYPE_LOGON)
OB_STMT_TYPE_DEF(T_LOGOFF, no_priv_needed, 201, ACTION_TYPE_LOGOFF)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_REFRESH_MEMORY_STAT, get_server_super_priv, 207)
// 209: Oracle ALTER TABLESPACE abandoned, id reserved
OB_STMT_TYPE_DEF(T_GRANT_ROLE, no_priv_needed, 211, ACTION_TYPE_GRANT_ROLE)
OB_STMT_TYPE_DEF(T_REVOKE_ROLE, no_priv_needed, 212, ACTION_TYPE_REVOKE_ROLE)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_TRIGGERS, err_stmt_type_priv, 214)
// 217-218: CREATE/DROP PUBLIC SYNONYM abandoned, ids reserved
// 219-221: Oracle CREATE/ALTER/DROP PROFILE abandoned, ids reserved
OB_STMT_TYPE_DEF(T_ALTER_USER, get_create_user_privs, 222, ACTION_TYPE_ALTER_USER)
OB_STMT_TYPE_DEF(T_ALTER_TRIGGER, no_priv_needed, 230, ACTION_TYPE_ALTER_TRIGGER)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CREATE_TRIGGER, err_stmt_type_priv, 232)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_DIAGNOSTICS, no_priv_needed, 233)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_PROFILE, err_stmt_type_priv, 237)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_FLUSH_DAG_WARNINGS, get_server_super_priv, 259)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_LOCK_TABLE, get_lock_table_priv, 268)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ARCHIVE_TENANT, get_server_alter_system_priv, 270)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_DISCONNECT_CLUSTER, get_server_super_priv, 271)
// 272: T_WASH_MEMORY_FRAGMENTATION abandoned, id reserved
// 273-274: Oracle application context statements abandoned, ids reserved
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_CHECKPOINT_SLOG, get_server_alter_system_priv, 275)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_REFRESH_IO_CALIBRATION, get_server_alter_system_priv, 276)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_RECOVER, get_server_alter_system_priv, 279)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_SEQUENCES, err_stmt_type_priv, 283)
// 287 is reserved for a removed statement type.
// 288: T_RECOVER_TABLE abandoned, id reserved
// 289: T_CANCEL_RECOVER_TABLE abandoned, id reserved
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ACTIVATE_STANDBY, get_server_alter_system_priv, 290)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SWITCHOVER_TO_STANDBY, get_server_alter_system_priv, 291)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SWITCHOVER_TO_PRIMARY, get_server_alter_system_priv, 293)
OB_STMT_TYPE_DEF(T_ALTER_SYSTEM_RESET_PARAMETER, get_server_alter_system_priv, 292, ACTION_TYPE_ALTER_SYSTEM)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_FLUSH_PRIVILEGES, no_priv_needed, 298)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_ALTER_LS_REPLICA, get_server_alter_system_priv, 299)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_PROCEDURE_CODE, err_stmt_type_priv, 300)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_FUNCTION_CODE, err_stmt_type_priv, 301)
// 302: T_CHANGE_EXTERNAL_STORAGE_DEST abandoned, id reserved

OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CREATE_USER, err_stmt_type_priv, 304)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_ENGINE, err_stmt_type_priv, 311)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_OPEN_TABLES, err_stmt_type_priv, 312)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_SHOW_CHECK_TABLE, err_stmt_type_priv, 360)
// 365 is reserved for a removed statement type.
// 366 is reserved for the removed MODULE DATA statement type.
// 367 is reserved for a removed statement type.
//370 for admin_alter_ls

// 377 is reserved for a removed statement type.

OB_STMT_TYPE_DEF_UNKNOWN_AT(T_DIFF_TABLE, get_dml_stmt_need_privs, 392)
OB_STMT_TYPE_DEF_UNKNOWN_AT(T_MERGE_TABLE, get_merge_table_stmt_need_privs, 393)

OB_STMT_TYPE_DEF_UNKNOWN_AT(T_MAX, err_stmt_type_priv, 500)
#endif

#ifndef OCEANBASE_SHARE_STATEMENT_OB_STMT_TYPE_
#define OCEANBASE_SHARE_STATEMENT_OB_STMT_TYPE_

// default set the action_type to ACTION_TYPE_UNKNOWN
#define OB_STMT_TYPE_DEF_UNKNOWN_AT(stmt_type, priv_check_func, id) OB_STMT_TYPE_DEF(stmt_type, priv_check_func, id, ACTION_TYPE_UNKNOWN)

namespace oceanbase {
namespace sql {
namespace stmt {

enum StmtType : int32_t
{
#define OB_STMT_TYPE_DEF(stmt_type, priv_check_func, id, action_type) stmt_type = id,
#include "share/statement/ob_stmt_type.h"
#undef OB_STMT_TYPE_DEF

#define IS_INSERT_OR_REPLACE_STMT(stmt_type) (stmt::T_INSERT == (stmt_type) || stmt::T_REPLACE == (stmt_type))
};

struct StmtTypeIndex
{
public:
  StmtTypeIndex()
    : stmt_type_idx_()
  {
    int i = 0;
    for (int j = 0; j < ARRAYSIZEOF(stmt_type_idx_); j++) {
      stmt_type_idx_[j] = -1;
    }
    #define OB_STMT_TYPE_DEF(stmt_type, priv_check_func, id, action_type) stmt_type_idx_[stmt_type] = i++;
    #include "share/statement/ob_stmt_type.h"
    #undef OB_STMT_TYPE_DEF
  }
  int32_t stmt_type_idx_[T_MAX + 1];
};

inline int32_t get_stmt_type_idx(StmtType type)
{
  static StmtTypeIndex inst;
  return inst.stmt_type_idx_[type];
}

}
}
}

#endif /* _OB_STMT_TYPE_H */
