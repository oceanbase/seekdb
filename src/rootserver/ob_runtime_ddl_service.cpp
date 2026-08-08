#include "share/ob_ex_rpc.h"
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

#define USING_LOG_PREFIX RS
#include "rootserver/ob_runtime_ddl_service.h"

#include "rootserver/ob_ddl_service.h"
#include "rootserver/ob_table_creator.h"
#include "share/ob_global_stat_proxy.h"
#include "share/ob_schema_status_proxy.h"
#include "storage/tx/ob_ts_mgr.h"
#include "share/ob_sql_client_decorator.h"
#include "share/ob_merge_info.h"
#include "share/ob_global_merge_table_operator.h"
#include "rootserver/ob_load_inner_table_schema_executor.h"
#include "logservice/ob_log_service.h"
#include "logservice/replayservice/ob_log_replay_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/rc/ob_server_runtime.h"
#include "share/scn.h"
#include "share/ob_server_struct.h"
#include "share/ob_version_parser.h"

// The input of value must be a string
#define SET_RUNTIME_VARIABLE(sysvar_id, value) \
        if (OB_SUCC(ret)) {\
          int64_t store_idx = OB_INVALID_INDEX; \
          if (OB_FAIL(share::ObSysVarMeta::calc_sys_var_store_idx(sysvar_id, store_idx))) { \
            LOG_WARN("failed to calc sys var store idx", KR(ret), K(sysvar_id)); \
          } else if (OB_UNLIKELY(store_idx < 0 \
                     || store_idx >= share::ObSysVarMeta::ALL_SYS_VARS_COUNT)) { \
            ret = OB_ERR_UNEXPECTED; \
            LOG_WARN("got store_idx is invalid", K(ret), K(store_idx)); \
          } else if (OB_FAIL(sys_params[store_idx].init( \
                     ObSysVariables::get_name(store_idx),\
                     ObSysVariables::get_type(store_idx),\
                     value,\
                     ObSysVariables::get_min(store_idx),\
                     ObSysVariables::get_max(store_idx),\
                     ObSysVariables::get_info(store_idx),\
                     ObSysVariables::get_flags(store_idx)))) {\
            LOG_WARN("failed to set runtime variable", \
                     KR(ret), K(value), K(sysvar_id), K(store_idx));\
          }\
        }
// Convert macro integer to string for setting into system variable
#define VAR_INT_TO_STRING(buf, value) \
        if (OB_SUCC(ret)) {\
          if (OB_FAIL(databuff_printf(buf, OB_MAX_SYS_PARAM_VALUE_LENGTH, "%d", static_cast<int>(value)))) {\
            LOG_WARN("failed to print value in buf", K(value), K(ret));\
          }\
        }
#define VAR_UINT_TO_STRING(buf, value) \
        if (OB_SUCC(ret)) {\
          if (OB_FAIL(databuff_printf(buf, OB_MAX_SYS_PARAM_VALUE_LENGTH, "%lu", static_cast<uint64_t>(value)))) {\
            LOG_WARN("failed to print value in buf", K(value), K(ret));\
          }\
        }

namespace oceanbase
{
using namespace obcall;
using namespace share;
namespace rootserver
{

int ObRuntimeDDLService::check_inner_stat()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRuntimeDDLService is not inited", KR(ret), K(inited_));
  } else if (OB_ISNULL(ddl_service_)
      || OB_ISNULL(sql_proxy_) || OB_ISNULL(schema_service_)
      || OB_ISNULL(ddl_trans_controller_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null pointer", KR(ret), KP(ddl_service_), KP(sql_proxy_),
        KP(schema_service_), KP(ddl_trans_controller_));
  }
  return ret;
}

