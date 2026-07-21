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

#include "ob_timezone_importer.h"
#include "lib/string/ob_sql_string.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_server_struct.h"
#include "share/system_variable/ob_system_variable_alias.h"
#include "sql/engine/ob_exec_context.h"

#define USING_LOG_PREFIX SERVER

namespace oceanbase
{
using namespace share;
using namespace sql;
using namespace common;
using namespace obcall;
namespace table
{

int ObTimezoneImporter::exec_op(table::ObModuleDataArg op)
{
  int ret = OB_SUCCESS;
  if (op.op_ == ObModuleDataArg::LOAD_INFO) {
    if (OB_FAIL(import_timezone_info(op.file_path_))) {
      LOG_WARN("import timezone info failed", K(ret));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support check timezone info", K(ret), K(op));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "check timezone is");
  }
  return ret;
}

ERRSIM_POINT_DEF(EN_LOAD_TIME_ZONE_INFO_FAILED);
int ObTimezoneImporter::import_timezone_info(const ObString &file_path)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  char *buf = NULL;
  common::ObMySQLProxy *sql_proxy = NULL;
  ObMySQLTransaction trans;
  int64_t affected_rows = 0;
  if (OB_ISNULL(sql_proxy = exec_ctx_.get_sql_proxy())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy must not null", K(ret), KP(sql_proxy));
  } else if (OB_FAIL(trans.start(sql_proxy))) {
    LOG_WARN("fail to start transaction", K(ret));
  } else {
    ObSqlString trunc_name_sql;
    ObSqlString trunc_transition_sql;
    ObSqlString trunc_transition_type_sql;
    if (OB_FAIL(trunc_name_sql.assign_fmt("DELETE FROM %s", OB_ALL_TIME_ZONE_NAME_TNAME))) {
      LOG_WARN("assign fmt failed", K(ret));
    } else if (OB_FAIL(trunc_transition_sql.assign_fmt("DELETE FROM %s", OB_ALL_TIME_ZONE_TRANSITION_TNAME))) {
      LOG_WARN("assign fmt failed", K(ret));
    } else if (OB_FAIL(trunc_transition_type_sql.assign_fmt("DELETE FROM %s", OB_ALL_TIME_ZONE_TRANSITION_TYPE_TNAME))) {
      LOG_WARN("assign fmt failed", K(ret));
    } else if (OB_FAIL(trans.write(trunc_name_sql.ptr(), affected_rows))) {
      LOG_WARN("write failed", K(ret));
    } else if (OB_FAIL(trans.write(trunc_transition_sql.ptr(), affected_rows))) {
      LOG_WARN("write failed", K(ret));
    } else if (OB_FAIL(trans.write(trunc_transition_type_sql.ptr(), affected_rows))) {
      LOG_WARN("write failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    ObSqlString load_name_sql;
    ObSqlString load_transition_sql;
    ObSqlString load_transition_type_sql;
    ObSqlString update_version_sql;
    const char *timezone_name_file = "timezone_name.data";
    const char *timezone_transition_file = "timezone_trans.data";
    const char *timezone_transition_type_file = "timezone_trans_type.data";
    if (OB_FAIL(load_name_sql.assign_fmt("LOAD DATA INFILE '%.*s/%s' INTO TABLE %s FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"'",
                file_path.length(), file_path.ptr(), timezone_name_file, OB_ALL_TIME_ZONE_NAME_TNAME))) {
      LOG_WARN("assign fmt failed", K(ret));
    } else if (OB_FAIL(load_transition_sql.assign_fmt("LOAD DATA INFILE '%.*s/%s' INTO TABLE %s FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"'",
                file_path.length(), file_path.ptr(), timezone_transition_file, OB_ALL_TIME_ZONE_TRANSITION_TNAME))) {
      LOG_WARN("assign fmt failed", K(ret));
    } else if (OB_FAIL(load_transition_type_sql.assign_fmt("LOAD DATA INFILE '%.*s/%s' INTO TABLE %s FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"'",
                file_path.length(), file_path.ptr(), timezone_transition_type_file, OB_ALL_TIME_ZONE_TRANSITION_TYPE_TNAME))) {
      LOG_WARN("assign fmt failed", K(ret));
    } else if (OB_FAIL(update_version_sql.assign_fmt(
                   "INSERT INTO %s (data_type, name, value, info) "
                   "VALUES (5, 'current_timezone_version', 1, 'current time zone version') "
                   "ON DUPLICATE KEY UPDATE value = CAST(value AS SIGNED) + 1",
                   OB_ALL_SYS_STAT_TNAME))) {
      LOG_WARN("assign fmt failed", K(ret));
    } else if (OB_FAIL(trans.write(load_name_sql.ptr(), affected_rows))) {
      LOG_WARN("write failed", K(ret));
    } else if (OB_FAIL(EN_LOAD_TIME_ZONE_INFO_FAILED)) {
      LOG_WARN("load time zone info failed due to trace point", K(ret));
    } else if (OB_FAIL(trans.write(load_transition_sql.ptr(), affected_rows))) {
      LOG_WARN("write failed", K(ret));
    } else if (OB_FAIL(trans.write(load_transition_type_sql.ptr(), affected_rows))) {
      LOG_WARN("write failed", K(ret));
    } else if (OB_FAIL(trans.write(update_version_sql.ptr(), affected_rows))) {
      LOG_WARN("update timezone version failed", K(ret));
    }
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_ERROR("failed to commit trans", KR(ret), KR(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  return ret;
}

}  // end namespace table
}  // namespace oceanbase
