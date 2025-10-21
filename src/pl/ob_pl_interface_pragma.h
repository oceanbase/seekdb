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

#include "lib/string/ob_string.h"
#include "lib/hash/ob_hashmap.h"
#include "sql/engine/ob_exec_context.h"
#include "pl/sys_package/ob_dbms_upgrade.h"
#include "pl/sys_package/ob_dbms_stats.h"
#include "pl/sys_package/ob_dbms_scheduler_mysql.h"
#include "pl/sys_package/ob_dbms_application.h"
#include "pl/sys_package/ob_dbms_session.h"
#include "pl/sys_package/ob_dbms_monitor.h"
#include "pl/sys_package/ob_dbms_sql.h"
#include "pl/sys_package/ob_dbms_user_define_rule.h"
#include "pl/sys_package/ob_dbms_xplan.h"
#include "pl/sys_package/ob_dbms_session.h"
#include "pl/sys_package/ob_dbms_space.h"
#include "pl/sys_package/ob_dbms_workload_repository.h"
#include "pl/sys_package/ob_dbms_mview_mysql.h"
#include "pl/sys_package/ob_dbms_mview_stats_mysql.h"
#include "pl/sys_package/ob_pl_dbms_trusted_certificate_manager.h"
#include "pl/sys_package/ob_dbms_limit_calculator_mysql.h"
#include "pl/sys_package/ob_dbms_external_table.h"
#include "pl/sys_package/ob_dbms_vector_mysql.h"
#include "pl/sys_package/ob_dbms_hybrid_vector_mysql.h"
#include "pl/pl_recompile/ob_pl_recompile_task_helper.h"
#include "pl/sys_package/ob_dbms_partition.h"
#include "pl/sys_package/ob_dbms_ai_service.h"