#define USE_DDL_FUNCTION(function_name, ...) \
  int ret = OB_SUCCESS; \
  if (OB_ISNULL(ddl_service_)) { \
    ret = OB_NOT_INIT; \
    LOG_WARN("ddl_service_ is null", KR(ret), KP(ddl_service_)); \
  } else if (OB_FAIL(ddl_service_->function_name(__VA_ARGS__))) { \
    LOG_WARN("failed to call " #function_name , KR(ret)); \
  } \
  return ret;

int ObRuntimeDDLService::get_runtime_schema_guard_with_version_in_inner_table(
    share::schema::ObSchemaGetterGuard &schema_guard)
{
  USE_DDL_FUNCTION(get_runtime_schema_guard_with_version_in_inner_table, schema_guard);
}

int ObRuntimeDDLService::publish_schema()
{
  USE_DDL_FUNCTION(publish_schema, );
}

#undef USE_DDL_FUNCTION

int ObRuntimeDDLService::init_runtime_sys_stats_(ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  int64_t start = ObTimeUtility::current_time();
  ObSysStat sys_stat;
  if (OB_FAIL(sys_stat.set_initial_values())) {
    LOG_WARN("set initial values failed", K(ret));
  } else if (sys_stat.item_list_.is_empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("not system stat item", KR(ret));
  } else if (OB_FAIL(replace_sys_stat(sys_stat, trans))) {
    LOG_WARN("replace system stat failed", K(ret));
  }
  LOG_INFO("init sys stat", K(ret),
           "cost", ObTimeUtility::current_time() - start);
  return ret;
}

int ObRuntimeDDLService::replace_sys_stat(ObSysStat &sys_stat,
                                    ObISQLClient &trans)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;

  if (sys_stat.item_list_.is_empty()) {
    // skip
  } else if (OB_FAIL(sql.assign_fmt("INSERT INTO %s "
      "(NAME, DATA_TYPE, VALUE, INFO, gmt_modified) VALUES ",
      OB_ALL_SYS_STAT_TNAME))) {
    LOG_WARN("sql append failed", K(ret));
  } else {
    DLIST_FOREACH_X(it, sys_stat.item_list_, OB_SUCC(ret)) {
      if (OB_ISNULL(it)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("it is null", K(ret));
      } else {
        char buf[2L<<10] = "";
        int64_t pos = 0;
        if (OB_FAIL(it->value_.print_sql_literal(
                      buf, sizeof(buf), pos))) {
          LOG_WARN("print obj failed", K(ret), "obj", it->value_);
        } else {
          ObString value(pos, buf);
          uint64_t schema_id = OB_INVALID_ID;
          if (OB_FAIL(ObMaxIdFetcher::str_to_uint(value, schema_id))) {
            LOG_WARN("fail to convert str to uint", K(ret), K(value));
          } else if (FALSE_IT(schema_id = ObSchemaUtils::get_extract_schema_id(schema_id))) {
          } else if (OB_FAIL(sql.append_fmt("%s('%s', %d, '%ld', '%s', now())",
              (it == sys_stat.item_list_.get_first()) ? "" : ", ",
              it->name_, it->value_.get_type(),
              static_cast<int64_t>(schema_id),
              it->info_))) {
            LOG_WARN("sql append failed", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      LOG_INFO("create system stat sql", K(sql));
      int64_t affected_rows = 0;
      if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
        LOG_WARN("execute sql failed", K(ret), K(sql));
      } else if (sys_stat.item_list_.get_size() != affected_rows
          && sys_stat.item_list_.get_size() != affected_rows / 2) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected affected_rows", K(affected_rows),
            "expected", sys_stat.item_list_.get_size());
      }
    }
  }
  return ret;
}

int ObRuntimeDDLService::create_system_runtime(share::schema::ObServerRuntimeSchema &runtime_schema)
{
  int ret = OB_SUCCESS;
  ObDDLSQLTransaction trans(schema_service_, true, false, false, false);
  ObSchemaService *schema_service = NULL;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("variable is not init");
  } else {
    ObDDLOperator ddl_operator(*schema_service_, *sql_proxy_);
    schema_service = schema_service_->get_schema_service();
    if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_SYS;
      LOG_ERROR("schema_service must not null", K(ret));
    } else {
      ObSchemaStatusProxy *schema_status_proxy = GCTX.schema_status_proxy_;
      ObRefreshSchemaStatus runtime_status(OB_INVALID_TIMESTAMP, OB_INVALID_VERSION);
      ObSysVariableSchema sys_variable;

      const ObSchemaOperationType operation_type = OB_DDL_MAX_OP;
      // Bootstrap writes the initial system-variable state in the same transaction.
      // The update of __all_core_table must be a single-partition transaction.
      int64_t refreshed_schema_version = 0; // won't lock
      if (OB_ISNULL(schema_status_proxy)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema_status_proxy is null", K(ret));
      } else if (OB_FAIL(schema_status_proxy->set_runtime_schema_status(runtime_status))) {
        LOG_WARN("init runtime schema status failed", K(ret), K(runtime_status));
      } else if (OB_FAIL(trans.start(sql_proxy_, refreshed_schema_version))) {
        LOG_WARN("start transaction failed", KR(ret));
      } else if (OB_FAIL(ddl_operator.initialize_runtime_schema(runtime_schema))) {
        LOG_WARN("initialize runtime schema failed", K(runtime_schema), K(ret));
      } else if (OB_FAIL(init_system_variables(runtime_schema, sys_variable))) {
        LOG_WARN("fail to initialize system variables", K(ret), K(runtime_schema));
      } else if (OB_FAIL(ddl_operator.replace_sys_variable(
              sys_variable, runtime_schema.get_schema_version(), trans, operation_type))) {
        LOG_WARN("fail to replace sys variable", K(ret), K(sys_variable));
      } else if (OB_FAIL(ddl_operator.init_runtime_schemas(runtime_schema, sys_variable, trans))) {
        LOG_WARN("init runtime schemas failed", K(runtime_schema), K(ret));
      } else if (OB_FAIL(init_runtime_sys_stats_(trans))) {
        LOG_WARN("insert default sys stats failed", K(ret));
      } else if (OB_FAIL(insert_global_merge_info_(trans))) {
        LOG_WARN("fail to insert global merge info", KR(ret));
      }
      if (trans.is_started()) {
        int temp_ret = OB_SUCCESS;
        LOG_INFO("finish runtime bootstrap transaction", "is_commit", OB_SUCCESS == ret, K(ret));
        if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
          ret = (OB_SUCC(ret)) ? temp_ret : ret;
          LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        }
      }
    }
  }
  return ret;
}

int ObRuntimeDDLService::insert_global_merge_info_(ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  HEAP_VAR(ObGlobalMergeInfo, global_info) {
    if (OB_FAIL(ObGlobalMergeTableOperator::insert_global_merge_info(trans, global_info))) {
      LOG_WARN("fail to insert global merge info", KR(ret), K(global_info));
    }
  }

  return ret;
}

int ObRuntimeDDLService::init(
    ObDDLService &ddl_service,
    common::ObMySQLProxy &sql_proxy,
    share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  ddl_service_ = &ddl_service;
  sql_proxy_ = &sql_proxy;
  schema_service_ = &schema_service;
  ddl_trans_controller_ = &schema_service.get_ddl_trans_controller();
  inited_ = true;
  stopped_ = false;
  return ret;
}

int ObRuntimeDDLService::init_system_variables(
    const ObServerRuntimeSchema &runtime_schema,
    ObSysVariableSchema &sys_variable_schema)
{
  int ret = OB_SUCCESS;
  const int64_t params_capacity = share::ObSysVarMeta::ALL_SYS_VARS_COUNT;
  int64_t var_amount = ObSysVariables::get_amount();

  ObMalloc alloc(ObModIds::OB_TEMP_VARIABLES);
  ObPtrGuard<ObSysParam, share::ObSysVarMeta::ALL_SYS_VARS_COUNT> sys_params_guard(alloc);
  sys_variable_schema.reset();

  ObSysParam *sys_params = NULL;
  if (OB_ISNULL(schema_service_)
             || OB_ISNULL(sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ptr is null", KR(ret), KP_(schema_service), KP_(sql_proxy));
  } else if (OB_FAIL(sys_params_guard.init())) {
    LOG_WARN("alloc sys parameters failed", KR(ret));
  } else if (FALSE_IT(sys_params = sys_params_guard.ptr())) {
  } else if (OB_ISNULL(sys_params) || OB_UNLIKELY(var_amount > params_capacity)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(sys_params), K(params_capacity), K(var_amount));
  } else {
    HEAP_VAR(char[OB_MAX_SYS_PARAM_VALUE_LENGTH], val_buf) {
      sys_variable_schema.set_name_case_mode(OB_LOWERCASE_AND_INSENSITIVE);

      // init default values
      for (int64_t i = 0; OB_SUCC(ret) && i < var_amount; ++i) {
        if (OB_FAIL(sys_params[i].init(ObSysVariables::get_name(i),
                                       ObSysVariables::get_type(i),
                                       ObSysVariables::get_value(i),
                                       ObSysVariables::get_min(i),
                                       ObSysVariables::get_max(i),
                                       ObSysVariables::get_info(i),
                                       ObSysVariables::get_flags(i)))) {
          LOG_WARN("fail to init param", KR(ret), K(1UL), K(i));
        }
      }

      if (OB_SUCC(ret)) {
        ObString read_only_value = "0";
        SET_RUNTIME_VARIABLE(SYS_VAR_READ_ONLY, read_only_value);
      }

      // Derive the default PX thread target from the local server CPU floor.
      int64_t default_px_thread_count = 0;
      if (OB_SUCC(ret)) {
        const int64_t server_default_min_cpu =
            static_cast<int64_t>(GCONF.get_server_default_min_cpu());
        default_px_thread_count = std::max(
            static_cast<int64_t>(3),
            server_default_min_cpu * GCONF.px_workers_per_cpu_quota);
      }

      if (OB_SUCC(ret) && default_px_thread_count > 0) {
        // target cannot be less than 3, otherwise any px query will not come in
        int64_t default_px_servers_target = std::max(static_cast<int64_t>(3), static_cast<int64_t>(default_px_thread_count));
        VAR_INT_TO_STRING(val_buf, default_px_servers_target);
        SET_RUNTIME_VARIABLE(SYS_VAR_PARALLEL_SERVERS_TARGET, val_buf);
      }

      if (FAILEDx(update_mysql_runtime_sys_var(
          runtime_schema, sys_params, params_capacity))) {
        LOG_WARN("failed to update_mysql_runtime_sys_var",
                 KR(ret), K(runtime_schema), K(sys_variable_schema));
      } else if (OB_FAIL(update_special_runtime_sys_var(
                 sys_variable_schema, sys_params, params_capacity))) {
        LOG_WARN("failed to update_special_runtime_sys_var", K(ret), K(sys_variable_schema));
      }

      // set sys_variable
      if (OB_SUCC(ret)) {
        ObSysVarSchema sysvar_schema;
        for (int64_t i = 0; OB_SUCC(ret) && i < var_amount; i++) {
          sysvar_schema.reset();
          if (OB_FAIL(ObSchemaUtils::convert_sys_param_to_sysvar_schema(sys_params[i], sysvar_schema))) {
            LOG_WARN("convert to sysvar schema failed", K(ret));
          } else if (OB_FAIL(sys_variable_schema.add_sysvar_schema(sysvar_schema))) {
            LOG_WARN("add system variable failed", K(ret));
          }
        } //end for
      }
    } // end HEAP_VAR
  }
  return ret;
}