#ifdef INTERFACE_DEF
  INTERFACE_DEF(INTERFACE_START, "TEST", (ObPLInterfaceImpl::call))

  /*************************.. add interface here ..*****************************/
  // start of __dbms_upgrade
  INTERFACE_DEF(INTERFACE_DBMS_UPGRADE_SINGLE, "UPGRADE_SINGLE", (ObDBMSUpgrade::upgrade_single))
  INTERFACE_DEF(INTERFACE_DBMS_UPGRADE_ALL, "UPGRADE_ALL", (ObDBMSUpgrade::upgrade_all))
  INTERFACE_DEF(INTERFACE_DBMS_FLUSH_DLL_NCOMP, "FLUSH_DLL_NCOMP", (ObDBMSUpgrade::flush_dll_ncomp))
  // end of __dbms_upgrade

  // start of dbms_application_info
  INTERFACE_DEF(INTERFACE_DBMS_READ_CLIENT_INFO, "READ_CLIENT_INFO", (ObDBMSAppInfo::read_client_info))
  INTERFACE_DEF(INTERFACE_DBMS_READ_MODULE, "READ_MODULE", (ObDBMSAppInfo::read_module))
  INTERFACE_DEF(INTERFACE_DBMS_SET_ACTION, "SET_ACTION", (ObDBMSAppInfo::set_action))
  INTERFACE_DEF(INTERFACE_DBMS_SET_CLIENT_INFO, "SET_CLIENT_INFO", (ObDBMSAppInfo::set_client_info))
  INTERFACE_DEF(INTERFACE_DBMS_SET_MODULE, "SET_MODULE", (ObDBMSAppInfo::set_module))
  // end of dbms_application_info

  // start of dbms_monitor
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_TRACE_ENABLE, "OB_SESSION_TRACE_ENABLE", (ObDBMSMonitor::session_trace_enable))
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_TRACE_DISABLE, "OB_SESSION_TRACE_DISABLE", (ObDBMSMonitor::session_trace_disable))
  INTERFACE_DEF(INTERFACE_DBMS_CLIENT_ID_TRACE_ENABLE, "OB_CLIENT_ID_TRACE_ENABLE", (ObDBMSMonitor::client_id_trace_enable))
  INTERFACE_DEF(INTERFACE_DBMS_CLIENT_ID_TRACE_DISABLE, "OB_CLIENT_ID_TRACE_DISABLE", (ObDBMSMonitor::client_id_trace_disable))
  INTERFACE_DEF(INTERFACE_DBMS_MOD_ACT_TRACE_ENABLE, "OB_MOD_ACT_TRACE_ENABLE", (ObDBMSMonitor::mod_act_trace_enable))
  INTERFACE_DEF(INTERFACE_DBMS_MOD_ACT_TRACE_DISABLE, "OB_MOD_ACT_TRACE_DISABLE", (ObDBMSMonitor::mod_act_trace_disable))
  INTERFACE_DEF(INTERFACE_DBMS_TENANT_TRACE_ENABLE, "OB_TENANT_TRACE_ENABLE", (ObDBMSMonitor::tenant_trace_enable))
  INTERFACE_DEF(INTERFACE_DBMS_TENANT_TRACE_DISABLE, "OB_TENANT_TRACE_DISABLE", (ObDBMSMonitor::tenant_trace_disable))
  // end of dbms_monitor


  //start of dbms_stat
  INTERFACE_DEF(INTERFACE_DBMS_STATS_GATHER_TABLE_STATS, "GATHER_TABLE_STATS", (ObDbmsStats::gather_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_GATHER_SCHEMA_STATS, "GATHER_SCHEMA_STATS", (ObDbmsStats::gather_schema_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_SET_TABLE_STATS, "SET_TABLE_STATS", (ObDbmsStats::set_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_SET_COLUMN_STATS, "SET_COLUMN_STATS", (ObDbmsStats::set_column_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_DELETE_TABLE_STATS, "DELETE_TABLE_STATS", (ObDbmsStats::delete_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_DELETE_COLUMN_STATS, "DELETE_COLUMN_STATS", (ObDbmsStats::delete_column_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_DELETE_SCHEMA_STATS, "DELETE_SCHEMA_STATS", (ObDbmsStats::delete_schema_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_FLUSH_DATABASE_MONITORING_INFO, "FLUSH_DATABASE_MONITORING_INFO", (ObDbmsStats::flush_database_monitoring_info))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_CREATE_STAT_TABLE, "CREATE_STAT_TABLE", (ObDbmsStats::create_stat_table))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_DROP_STAT_TABLE, "DROP_STAT_TABLE", (ObDbmsStats::drop_stat_table))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_EXPORT_TABLE_STATS, "EXPORT_TABLE_STATS", (ObDbmsStats::export_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_EXPORT_COLUMN_STATS, "EXPORT_COLUMN_STATS", (ObDbmsStats::export_column_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_EXPORT_SCHEMA_STATS, "EXPORT_SCHEMA_STATS", (ObDbmsStats::export_schema_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_IMPORT_TABLE_STATS, "IMPORT_TABLE_STATS", (ObDbmsStats::import_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_IMPORT_COLUMN_STATS, "IMPORT_COLUMN_STATS", (ObDbmsStats::import_column_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_IMPORT_SCHEMA_STATS, "IMPORT_SCHEMA_STATS", (ObDbmsStats::import_schema_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_LOCK_TABLE_STATS, "LOCK_TABLE_STATS", (ObDbmsStats::lock_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_LOCK_PARTITION_STATS, "LOCK_PARTITION_STATS", (ObDbmsStats::lock_partition_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_LOCK_SCHEMA_STATS, "LOCK_SCHEMA_STATS", (ObDbmsStats::lock_schema_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_UNLOCK_TABLE_STATS, "UNLOCK_TABLE_STATS", (ObDbmsStats::unlock_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_UNLOCK_PARTITION_STATS, "UNLOCK_PARTITION_STATS", (ObDbmsStats::unlock_partition_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_UNLOCK_SCHEMA_STATS, "UNLOCK_SCHEMA_STATS", (ObDbmsStats::unlock_schema_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_GATHER_DATABASE_STATS_JOB_PROC, "GATHER_DATABASE_STATS_JOB_PROC", (ObDbmsStats::gather_database_stats_job_proc))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_RESTORE_TABLE_STATS, "RESTORE_TABLE_STATS", (ObDbmsStats::restore_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_RESTORE_SCHEMA_STATS, "RESTORE_SCHEMA_STATS", (ObDbmsStats::restore_schema_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_PURGE_STATS, "PURGE_STATS", (ObDbmsStats::purge_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_ALTER_STATS_HISTORY_RETENTION, "ALTER_STATS_HISTORY_RETENTION", (ObDbmsStats::alter_stats_history_retention))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_GET_STATS_HISTORY_AVAILABILITY, "GET_STATS_HISTORY_AVAILABILITY", (ObDbmsStats::get_stats_history_availability))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_GET_STATS_HISTORY_RETENTION, "GET_STATS_HISTORY_RETENTION", (ObDbmsStats::get_stats_history_retention))
  INTERFACE_DEF(INTERFACE_DBMS_RESET_GLOBAL_PREF_DEFAULTS, "RESET_GLOBAL_PREF_DEFAULTS", (ObDbmsStats::reset_global_pref_defaults))
  INTERFACE_DEF(INTERFACE_DBMS_SET_GLOBAL_PREFS, "SET_GLOBAL_PREFS", (ObDbmsStats::set_global_prefs))
  INTERFACE_DEF(INTERFACE_DBMS_SET_SCHEMA_PREFS, "SET_SCHEMA_PREFS", (ObDbmsStats::set_schema_prefs))
  INTERFACE_DEF(INTERFACE_DBMS_SET_TABLE_PREFS, "SET_TABLE_PREFS", (ObDbmsStats::set_table_prefs))
  INTERFACE_DEF(INTERFACE_DBMS_GET_PREFS, "GET_PREFS", (ObDbmsStats::get_prefs))
  INTERFACE_DEF(INTERFACE_DBMS_DELETE_SCHEMA_PREFS, "DELETE_SCHEMA_PREFS", (ObDbmsStats::delete_schema_prefs))
  INTERFACE_DEF(INTERFACE_DBMS_DELETE_TABLE_PREFS, "DELETE_TABLE_PREFS", (ObDbmsStats::delete_table_prefs))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_GATHER_INDEX_STATS, "GATHER_INDEX_STATS", (ObDbmsStats::gather_index_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_DELETE_INDEX_STATS, "DELETE_INDEX_STATS", (ObDbmsStats::delete_index_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_SET_INDEX_STATS, "SET_INDEX_STATS", (ObDbmsStats::set_index_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_EXPORT_INDEX_STATS, "EXPORT_INDEX_STATS", (ObDbmsStats::export_index_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_IMPORT_INDEX_STATS, "IMPORT_INDEX_STATS", (ObDbmsStats::import_index_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_COPY_TABLE_STATS, "COPY_TABLE_STATS", (ObDbmsStats::copy_table_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_CNACEL_GATHER_STATS, "CANCEL_GATHER_STATS", (ObDbmsStats::cancel_gather_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_GATHER_SYSTEM_STATS, "GATHER_SYSTEM_STATS", (ObDbmsStats::gather_system_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_DELETE_SYSTEM_STATS, "DELETE_SYSTEM_STATS", (ObDbmsStats::delete_system_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_SET_SYSTEM_STATS, "SET_SYSTEM_STATS", (ObDbmsStats::set_system_stats))
  INTERFACE_DEF(INTERFACE_DBMS_STATS_ASYNC_GATHER_STATS_JOB_PROC, "ASYNC_GATHER_STATS_JOB_PROC", (ObDbmsStats::async_gather_stats_job_proc))
  //end of dbms_stat

  //start of dbms xplan
  INTERFACE_DEF(INTERFACE_DBMS_XPLAN_ENABLE_OPT_TRACE, "ENABLE_OPT_TRACE", (ObDbmsXplan::enable_opt_trace))
  INTERFACE_DEF(INTERFACE_DBMS_XPLAN_DISABLE_OPT_TRACE, "DISABLE_OPT_TRACE", (ObDbmsXplan::disable_opt_trace))
  INTERFACE_DEF(INTERFACE_DBMS_XPLAN_SET_OPT_TRACE_PARAMETER, "SET_OPT_TRACE_PARAMETER", (ObDbmsXplan::set_opt_trace_parameter))
  INTERFACE_DEF(INTERFACE_DBMS_XPLAN_DISPLAY, "DISPLAY", (ObDbmsXplan::display))
  INTERFACE_DEF(INTERFACE_DBMS_XPLAN_DISPLAY_CURSOR, "DISPLAY_CURSOR", (ObDbmsXplan::display_cursor))
  INTERFACE_DEF(INTERFACE_DBMS_XPLAN_DISPLAY_SQL_PLAN_BASELINE, "DISPLAY_SQL_PLAN_BASELINE", (ObDbmsXplan::display_sql_plan_baseline))
  INTERFACE_DEF(INTERFACE_DBMS_XPLAN_DISPLAY_ACTIVE_SESSION_PLAN, "DISPLAY_ACTIVE_SESSION_PLAN", (ObDbmsXplan::display_active_session_plan))
  //end of dbms xplan


  //start of dbms_scheduler_mysql
#define DEFINE_DBMS_SCHEDULER_MYSQL_INTERFACE(symbol, func) \
  INTERFACE_DEF(INTERFACE_##symbol, #symbol, (func))

  DEFINE_DBMS_SCHEDULER_MYSQL_INTERFACE(DBMS_SCHEDULER_MYSQL_DISABLE, ObDBMSSchedulerMysql::disable)
  DEFINE_DBMS_SCHEDULER_MYSQL_INTERFACE(DBMS_SCHEDULER_MYSQL_ENABLE, ObDBMSSchedulerMysql::enable)
  DEFINE_DBMS_SCHEDULER_MYSQL_INTERFACE(DBMS_SCHEDULER_MYSQL_SET_ATTRIBUTE, ObDBMSSchedulerMysql::set_attribute)
  DEFINE_DBMS_SCHEDULER_MYSQL_INTERFACE(DBMS_SCHEDULER_MYSQL_GET_AND_INCREASE_JOB_ID, ObDBMSSchedulerMysql::get_and_increase_job_id)

#undef DEFINE_DBMS_SCHEDULER_MYSQL_INTERFACE
  //end of dbms_scheduler_mysql



  // start of dbms_session
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_CLEAR_ALL_CONTEXT, "CLEAR_ALL_CONTEXT", (ObDBMSSession::clear_all_context))
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_CLEAR_CONTEXT, "CLEAR_CONTEXT", (ObDBMSSession::clear_context))
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_CLEAR_IDENTIFIER, "CLEAR_IDENTIFIER", (ObDBMSSession::clear_identifier))
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_SET_CONTEXT, "SET_CONTEXT", (ObDBMSSession::set_context))
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_SET_IDENTIFIER, "SET_IDENTIFIER", (ObDBMSSession::set_identifier))
  INTERFACE_DEF(INTERFACE_DBMS_SESSION_RESET_PACKAGE, "RESET_PACKAGE", (ObDBMSSession::reset_package))
  // end of dbms_session

  // start of dbms_space
  INTERFACE_DEF(INTERFACE_DBMS_SPACE_CREATE_INDEX_COST, "CREATE_INDEX_COST", (ObDbmsSpace::create_index_cost))
  // end of dbms_space



  // start of dbms_mview_mysql
#define DEFINE_DBMS_MVIEW_MYSQL_INTERFACE(symbol, func) \
  INTERFACE_DEF(INTERFACE_##symbol, #symbol, (func))

  DEFINE_DBMS_MVIEW_MYSQL_INTERFACE(DBMS_MVIEW_MYSQL_PURGE_LOG, ObDBMSMViewMysql::purge_log)
  DEFINE_DBMS_MVIEW_MYSQL_INTERFACE(DBMS_MVIEW_MYSQL_REFRESH, ObDBMSMViewMysql::refresh)

#undef DEFINE_DBMS_MVIEW_MYSQL_INTERFACE
  // end of dbms_mview_mysql

  // start of dbms_mview_stats_mysql
#define DEFINE_DBMS_MVIEW_STATS_MYSQL_INTERFACE(symbol, func) \
  INTERFACE_DEF(INTERFACE_##symbol, #symbol, (func))

  DEFINE_DBMS_MVIEW_STATS_MYSQL_INTERFACE(DBMS_MVIEW_STATS_MYSQL_SET_SYS_DEFAULT, ObDBMSMViewStatsMysql::set_system_default)
  DEFINE_DBMS_MVIEW_STATS_MYSQL_INTERFACE(DBMS_MVIEW_STATS_MYSQL_SET_MVREF_STATS_PARAMS, ObDBMSMViewStatsMysql::set_mvref_stats_params)
  DEFINE_DBMS_MVIEW_STATS_MYSQL_INTERFACE(DBMS_MVIEW_STATS_MYSQL_PURGE_REFRESH_STATS, ObDBMSMViewStatsMysql::purge_refresh_stats)

#undef DEFINE_DBMS_MVIEW_STATS_MYSQL_INTERFACE
  // end of dbms_mview_stats_mysql

  // start of dbms_udr
  INTERFACE_DEF(INTERFACE_DBMS_UDR_CREATE_RULE, "CREATE_RULE", (ObDBMSUserDefineRule::create_rule))
  INTERFACE_DEF(INTERFACE_DBMS_UDR_REMOVE_RULE, "REMOVE_RULE", (ObDBMSUserDefineRule::remove_rule))
  INTERFACE_DEF(INTERFACE_DBMS_UDR_ENABLE_RULE, "ENABLE_RULE", (ObDBMSUserDefineRule::enable_rule))
  INTERFACE_DEF(INTERFACE_DBMS_UDR_DISABLE_RULE, "DISABLE_RULE", (ObDBMSUserDefineRule::disable_rule))
  // end of dbms_udr

  // start of dbms_workload_repository
  INTERFACE_DEF(INTERFACE_DBMS_WR_CREATE_SNAPSHOT, "WR_CREATE_SNAPSHOT", (ObDbmsWorkloadRepository::create_snapshot))
  INTERFACE_DEF(INTERFACE_DBMS_WR_DROP_SNAPSHOT_RANGE, "WR_DROP_SNAPSHOT_RANGE", (ObDbmsWorkloadRepository::drop_snapshot_range))
  INTERFACE_DEF(INTERFACE_DBMS_WR_MODIFY_SNAPSHOT_SETTINGS, "WR_MODIFY_SNAPSHOT_SETTINGS", (ObDbmsWorkloadRepository::modify_snapshot_settings))
  INTERFACE_DEF(INTERFACE_DBMS_GENERATE_ASH_REPORT_TEXT, "GENERATE_ASH_REPORT_TEXT", (ObDbmsWorkloadRepository::generate_ash_report_text))
  // end of dbms_workload_repository


    // start of dbms_vector_mysql
#define DEFINE_DBMS_VECTOR_MYSQL_INTERFACE(symbol, func) \
  INTERFACE_DEF(INTERFACE_##symbol, #symbol, (func))

  DEFINE_DBMS_VECTOR_MYSQL_INTERFACE(DBMS_VECTOR_MYSQL_REFRESH_INDEX, ObDBMSVectorMySql::refresh_index)
  DEFINE_DBMS_VECTOR_MYSQL_INTERFACE(DBMS_VECTOR_MYSQL_REBUILD_INDEX, ObDBMSVectorMySql::rebuild_index)
  DEFINE_DBMS_VECTOR_MYSQL_INTERFACE(DBMS_VECTOR_MYSQL_REFRESH_INDEX_INNER, ObDBMSVectorMySql::refresh_index_inner)
  DEFINE_DBMS_VECTOR_MYSQL_INTERFACE(DBMS_VECTOR_MYSQL_REBUILD_INDEX_INNER, ObDBMSVectorMySql::rebuild_index_inner)
  DEFINE_DBMS_VECTOR_MYSQL_INTERFACE(DBMS_VECTOR_MYSQL_INDEX_VECTOR_MEMORY_ADVISOR, ObDBMSVectorMySql::index_vector_memory_advisor)
  DEFINE_DBMS_VECTOR_MYSQL_INTERFACE(DBMS_VECTOR_MYSQL_INDEX_VECTOR_MEMORY_ESTIMATE, ObDBMSVectorMySql::index_vector_memory_estimate)

#undef DEFINE_DBMS_VECTOR_MYSQL_INTERFACE
  // end of dbms_vector_mysql

  /****************************************************************************/

  // start of dbms_trusted_certificate_manager
  INTERFACE_DEF(INTERFACE_DBMS_ADD_TRUSTED_CERTIFICATE, "ADD_TRUSTED_CERTIFICATE", (ObPlDBMSTrustedCertificateManager::add_trusted_certificate))
  INTERFACE_DEF(INTERFACE_DBMS_DELETE_TRUSTED_CERTIFICATE, "DELETE_TRUSTED_CERTIFICATE", (ObPlDBMSTrustedCertificateManager::delete_trusted_certificate))
  INTERFACE_DEF(INTERFACE_DBMS_UPDATE_TRUSTED_CERTIFICATE, "UPDATE_TRUSTED_CERTIFICATE", (ObPlDBMSTrustedCertificateManager::update_trusted_certificate))
  // end of end of dbms_workload_repository

  // start of dbms_ob_limit_calculator
  INTERFACE_DEF(INTERFACE_DBMS_OB_LIMIT_CALCULATOR_PHY_RES_CALCULATE_BY_LOGIC_RES, "PHY_RES_CALCULATE_BY_LOGIC_RES", (ObDBMSLimitCalculator::phy_res_calculate_by_logic_res))
  INTERFACE_DEF(INTERFACE_DBMS_OB_LIMIT_CALCULATOR_PHY_RES_CALCULATE_BY_UNIT, "PHY_RES_CALCULATE_BY_UNIT", (ObDBMSLimitCalculator::phy_res_calculate_by_unit))
  INTERFACE_DEF(INTERFACE_DBMS_OB_LIMIT_CALCULATOR_PHY_RES_CALCULATE_BY_STADNBY_TENANT, "PHY_RES_CALCULATE_BY_STANDBY_TENANT", (ObDBMSLimitCalculator::phy_res_calculate_by_standby_tenant))
  // end of dbms_ob_limit_calculator

  // start of dbms_external_table
  INTERFACE_DEF(INTERFACE_DBMS_EXTERNAL_TABLE_AUTO_REFRESH_EXTERNAL_TABLE, "AUTO_REFRESH_EXTERNAL_TABLE", (ObDBMSExternalTable::auto_refresh_external_table))
  //end of dbms_external_table

  // start of dbms_ddl
  // end of dbms_ddl

  // start of dbms_partition
  INTERFACE_DEF(INTERFACE_DBMS_PARTITION_MANAGE_DYNAMIC_PARTITION, "DBMS_PARTITION_MANAGE_DYNAMIC_PARTITION", (ObDBMSPartition::manage_dynamic_partition))
  // end of dbms_partition

  // start of dbms_ai_service
  INTERFACE_DEF(INTERFACE_DBMS_AI_SERVICE_CREATE_AI_MODEL_MYSQL, "DBMS_AI_SERVICE_CREATE_AI_MODEL_MYSQL", (ObDBMSAiService::create_ai_model))
  INTERFACE_DEF(INTERFACE_DBMS_AI_SERVICE_DROP_AI_MODEL_MYSQL, "DBMS_AI_SERVICE_DROP_AI_MODEL_MYSQL", (ObDBMSAiService::drop_ai_model))
  INTERFACE_DEF(INTERFACE_DBMS_AI_SERVICE_CREATE_AI_MODEL_ENDPOINT_MYSQL, "DBMS_AI_SERVICE_CREATE_AI_MODEL_ENDPOINT_MYSQL", (ObDBMSAiService::create_ai_model_endpoint))
  INTERFACE_DEF(INTERFACE_DBMS_AI_SERVICE_ALTER_AI_MODEL_ENDPOINT_MYSQL, "DBMS_AI_SERVICE_ALTER_AI_MODEL_ENDPOINT_MYSQL", (ObDBMSAiService::alter_ai_model_endpoint))
  INTERFACE_DEF(INTERFACE_DBMS_AI_SERVICE_DROP_AI_MODEL_ENDPOINT_MYSQL, "DBMS_AI_SERVICE_DROP_AI_MODEL_ENDPOINT_MYSQL", (ObDBMSAiService::drop_ai_model_endpoint))
  // end of dbms_ai_service

  // start of dbms_hybrid_search
#define DEFINE_DBMS_HYBRID_VECTOR_MYSQL_INTERFACE(symbol, func) \
INTERFACE_DEF(INTERFACE_##symbol, #symbol, (func))

DEFINE_DBMS_HYBRID_VECTOR_MYSQL_INTERFACE(DBMS_HYBRID_VECTOR_MYSQL_SEARCH, ObDBMSHybridVectorMySql::search)
DEFINE_DBMS_HYBRID_VECTOR_MYSQL_INTERFACE(DBMS_HYBRID_VECTOR_MYSQL_GET_SQL, ObDBMSHybridVectorMySql::get_sql)

#undef DEFINE_DBMS_HYBRID_VECTOR_MYSQL_INTERFACE
  // end of dbms_hybrid_search

  INTERFACE_DEF(INTERFACE_END, "INVALID", (nullptr))
#endif

#ifndef OCEANBASE_SRC_PL_OB_PL_INTERFACE_PRAGMA_H_
#define OCEANBASE_SRC_PL_OB_PL_INTERFACE_PRAGMA_H_

namespace oceanbase
{
namespace pl
{

typedef int(*PL_C_INTERFACE_t)(ObPLExecCtx&, ParamStore&, ObObj&);

enum ObPLInterfaceType
{
#define INTERFACE_DEF(type, name, entry) type,
#include "pl/ob_pl_interface_pragma.h"
#undef INTERFACE_DEF
};

class ObPLInterfaceService
{
public:
  ObPLInterfaceService() {}
  virtual ~ObPLInterfaceService() {}

  PL_C_INTERFACE_t get_entry(common::ObString &name) const;
  int init();

private:

  ObPLInterfaceType get_type(common::ObString &name) const;

private:
  typedef common::hash::ObHashMap<common::ObString, ObPLInterfaceType,
      common::hash::NoPthreadDefendMode> InterfaceMap;
  InterfaceMap interface_map_;
};

class ObPLInterfaceImpl
{
public:
  ObPLInterfaceImpl() {}
  virtual ~ObPLInterfaceImpl() {}

public:
  static int call(sql::ObExecContext &ctx, sql::ParamStore &params, common::ObObj &result);
  virtual int check_params() = 0;

};

}
}

#endif /* OCEANBASE_SRC_PL_OB_PL_INTERFACE_PRAGMA_H_ */