int ObRuntimeDDLService::update_mysql_runtime_sys_var(
    const ObServerRuntimeSchema &runtime_schema,
    ObSysParam *sys_params,
    int64_t params_capacity)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sys_params) || OB_UNLIKELY(params_capacity < share::ObSysVarMeta::ALL_SYS_VARS_COUNT)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(sys_params), K(params_capacity));
  } else {
    // seekdb is MySQL-only: initialize the server charset and collation.
    HEAP_VAR(char[OB_MAX_SYS_PARAM_VALUE_LENGTH], val_buf) {
      VAR_INT_TO_STRING(val_buf, runtime_schema.get_collation_type());
      // set collation and char set
      SET_RUNTIME_VARIABLE(SYS_VAR_COLLATION_DATABASE, val_buf);
      SET_RUNTIME_VARIABLE(SYS_VAR_COLLATION_SERVER, val_buf);
      SET_RUNTIME_VARIABLE(SYS_VAR_CHARACTER_SET_DATABASE, val_buf);
      SET_RUNTIME_VARIABLE(SYS_VAR_CHARACTER_SET_SERVER, val_buf);
    } // end HEAP_VAR
  }
  return ret;
}

// Initialize server-wide system variables.
int ObRuntimeDDLService::update_special_runtime_sys_var(
    const ObSysVariableSchema &sys_variable_schema,
    ObSysParam *sys_params,
    int64_t params_capacity)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sys_params) || OB_UNLIKELY(params_capacity < share::ObSysVarMeta::ALL_SYS_VARS_COUNT)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(sys_params), K(params_capacity));
  } else {
    HEAP_VAR(char[OB_MAX_SYS_PARAM_VALUE_LENGTH], val_buf) {
      {
        VAR_INT_TO_STRING(val_buf, sys_variable_schema.get_name_case_mode());
        SET_RUNTIME_VARIABLE(SYS_VAR_LOWER_CASE_TABLE_NAMES, val_buf);

        OZ(databuff_printf(val_buf, OB_MAX_SYS_PARAM_VALUE_LENGTH, "%s", OB_SYS_HOST_NAME));
        SET_RUNTIME_VARIABLE(SYS_VAR_OB_TCP_INVITED_NODES, val_buf);
      }
    } // end HEAP_VAR
  }
  return ret;
}

}
}
