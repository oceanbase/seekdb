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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_inner_table_schema.h"

#include "share/schema/ob_schema_macro_define.h"
#include "share/schema/ob_schema_service_sql_impl.h"
#include "share/schema/ob_table_schema.h"
#include "share/scn.h"

namespace oceanbase
{
using namespace share::schema;
using namespace common;
namespace share
{

int ObInnerTableSchema::dba_ob_sys_variables_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_SYS_VARIABLES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_SYS_VARIABLES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT     a.GMT_CREATE AS CREATE_TIME,     a.GMT_MODIFIED AS MODIFY_TIME,     a.NAME as NAME,     a.VALUE as VALUE,     a.MIN_VAL as MIN_VALUE,     a.MAX_VAL as MAX_VALUE,     CASE a.FLAGS & 0x3         WHEN 1 THEN "GLOBAL_ONLY"         WHEN 2 THEN "SESSION_ONLY"         WHEN 3 THEN "GLOBAL | SESSION"         ELSE NULL     END as SCOPE,     a.INFO as INFO,     b.DEFAULT_VALUE as DEFAULT_VALUE,     CAST (CASE WHEN a.VALUE = b.DEFAULT_VALUE           THEN 'YES'           ELSE 'NO'           END AS CHAR(3)) AS ISDEFAULT   FROM oceanbase.__all_sys_variable a   join oceanbase.__all_virtual_sys_variable_default_value b   where a.name = b.variable_name;   )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_wr_sqltext_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_WR_SQLTEXT_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_WR_SQLTEXT_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT       STAT.SNAP_ID AS SNAP_ID,       STAT.SQL_ID AS SQL_ID,       STAT.QUERY_SQL AS QUERY_SQL,       STAT.SQL_TYPE AS SQL_TYPE     FROM     (       oceanbase.__all_virtual_wr_sqltext STAT       JOIN oceanbase.__all_virtual_wr_snapshot SNAP       ON STAT.CLUSTER_ID = SNAP.CLUSTER_ID       AND STAT.SNAP_ID = SNAP.SNAP_ID     )     WHERE       SNAP.STATUS = 0   )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_wr_sqltext_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_WR_SQLTEXT_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_WR_SQLTEXT_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT       STAT.SNAP_ID AS SNAP_ID,       STAT.SQL_ID AS SQL_ID,       STAT.QUERY_SQL AS QUERY_SQL,       STAT.SQL_TYPE AS SQL_TYPE     FROM     (       oceanbase.__all_virtual_wr_sqltext STAT       JOIN oceanbase.__all_virtual_wr_snapshot SNAP       ON STAT.CLUSTER_ID = SNAP.CLUSTER_ID       AND STAT.SNAP_ID = SNAP.SNAP_ID     )     WHERE       SNAP.STATUS = 0   )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::gv_ob_active_session_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_ACTIVE_SESSION_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_ACTIVE_SESSION_HISTORY_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT       CAST(SAMPLE_ID AS SIGNED) AS SAMPLE_ID,       SAMPLE_TIME AS SAMPLE_TIME,       CAST(1 AS SIGNED) AS CON_ID,       CAST(USER_ID AS SIGNED) AS USER_ID,       CAST(SESSION_ID AS SIGNED) AS SESSION_ID,       CAST(IF (SESSION_TYPE = 0, 'FOREGROUND', 'BACKGROUND') AS CHAR(10)) AS SESSION_TYPE,       CAST(IF (EVENT_NO = 0, 'ON CPU', 'WAITING') AS CHAR(7)) AS SESSION_STATE,       CAST(SQL_ID AS CHAR(32)) AS SQL_ID,       CAST(PLAN_ID AS SIGNED) AS PLAN_ID,       CAST(TRACE_ID AS CHAR(64)) AS TRACE_ID,       CAST(NAME AS CHAR(64)) AS EVENT,       CAST(EVENT_NO AS SIGNED) AS EVENT_NO,       CAST(ASH.EVENT_ID AS SIGNED) AS EVENT_ID,       CAST(PARAMETER1 AS CHAR(64)) AS P1TEXT,       CAST(P1 AS SIGNED) AS P1,       CAST(PARAMETER2 AS CHAR(64)) AS P2TEXT,       CAST(P2 AS SIGNED) AS P2,       CAST(PARAMETER3 AS CHAR(64)) AS P3TEXT,       CAST(P3 AS SIGNED) AS P3,       CAST(WAIT_CLASS AS CHAR(64)) AS WAIT_CLASS,       CAST(WAIT_CLASS_ID AS SIGNED) AS WAIT_CLASS_ID,       CAST(TIME_WAITED AS SIGNED) AS TIME_WAITED,       CAST(SQL_PLAN_LINE_ID AS SIGNED) SQL_PLAN_LINE_ID,       CAST(GROUP_ID AS SIGNED) GROUP_ID,       CAST(PLAN_HASH AS UNSIGNED) PLAN_HASH,       CAST(THREAD_ID AS SIGNED) THREAD_ID,       CAST(STMT_TYPE AS SIGNED) STMT_TYPE,       CAST(TIME_MODEL AS SIGNED) TIME_MODEL,       CAST(IF (IN_PARSE = 1, 'Y', 'N') AS CHAR(1)) AS IN_PARSE,       CAST(IF (IN_PL_PARSE = 1, 'Y', 'N') AS CHAR(1)) AS IN_PL_PARSE,       CAST(IF (IN_PLAN_CACHE = 1, 'Y', 'N') AS CHAR(1)) AS IN_PLAN_CACHE,       CAST(IF (IN_SQL_OPTIMIZE = 1, 'Y', 'N') AS CHAR(1)) AS IN_SQL_OPTIMIZE,       CAST(IF (IN_SQL_EXECUTION = 1, 'Y', 'N') AS CHAR(1)) AS IN_SQL_EXECUTION,       CAST(IF (IN_PX_EXECUTION = 1, 'Y', 'N') AS CHAR(1)) AS IN_PX_EXECUTION,       CAST(IF (IN_SEQUENCE_LOAD = 1, 'Y', 'N') AS CHAR(1)) AS IN_SEQUENCE_LOAD,       CAST(IF (IN_COMMITTING = 1, 'Y', 'N') AS CHAR(1)) AS IN_COMMITTING,       CAST(IF (IN_STORAGE_READ = 1, 'Y', 'N') AS CHAR(1)) AS IN_STORAGE_READ,       CAST(IF (IN_STORAGE_WRITE = 1, 'Y', 'N') AS CHAR(1)) AS IN_STORAGE_WRITE,       CAST(IF (IN_REMOTE_DAS_EXECUTION = 1, 'Y', 'N') AS CHAR(1)) AS IN_REMOTE_DAS_EXECUTION,       CAST(IF (IN_FILTER_ROWS = 1, 'Y', 'N') AS CHAR(1)) AS IN_FILTER_ROWS,       CAST(CASE WHEN (TIME_MODEL & 16384) > 0 THEN 'Y' ELSE 'N' END AS CHAR(1)) AS IN_RPC_ENCODE,       CAST(CASE WHEN (TIME_MODEL & 32768) > 0 THEN 'Y' ELSE 'N' END AS CHAR(1)) AS IN_RPC_DECODE,       CAST(CASE WHEN (TIME_MODEL & 65536) > 0 THEN 'Y' ELSE 'N' END AS CHAR(1)) AS IN_CONNECTION_MGR,       CAST(PROGRAM AS CHAR(64)) AS PROGRAM,       CAST(MODULE AS CHAR(64)) AS MODULE,       CAST(ACTION AS CHAR(64)) AS ACTION,       CAST(CLIENT_ID AS CHAR(64)) AS CLIENT_ID,       CAST(BACKTRACE AS CHAR(512)) AS BACKTRACE,       CAST(TM_DELTA_TIME AS SIGNED) AS TM_DELTA_TIME,       CAST(TM_DELTA_CPU_TIME AS SIGNED) AS TM_DELTA_CPU_TIME,       CAST(TM_DELTA_DB_TIME AS SIGNED) AS TM_DELTA_DB_TIME,       CAST(TOP_LEVEL_SQL_ID AS CHAR(32)) AS TOP_LEVEL_SQL_ID,       CAST(IF (IN_PLSQL_COMPILATION = 1, 'Y', 'N') AS CHAR(1)) AS IN_PLSQL_COMPILATION,       CAST(IF (IN_PLSQL_EXECUTION = 1, 'Y', 'N') AS CHAR(1)) AS IN_PLSQL_EXECUTION,       CAST(PLSQL_ENTRY_OBJECT_ID AS SIGNED) AS PLSQL_ENTRY_OBJECT_ID,       CAST(PLSQL_ENTRY_SUBPROGRAM_ID AS SIGNED) AS PLSQL_ENTRY_SUBPROGRAM_ID,       CAST(PLSQL_ENTRY_SUBPROGRAM_NAME AS CHAR(32)) AS PLSQL_ENTRY_SUBPROGRAM_NAME,       CAST(PLSQL_OBJECT_ID AS SIGNED) AS PLSQL_OBJECT_ID,       CAST(PLSQL_SUBPROGRAM_ID AS SIGNED) AS PLSQL_SUBPROGRAM_ID,       CAST(PLSQL_SUBPROGRAM_NAME AS CHAR(32)) AS PLSQL_SUBPROGRAM_NAME,       CAST(TX_ID AS SIGNED) AS TX_ID,       CAST(BLOCKING_SESSION_ID AS SIGNED) AS BLOCKING_SESSION_ID,       CAST(TABLET_ID AS SIGNED) AS TABLET_ID,       CAST(PROXY_SID AS SIGNED) AS PROXY_SID,       CAST(DELTA_READ_IO_REQUESTS AS SIGNED) AS DELTA_READ_IO_REQUESTS,       CAST(DELTA_READ_IO_BYTES AS SIGNED) AS DELTA_READ_IO_BYTES,       CAST(DELTA_WRITE_IO_REQUESTS AS SIGNED) AS DELTA_WRITE_IO_REQUESTS,       CAST(DELTA_WRITE_IO_BYTES AS SIGNED) AS DELTA_WRITE_IO_BYTES   FROM oceanbase.__all_virtual_ash ASH LEFT JOIN oceanbase.v$event_name on EVENT_NO = `event#` )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::v_ob_active_session_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_ACTIVE_SESSION_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_ACTIVE_SESSION_HISTORY_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT       SAMPLE_ID,       SAMPLE_TIME,       CON_ID,       USER_ID,       SESSION_ID,       SESSION_TYPE,       SESSION_STATE,       SQL_ID,       PLAN_ID,       TRACE_ID,       EVENT,       EVENT_NO,       EVENT_ID,       P1TEXT,       P1,       P2TEXT,       P2,       P3TEXT,       P3,       WAIT_CLASS,       WAIT_CLASS_ID,       TIME_WAITED,       SQL_PLAN_LINE_ID,       GROUP_ID,       PLAN_HASH,       THREAD_ID,       STMT_TYPE,       TIME_MODEL,       IN_PARSE,       IN_PL_PARSE,       IN_PLAN_CACHE,       IN_SQL_OPTIMIZE,       IN_SQL_EXECUTION,       IN_PX_EXECUTION,       IN_SEQUENCE_LOAD,       IN_COMMITTING,       IN_STORAGE_READ,       IN_STORAGE_WRITE,       IN_REMOTE_DAS_EXECUTION,       IN_FILTER_ROWS,       IN_RPC_ENCODE,       IN_RPC_DECODE,       IN_CONNECTION_MGR,       PROGRAM,       MODULE,       ACTION,       CLIENT_ID,       BACKTRACE,       TM_DELTA_TIME,       TM_DELTA_CPU_TIME,       TM_DELTA_DB_TIME,       TOP_LEVEL_SQL_ID,       IN_PLSQL_COMPILATION,       IN_PLSQL_EXECUTION,       PLSQL_ENTRY_OBJECT_ID,       PLSQL_ENTRY_SUBPROGRAM_ID,       PLSQL_ENTRY_SUBPROGRAM_NAME,       PLSQL_OBJECT_ID,       PLSQL_SUBPROGRAM_ID,       PLSQL_SUBPROGRAM_NAME,       TX_ID,       BLOCKING_SESSION_ID,       TABLET_ID,       PROXY_SID,       DELTA_READ_IO_REQUESTS,       DELTA_READ_IO_BYTES,       DELTA_WRITE_IO_REQUESTS,       DELTA_WRITE_IO_BYTES       FROM oceanbase.GV$OB_ACTIVE_SESSION_HISTORY )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::role_edges_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_MYSQL_SCHEMA_ID);
  table_schema.set_table_id(OB_ROLE_EDGES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_ROLE_EDGES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT cast(from_user.host AS char(255)) FROM_HOST,          cast(from_user.user_name AS char(128)) FROM_USER,          cast(to_user.host AS char(255)) TO_HOST,          cast(to_user.user_name AS char(128)) TO_USER,          cast(CASE role_map.admin_option WHEN 1 THEN 'Y' ELSE 'N' END AS char(1)) WITH_ADMIN_OPTION   FROM oceanbase.__all_tenant_role_grantee_map role_map,        oceanbase.__all_user from_user,        oceanbase.__all_user to_user   WHERE role_map.grantee_id = to_user.user_id     AND role_map.role_id = from_user.user_id; )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::default_roles_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_MYSQL_SCHEMA_ID);
  table_schema.set_table_id(OB_DEFAULT_ROLES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DEFAULT_ROLES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT cast(to_user.host AS char(255)) HOST,          cast(to_user.user_name AS char(128)) USER,          cast(from_user.host AS char(255)) DEFAULT_ROLE_HOST,          cast(from_user.user_name AS char(128)) DEFAULT_ROLE_USER   FROM oceanbase.__all_tenant_role_grantee_map role_map,        oceanbase.__all_user from_user,        oceanbase.__all_user to_user   WHERE role_map.grantee_id = to_user.user_id     AND role_map.role_id = from_user.user_id     AND role_map.disable_flag = 0; )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_index_usage_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_INDEX_USAGE_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_INDEX_USAGE_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       1 AS CON_ID,       CAST(IUT.OBJECT_ID AS SIGNED) AS OBJECT_ID,       CAST(T.TABLE_NAME AS CHAR(128)) AS NAME,       CAST(DB.DATABASE_NAME AS CHAR(128)) AS OWNER,       CAST(IUT.TOTAL_ACCESS_COUNT AS SIGNED) AS TOTAL_ACCESS_COUNT,       CAST(IUT.TOTAL_EXEC_COUNT AS SIGNED) AS TOTAL_EXEC_COUNT,       CAST(IUT.TOTAL_ROWS_RETURNED AS SIGNED) AS TOTAL_ROWS_RETURNED,       CAST(IUT.BUCKET_0_ACCESS_COUNT AS SIGNED) AS BUCKET_0_ACCESS_COUNT,       CAST(IUT.BUCKET_1_ACCESS_COUNT AS SIGNED) AS BUCKET_1_ACCESS_COUNT,       CAST(IUT.BUCKET_2_10_ACCESS_COUNT AS SIGNED) AS BUCKET_2_10_ACCESS_COUNT,       CAST(IUT.BUCKET_2_10_ROWS_RETURNED AS SIGNED) AS BUCKET_2_10_ROWS_RETURNED,       CAST(IUT.BUCKET_11_100_ACCESS_COUNT AS SIGNED) AS BUCKET_11_100_ACCESS_COUNT,       CAST(IUT.BUCKET_11_100_ROWS_RETURNED AS SIGNED) AS BUCKET_11_100_ROWS_RETURNED,       CAST(IUT.BUCKET_101_1000_ACCESS_COUNT AS SIGNED) AS BUCKET_101_1000_ACCESS_COUNT,       CAST(IUT.BUCKET_101_1000_ROWS_RETURNED AS SIGNED) AS BUCKET_101_1000_ROWS_RETURNED,       CAST(IUT.BUCKET_1000_PLUS_ACCESS_COUNT AS SIGNED) AS BUCKET_1000_PLUS_ACCESS_COUNT,       CAST(IUT.BUCKET_1000_PLUS_ROWS_RETURNED AS SIGNED) AS BUCKET_1000_PLUS_ROWS_RETURNED,       CAST(IUT.LAST_USED AS CHAR(128)) AS LAST_USED     FROM       oceanbase.__all_virtual_index_usage_info IUT       JOIN oceanbase.__all_virtual_table T       ON IUT.OBJECT_ID = T.TABLE_ID       JOIN oceanbase.__all_virtual_database DB       ON t.DATABASE_ID = DB.DATABASE_ID     WHERE T.TABLE_ID = IUT.OBJECT_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::columns_priv_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_MYSQL_SCHEMA_ID);
  table_schema.set_table_id(OB_COLUMNS_PRIV_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_COLUMNS_PRIV_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT cast(b.host as char(255)) as Host,            cast(a.database_name as char(128)) as Db,            cast(b.user_name as char(128)) as User,            cast(a.table_name as char(128)) as Table_name,            cast(a.column_name as char(128)) as Column_name,            substr(concat(case when (a.all_priv & 1) > 0 then ',Select' else '' end,                           case when (a.all_priv & 2) > 0 then ',Insert' else '' end,                           case when (a.all_priv & 4) > 0 then ',Update' else '' end,                           case when (a.all_priv & 8) > 0 then ',References' else '' end), 2) as Column_priv,            cast(a.gmt_modified as datetime) as Timestamp     FROM oceanbase.__all_column_privilege a, oceanbase.__all_user b     WHERE a.user_id = b.user_id )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mview_logs_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVIEW_LOGS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVIEW_LOGS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS LOG_OWNER,       CAST(D.TABLE_NAME AS CHAR(128)) AS MASTER,       CAST(B.TABLE_NAME AS CHAR(128)) AS LOG_TABLE,       CAST(NULL AS CHAR(128)) AS LOG_TRIGGER,       CAST(IF(D.TABLE_MODE & 66048 = 66048, 'YES', 'NO') AS  CHAR(3)) AS ROWIDS,       CAST(IF(D.TABLE_MODE & 66048 = 0, 'YES', 'NO') AS  CHAR(3)) AS PRIMARY_KEY,       CAST('NO' AS CHAR(3)) AS OBJECT_ID,       CAST(         IF((           SELECT COUNT(*)             FROM oceanbase.__all_virtual_column C1,                  oceanbase.__all_virtual_column C2             WHERE B.TENANT_ID = C1.TENANT_ID               AND B.TABLE_ID = C1.TABLE_ID               AND C1.COLUMN_ID >= 16               AND C1.COLUMN_ID < 65520               AND D.TENANT_ID = C2.TENANT_ID               AND D.TABLE_ID = C2.TABLE_ID               AND C2.ROWKEY_POSITION != 0               AND C1.COLUMN_ID != C2.COLUMN_ID           ) = 0, 'NO', 'YES') AS CHAR(3)       ) AS FILTER_COLUMNS,       CAST('YES' AS CHAR(3)) AS SEQUENCE,       CAST('YES' AS CHAR(3)) AS INCLUDE_NEW_VALUES,       CAST(IF(C.PURGE_MODE = 1, 'YES', 'NO') AS CHAR(3)) AS PURGE_ASYNCHRONOUS,       CAST(IF(C.PURGE_MODE = 2, 'YES', 'NO') AS CHAR(3)) AS PURGE_DEFERRED,       CAST(C.PURGE_START AS DATETIME) AS PURGE_START,       CAST(C.PURGE_NEXT AS CHAR(200)) AS PURGE_INTERVAL,       CAST(C.LAST_PURGE_DATE AS DATETIME) AS LAST_PURGE_DATE,       CAST(0 AS SIGNED) AS LAST_PURGE_STATUS,       C.LAST_PURGE_ROWS AS NUM_ROWS_PURGED,       CAST('YES' AS CHAR(3)) AS COMMIT_SCN_BASED,       CAST('NO' AS CHAR(3)) AS STAGING_LOG,       B.DOP AS PURGE_DOP,       C.LAST_PURGE_TIME AS LAST_PURGE_TIME     FROM       oceanbase.__all_virtual_database A,       oceanbase.__all_virtual_table B,       oceanbase.__all_virtual_mlog C,       oceanbase.__all_virtual_table D     WHERE A.TENANT_ID = B.TENANT_ID       AND A.DATABASE_ID = B.DATABASE_ID       AND B.TENANT_ID = D.TENANT_ID       AND C.TENANT_ID = D.TENANT_ID       AND B.TABLE_ID = C.MLOG_ID       AND B.TABLE_TYPE = 15       AND B.DATA_TABLE_ID = D.TABLE_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mview_logs_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVIEW_LOGS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVIEW_LOGS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS LOG_OWNER,       CAST(D.TABLE_NAME AS CHAR(128)) AS MASTER,       CAST(B.TABLE_NAME AS CHAR(128)) AS LOG_TABLE,       CAST(NULL AS CHAR(128)) AS LOG_TRIGGER,       CAST(IF(D.TABLE_MODE & 66048 = 66048, 'YES', 'NO') AS  CHAR(3)) AS ROWIDS,       CAST(IF(D.TABLE_MODE & 66048 = 0, 'YES', 'NO') AS  CHAR(3)) AS PRIMARY_KEY,       CAST('NO' AS CHAR(3)) AS OBJECT_ID,       CAST(         IF((           SELECT COUNT(*)             FROM oceanbase.__all_column C1,                  oceanbase.__all_column C2             WHERE               B.TABLE_ID = C1.TABLE_ID               AND C1.COLUMN_ID >= 16               AND C1.COLUMN_ID < 65520               AND D.TABLE_ID = C2.TABLE_ID               AND C2.ROWKEY_POSITION != 0               AND C1.COLUMN_ID != C2.COLUMN_ID           ) = 0, 'NO', 'YES') AS CHAR(3)       ) AS FILTER_COLUMNS,       CAST('YES' AS CHAR(3)) AS SEQUENCE,       CAST('YES' AS CHAR(3)) AS INCLUDE_NEW_VALUES,       CAST(IF(C.PURGE_MODE = 1, 'YES', 'NO') AS CHAR(3)) AS PURGE_ASYNCHRONOUS,       CAST(IF(C.PURGE_MODE = 2, 'YES', 'NO') AS CHAR(3)) AS PURGE_DEFERRED,       CAST(C.PURGE_START AS DATETIME) AS PURGE_START,       CAST(C.PURGE_NEXT AS CHAR(200)) AS PURGE_INTERVAL,       CAST(C.LAST_PURGE_DATE AS DATETIME) AS LAST_PURGE_DATE,       CAST(0 AS SIGNED) AS LAST_PURGE_STATUS,       C.LAST_PURGE_ROWS AS NUM_ROWS_PURGED,       CAST('YES' AS CHAR(3)) AS COMMIT_SCN_BASED,       CAST('NO' AS CHAR(3)) AS STAGING_LOG,       B.DOP AS PURGE_DOP,       C.LAST_PURGE_TIME AS LAST_PURGE_TIME     FROM       oceanbase.__all_database A,       oceanbase.__all_table B,       oceanbase.__all_mlog C,       oceanbase.__all_table D     WHERE       A.DATABASE_ID = B.DATABASE_ID       AND B.TABLE_ID = C.MLOG_ID       AND B.TABLE_TYPE = 15       AND B.DATA_TABLE_ID = D.TABLE_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mviews_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVIEWS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVIEWS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MVIEW_NAME,       CAST(D.TABLE_NAME AS CHAR(128)) AS CONTAINER_NAME,       B.VIEW_DEFINITION AS QUERY,       CAST(LENGTH(B.VIEW_DEFINITION) AS SIGNED) AS QUERY_LEN,       CAST('N' AS CHAR(1)) AS UPDATABLE,       CAST(NULL AS CHAR(128)) AS UPDATE_LOG,       CAST(NULL AS CHAR(128)) AS MASTER_ROLLBACK_SEG,       CAST(NULL AS CHAR(128)) AS MASTER_LINK,       CAST(         CASE ((B.TABLE_MODE >> 27) & 1)           WHEN 0 THEN 'N'           WHEN 1 THEN 'Y'           ELSE NULL         END AS CHAR(1)       ) AS REWRITE_ENABLED,       CAST(NULL AS CHAR(9)) AS REWRITE_CAPABILITY,       CAST(         CASE C.REFRESH_MODE           WHEN 0 THEN 'NEVER'           WHEN 1 THEN 'DEMAND'           WHEN 2 THEN 'COMMIT'           WHEN 3 THEN 'STATEMENT'           WHEN 4 THEN 'MAJOR_COMPACTION'           ELSE NULL         END AS CHAR(32)       ) AS REFRESH_MODE,       CAST(         CASE C.REFRESH_METHOD           WHEN 0 THEN 'NEVER'           WHEN 1 THEN 'COMPLETE'           WHEN 2 THEN 'FAST'           WHEN 3 THEN 'FORCE'           ELSE NULL         END AS CHAR(8)       ) AS REFRESH_METHOD,       CAST(         CASE C.BUILD_MODE           WHEN 0 THEN 'IMMEDIATE'           WHEN 1 THEN 'DEFERRED'           WHEN 2 THEN 'PERBUILT'           ELSE NULL         END AS CHAR(9)       ) AS BUILD_MODE,       CAST(NULL AS CHAR(18)) AS FAST_REFRESHABLE,       CAST(         CASE C.LAST_REFRESH_TYPE           WHEN 0 THEN 'COMPLETE'           WHEN 1 THEN 'FAST'           ELSE 'NA'         END AS CHAR(8)       ) AS LAST_REFRESH_TYPE,       CAST(C.LAST_REFRESH_DATE AS DATETIME) AS LAST_REFRESH_DATE,       CAST(DATE_ADD(C.LAST_REFRESH_DATE, INTERVAL C.LAST_REFRESH_TIME SECOND) AS DATETIME) AS LAST_REFRESH_END_TIME,       CAST(NULL AS CHAR(19)) AS STALENESS,       CAST(NULL AS CHAR(19)) AS AFTER_FAST_REFRESH,       CAST(IF(C.BUILD_MODE = 2, 'Y', 'N') AS CHAR(1)) AS UNKNOWN_PREBUILT,       CAST('N' AS CHAR(1)) AS UNKNOWN_PLSQL_FUNC,       CAST('N' AS CHAR(1)) AS UNKNOWN_EXTERNAL_TABLE,       CAST('N' AS CHAR(1)) AS UNKNOWN_CONSIDER_FRESH,       CAST('N' AS CHAR(1)) AS UNKNOWN_IMPORT,       CAST('N' AS CHAR(1)) AS UNKNOWN_TRUSTED_FD,       CAST(NULL AS CHAR(19)) AS COMPILE_STATE,       CAST('Y' AS CHAR(1)) AS USE_NO_INDEX,       CAST(NULL AS DATETIME) AS STALE_SINCE,       CAST(NULL AS SIGNED) AS NUM_PCT_TABLES,       CAST(NULL AS SIGNED) AS NUM_FRESH_PCT_REGIONS,       CAST(NULL AS SIGNED) AS NUM_STALE_PCT_REGIONS,       CAST('NO' AS CHAR(3)) AS SEGMENT_CREATED,       CAST(NULL AS CHAR(128)) AS EVALUATION_EDITION,       CAST(NULL AS CHAR(128)) AS UNUSABLE_BEFORE,       CAST(NULL AS CHAR(128)) AS UNUSABLE_BEGINNING,       CAST(NULL AS CHAR(100)) AS DEFAULT_COLLATION,       CAST(         CASE ((B.TABLE_MODE >> 28) & 1)           WHEN 0 THEN 'N'           WHEN 1 THEN 'Y'           ELSE NULL         END AS CHAR(1)       ) AS ON_QUERY_COMPUTATION,       C.REFRESH_DOP AS REFRESH_DOP,       C.data_sync_scn AS DATA_SYNC_SCN,       CAST(         CASE C.data_sync_scn           WHEN 0 THEN 'NOT AVAILABLE'           ELSE TIMESTAMPDIFF(SECOND, SCN_TO_TIMESTAMP(C.data_sync_scn), NOW())         END AS CHAR(128)       ) AS DATA_SYNC_DELAY     FROM       oceanbase.__all_virtual_database A,       oceanbase.__all_virtual_table B,       oceanbase.__all_virtual_mview C,       oceanbase.__all_virtual_table D     WHERE A.DATABASE_ID = B.DATABASE_ID       AND B.TABLE_ID = C.MVIEW_ID       AND B.TABLE_TYPE = 7       AND B.DATA_TABLE_ID = D.TABLE_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mviews_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVIEWS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVIEWS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MVIEW_NAME,       CAST(D.TABLE_NAME AS CHAR(128)) AS CONTAINER_NAME,       B.VIEW_DEFINITION AS QUERY,       CAST(LENGTH(B.VIEW_DEFINITION) AS SIGNED) AS QUERY_LEN,       CAST('N' AS CHAR(1)) AS UPDATABLE,       CAST(NULL AS CHAR(128)) AS UPDATE_LOG,       CAST(NULL AS CHAR(128)) AS MASTER_ROLLBACK_SEG,       CAST(NULL AS CHAR(128)) AS MASTER_LINK,       CAST(         CASE ((B.TABLE_MODE >> 27) & 1)           WHEN 0 THEN 'N'           WHEN 1 THEN 'Y'           ELSE NULL         END AS CHAR(1)       ) AS REWRITE_ENABLED,       CAST(NULL AS CHAR(9)) AS REWRITE_CAPABILITY,       CAST(         CASE C.REFRESH_MODE           WHEN 0 THEN 'NEVER'           WHEN 1 THEN 'DEMAND'           WHEN 2 THEN 'COMMIT'           WHEN 3 THEN 'STATEMENT'           WHEN 4 THEN 'MAJOR_COMPACTION'           ELSE NULL         END AS CHAR(32)       ) AS REFRESH_MODE,       CAST(         CASE C.REFRESH_METHOD           WHEN 0 THEN 'NEVER'           WHEN 1 THEN 'COMPLETE'           WHEN 2 THEN 'FAST'           WHEN 3 THEN 'FORCE'           ELSE NULL         END AS CHAR(8)       ) AS REFRESH_METHOD,       CAST(         CASE C.BUILD_MODE           WHEN 0 THEN 'IMMEDIATE'           WHEN 1 THEN 'DEFERRED'           WHEN 2 THEN 'PERBUILT'           ELSE NULL         END AS CHAR(9)       ) AS BUILD_MODE,       CAST(NULL AS CHAR(18)) AS FAST_REFRESHABLE,       CAST(         CASE C.LAST_REFRESH_TYPE           WHEN 0 THEN 'COMPLETE'           WHEN 1 THEN 'FAST'           ELSE 'NA'         END AS CHAR(8)       ) AS LAST_REFRESH_TYPE,       CAST(C.LAST_REFRESH_DATE AS DATETIME) AS LAST_REFRESH_DATE,       CAST(DATE_ADD(C.LAST_REFRESH_DATE, INTERVAL C.LAST_REFRESH_TIME SECOND) AS DATETIME) AS LAST_REFRESH_END_TIME,       CAST(NULL AS CHAR(19)) AS STALENESS,       CAST(NULL AS CHAR(19)) AS AFTER_FAST_REFRESH,       CAST(IF(C.BUILD_MODE = 2, 'Y', 'N') AS CHAR(1)) AS UNKNOWN_PREBUILT,       CAST('N' AS CHAR(1)) AS UNKNOWN_PLSQL_FUNC,       CAST('N' AS CHAR(1)) AS UNKNOWN_EXTERNAL_TABLE,       CAST('N' AS CHAR(1)) AS UNKNOWN_CONSIDER_FRESH,       CAST('N' AS CHAR(1)) AS UNKNOWN_IMPORT,       CAST('N' AS CHAR(1)) AS UNKNOWN_TRUSTED_FD,       CAST(NULL AS CHAR(19)) AS COMPILE_STATE,       CAST('Y' AS CHAR(1)) AS USE_NO_INDEX,       CAST(NULL AS DATETIME) AS STALE_SINCE,       CAST(NULL AS SIGNED) AS NUM_PCT_TABLES,       CAST(NULL AS SIGNED) AS NUM_FRESH_PCT_REGIONS,       CAST(NULL AS SIGNED) AS NUM_STALE_PCT_REGIONS,       CAST('NO' AS CHAR(3)) AS SEGMENT_CREATED,       CAST(NULL AS CHAR(128)) AS EVALUATION_EDITION,       CAST(NULL AS CHAR(128)) AS UNUSABLE_BEFORE,       CAST(NULL AS CHAR(128)) AS UNUSABLE_BEGINNING,       CAST(NULL AS CHAR(100)) AS DEFAULT_COLLATION,       CAST(         CASE ((B.TABLE_MODE >> 28) & 1)           WHEN 0 THEN 'N'           WHEN 1 THEN 'Y'           ELSE NULL         END AS CHAR(1)       ) AS ON_QUERY_COMPUTATION,       C.REFRESH_DOP AS REFRESH_DOP,       C.data_sync_scn AS DATA_SYNC_SCN,       CAST(         CASE C.data_sync_scn           WHEN 0 THEN 'NOT AVAILABLE'           ELSE TIMESTAMPDIFF(SECOND, SCN_TO_TIMESTAMP(C.data_sync_scn), NOW())         END AS CHAR(128)       ) AS DATA_SYNC_DELAY     FROM       oceanbase.__all_database A,       oceanbase.__all_table B,       oceanbase.__all_mview C,       oceanbase.__all_table D     WHERE A.DATABASE_ID = B.DATABASE_ID       AND B.TABLE_ID = C.MVIEW_ID       AND B.TABLE_TYPE = 7       AND B.DATA_TABLE_ID = D.TABLE_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mvref_stats_sys_defaults_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVREF_STATS_SYS_DEFAULTS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVREF_STATS_SYS_DEFAULTS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(PARAMETER_NAME AS CHAR(16)) AS PARAMETER_NAME,       CAST(VALUE AS CHAR(40)) AS VALUE     FROM     (       /* COLLECTION_LEVEL */       SELECT         'COLLECTION_LEVEL' PARAMETER_NAME,         CASE IFNULL(MAX(COLLECTION_LEVEL), 1)           WHEN 0 THEN 'NONE'           WHEN 1 THEN 'TYPICAL'           WHEN 2 THEN 'ADVANCED'           ELSE NULL         END VALUE       FROM         oceanbase.__all_virtual_mview_refresh_stats_sys_defaults        UNION ALL        /* RETENTION_PERIOD */       SELECT         'RETENTION_PERIOD' PARAMETER_NAME,         CAST(IFNULL(MAX(RETENTION_PERIOD), 31) AS CHAR) VALUE       FROM         oceanbase.__all_virtual_mview_refresh_stats_sys_defaults     ) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mvref_stats_sys_defaults_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVREF_STATS_SYS_DEFAULTS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVREF_STATS_SYS_DEFAULTS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(PARAMETER_NAME AS CHAR(16)) AS PARAMETER_NAME,       CAST(VALUE AS CHAR(40)) AS VALUE     FROM     (       /* COLLECTION_LEVEL */       SELECT         'COLLECTION_LEVEL' PARAMETER_NAME,         CASE IFNULL(MAX(COLLECTION_LEVEL), 1)           WHEN 0 THEN 'NONE'           WHEN 1 THEN 'TYPICAL'           WHEN 2 THEN 'ADVANCED'           ELSE NULL         END VALUE       FROM         oceanbase.__all_mview_refresh_stats_sys_defaults        UNION ALL        /* RETENTION_PERIOD */       SELECT         'RETENTION_PERIOD' PARAMETER_NAME,         CAST(IFNULL(MAX(RETENTION_PERIOD), 31) AS CHAR) VALUE       FROM         oceanbase.__all_mview_refresh_stats_sys_defaults     ) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mvref_stats_params_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVREF_STATS_PARAMS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVREF_STATS_PARAMS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(MV_OWNER AS CHAR(128)) AS MV_OWNER,       CAST(MV_NAME AS CHAR(128)) AS MV_NAME,       CAST(         CASE COLLECTION_LEVEL           WHEN 0 THEN 'NONE'           WHEN 1 THEN 'TYPICAL'           WHEN 2 THEN 'ADVANCED'           ELSE NULL         END AS CHAR(8)       ) AS COLLECTION_LEVEL,       RETENTION_PERIOD     FROM     (       WITH DEFVALS AS       (         SELECT           IFNULL(MAX(COLLECTION_LEVEL), 1) AS COLLECTION_LEVEL,           IFNULL(MAX(RETENTION_PERIOD), 31) AS RETENTION_PERIOD         FROM           oceanbase.__all_virtual_mview_refresh_stats_sys_defaults       )        SELECT         A.DATABASE_NAME MV_OWNER,         B.TABLE_NAME MV_NAME,         IFNULL(C.COLLECTION_LEVEL, D.COLLECTION_LEVEL) COLLECTION_LEVEL,         IFNULL(C.RETENTION_PERIOD, D.RETENTION_PERIOD) RETENTION_PERIOD       FROM         oceanbase.__all_virtual_database A,         oceanbase.__all_virtual_table B,         (           SELECT MVIEW_ID, COLLECTION_LEVEL, RETENTION_PERIOD FROM oceanbase.__all_virtual_mview_refresh_stats_params           RIGHT OUTER JOIN           (             SELECT MVIEW_ID FROM oceanbase.__all_virtual_mview           )           USING (MVIEW_ID)         ) C,         DEFVALS D       WHERE A.DATABASE_ID = B.DATABASE_ID         AND B.TABLE_ID = C.MVIEW_ID         AND B.TABLE_TYPE = 7     ) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mvref_stats_params_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVREF_STATS_PARAMS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVREF_STATS_PARAMS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(MV_OWNER AS CHAR(128)) AS MV_OWNER,       CAST(MV_NAME AS CHAR(128)) AS MV_NAME,       CAST(         CASE COLLECTION_LEVEL           WHEN 0 THEN 'NONE'           WHEN 1 THEN 'TYPICAL'           WHEN 2 THEN 'ADVANCED'           ELSE NULL         END AS CHAR(8)       ) AS COLLECTION_LEVEL,       RETENTION_PERIOD     FROM     (       WITH DEFVALS AS       (         SELECT           IFNULL(MAX(COLLECTION_LEVEL), 1) AS COLLECTION_LEVEL,           IFNULL(MAX(RETENTION_PERIOD), 31) AS RETENTION_PERIOD         FROM           oceanbase.__all_mview_refresh_stats_sys_defaults       )        SELECT         A.DATABASE_NAME MV_OWNER,         B.TABLE_NAME MV_NAME,         IFNULL(C.COLLECTION_LEVEL, D.COLLECTION_LEVEL) COLLECTION_LEVEL,         IFNULL(C.RETENTION_PERIOD, D.RETENTION_PERIOD) RETENTION_PERIOD       FROM         oceanbase.__all_database A,         oceanbase.__all_table B,         (           SELECT MVIEW_ID, COLLECTION_LEVEL, RETENTION_PERIOD FROM oceanbase.__all_mview_refresh_stats_params           RIGHT OUTER JOIN           (             SELECT MVIEW_ID FROM oceanbase.__all_mview           )           USING (MVIEW_ID)         ) C,         DEFVALS D       WHERE A.DATABASE_ID = B.DATABASE_ID         AND B.TABLE_ID = C.MVIEW_ID         AND B.TABLE_TYPE = 7     ) )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mvref_run_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVREF_RUN_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVREF_RUN_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.USER_NAME AS CHAR(128)) AS RUN_OWNER,       B.REFRESH_ID AS REFRESH_ID,       B.NUM_MVS_TOTAL AS NUM_MVS,       CAST(B.MVIEWS AS CHAR(4000)) AS MVIEWS,       CAST(B.BASE_TABLES AS CHAR(4000)) AS BASE_TABLES,       CAST(B.METHOD AS CHAR(4000)) AS METHOD,       CAST(B.ROLLBACK_SEG AS CHAR(4000)) AS ROLLBACK_SEG,       CAST(IF(B.PUSH_DEFERRED_RPC = 1, 'Y', 'N') AS CHAR(1)) AS PUSH_DEFERRED_RPC,       CAST(IF(B.REFRESH_AFTER_ERRORS = 1, 'Y', 'N') AS CHAR(1)) AS REFRESH_AFTER_ERRORS,       B.PURGE_OPTION AS PURGE_OPTION,       B.PARALLELISM AS PARALLELISM,       B.HEAP_SIZE AS HEAP_SIZE,       CAST(IF(B.ATOMIC_REFRESH = 1, 'Y', 'N') AS CHAR(1)) AS ATOMIC_REFRESH,       CAST(IF(B.NESTED = 1, 'Y', 'N') AS CHAR(1)) AS NESTED,       CAST(IF(B.OUT_OF_PLACE = 1, 'Y', 'N') AS CHAR(1)) AS OUT_OF_PLACE,       B.NUMBER_OF_FAILURES AS NUMBER_OF_FAILURES,       CAST(B.START_TIME AS DATETIME) AS START_TIME,       CAST(B.END_TIME AS DATETIME) AS END_TIME,       B.ELAPSED_TIME AS ELAPSED_TIME,       CAST(0 AS SIGNED) AS LOG_SETUP_TIME,       B.LOG_PURGE_TIME AS LOG_PURGE_TIME,       CAST(IF(B.COMPLETE_STATS_AVALIABLE = 1, 'Y', 'N') AS CHAR(1)) AS COMPLETE_STATS_AVAILABLE     FROM       oceanbase.__all_virtual_user A,       oceanbase.__all_virtual_mview_refresh_run_stats B,       (         SELECT           C1.REFRESH_ID AS REFRESH_ID         FROM           oceanbase.__all_virtual_mview_refresh_stats C1,           oceanbase.__all_virtual_table C2         WHERE C1.MVIEW_ID = C2.TABLE_ID         GROUP BY REFRESH_ID       ) C     WHERE A.USER_ID = B.RUN_USER_ID       AND B.REFRESH_ID = C.REFRESH_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mvref_run_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVREF_RUN_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVREF_RUN_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.USER_NAME AS CHAR(128)) AS RUN_OWNER,       B.REFRESH_ID AS REFRESH_ID,       B.NUM_MVS_TOTAL AS NUM_MVS,       CAST(B.MVIEWS AS CHAR(4000)) AS MVIEWS,       CAST(B.BASE_TABLES AS CHAR(4000)) AS BASE_TABLES,       CAST(B.METHOD AS CHAR(4000)) AS METHOD,       CAST(B.ROLLBACK_SEG AS CHAR(4000)) AS ROLLBACK_SEG,       CAST(IF(B.PUSH_DEFERRED_RPC = 1, 'Y', 'N') AS CHAR(1)) AS PUSH_DEFERRED_RPC,       CAST(IF(B.REFRESH_AFTER_ERRORS = 1, 'Y', 'N') AS CHAR(1)) AS REFRESH_AFTER_ERRORS,       B.PURGE_OPTION AS PURGE_OPTION,       B.PARALLELISM AS PARALLELISM,       B.HEAP_SIZE AS HEAP_SIZE,       CAST(IF(B.ATOMIC_REFRESH = 1, 'Y', 'N') AS CHAR(1)) AS ATOMIC_REFRESH,       CAST(IF(B.NESTED = 1, 'Y', 'N') AS CHAR(1)) AS NESTED,       CAST(IF(B.OUT_OF_PLACE = 1, 'Y', 'N') AS CHAR(1)) AS OUT_OF_PLACE,       B.NUMBER_OF_FAILURES AS NUMBER_OF_FAILURES,       CAST(B.START_TIME AS DATETIME) AS START_TIME,       CAST(B.END_TIME AS DATETIME) AS END_TIME,       B.ELAPSED_TIME AS ELAPSED_TIME,       CAST(0 AS SIGNED) AS LOG_SETUP_TIME,       B.LOG_PURGE_TIME AS LOG_PURGE_TIME,       CAST(IF(B.COMPLETE_STATS_AVALIABLE = 1, 'Y', 'N') AS CHAR(1)) AS COMPLETE_STATS_AVAILABLE     FROM       oceanbase.__all_user A,       oceanbase.__all_mview_refresh_run_stats B,       (         SELECT           C1.REFRESH_ID AS REFRESH_ID         FROM           oceanbase.__all_mview_refresh_stats C1,           oceanbase.__all_table C2         WHERE C1.MVIEW_ID = C2.TABLE_ID         GROUP BY REFRESH_ID       ) C     WHERE A.USER_ID = B.RUN_USER_ID       AND B.REFRESH_ID = C.REFRESH_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mvref_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVREF_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVREF_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS MV_OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MV_NAME,       C.REFRESH_ID AS REFRESH_ID,       CAST(         CASE C.REFRESH_TYPE           WHEN 0 THEN 'COMPLETE'           WHEN 1 THEN 'FAST'           ELSE NULL         END AS CHAR(30)       ) AS REFRESH_METHOD,       CAST(NULL AS CHAR(4000)) AS REFRESH_OPTIMIZATIONS,       CAST(NULL AS CHAR(4000)) AS ADDITIONAL_EXECUTIONS,       CAST(C.START_TIME AS DATETIME) AS START_TIME,       CAST(C.END_TIME AS DATETIME) AS END_TIME,       C.ELAPSED_TIME AS ELAPSED_TIME,       CAST(0 AS SIGNED) AS LOG_SETUP_TIME,       C.LOG_PURGE_TIME AS LOG_PURGE_TIME,       C.INITIAL_NUM_ROWS AS INITIAL_NUM_ROWS,       C.FINAL_NUM_ROWS AS FINAL_NUM_ROWS,       C.RESULT AS RESULT     FROM       oceanbase.__all_virtual_database A,       oceanbase.__all_virtual_table B,       oceanbase.__all_virtual_mview_refresh_stats C     WHERE A.DATABASE_ID = B.DATABASE_ID       AND B.TABLE_ID = C.MVIEW_ID       AND B.TABLE_TYPE = 7 )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mvref_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVREF_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVREF_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS MV_OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MV_NAME,       C.REFRESH_ID AS REFRESH_ID,       CAST(         CASE C.REFRESH_TYPE           WHEN 0 THEN 'COMPLETE'           WHEN 1 THEN 'FAST'           ELSE NULL         END AS CHAR(30)       ) AS REFRESH_METHOD,       CAST(NULL AS CHAR(4000)) AS REFRESH_OPTIMIZATIONS,       CAST(NULL AS CHAR(4000)) AS ADDITIONAL_EXECUTIONS,       CAST(C.START_TIME AS DATETIME) AS START_TIME,       CAST(C.END_TIME AS DATETIME) AS END_TIME,       C.ELAPSED_TIME AS ELAPSED_TIME,       CAST(0 AS SIGNED) AS LOG_SETUP_TIME,       C.LOG_PURGE_TIME AS LOG_PURGE_TIME,       C.INITIAL_NUM_ROWS AS INITIAL_NUM_ROWS,       C.FINAL_NUM_ROWS AS FINAL_NUM_ROWS,       C.RESULT AS RESULT     FROM       oceanbase.__all_database A,       oceanbase.__all_table B,       oceanbase.__all_mview_refresh_stats C     WHERE A.DATABASE_ID = B.DATABASE_ID       AND B.TABLE_ID = C.MVIEW_ID       AND B.TABLE_TYPE = 7 )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mvref_change_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVREF_CHANGE_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVREF_CHANGE_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(C.DATABASE_NAME AS CHAR(128)) AS TBL_OWNER,       CAST(D.TABLE_NAME AS CHAR(128)) AS TBL_NAME,       CAST(A.DATABASE_NAME AS CHAR(128)) AS MV_OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MV_NAME,       E.REFRESH_ID AS REFRESH_ID,       E.NUM_ROWS_INS AS NUM_ROWS_INS,       E.NUM_ROWS_UPD AS NUM_ROWS_UPD,       E.NUM_ROWS_DEL AS NUM_ROWS_DEL,       CAST(0 AS SIGNED) AS NUM_ROWS_DL_INS,       CAST('N' AS CHAR(1)) AS PMOPS_OCCURRED,       CAST(NULL AS CHAR(4000)) AS PMOP_DETAILS,       E.NUM_ROWS AS NUM_ROWS     FROM       oceanbase.__all_virtual_database A,       oceanbase.__all_virtual_table B,       oceanbase.__all_virtual_database C,       oceanbase.__all_virtual_table D,       oceanbase.__all_virtual_mview_refresh_change_stats E     WHERE A.DATABASE_ID = B.DATABASE_ID       AND C.DATABASE_ID = D.DATABASE_ID       AND E.MVIEW_ID = B.TABLE_ID       AND E.DETAIL_TABLE_ID = D.TABLE_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mvref_change_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVREF_CHANGE_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVREF_CHANGE_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(C.DATABASE_NAME AS CHAR(128)) AS TBL_OWNER,       CAST(D.TABLE_NAME AS CHAR(128)) AS TBL_NAME,       CAST(A.DATABASE_NAME AS CHAR(128)) AS MV_OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MV_NAME,       E.REFRESH_ID AS REFRESH_ID,       E.NUM_ROWS_INS AS NUM_ROWS_INS,       E.NUM_ROWS_UPD AS NUM_ROWS_UPD,       E.NUM_ROWS_DEL AS NUM_ROWS_DEL,       CAST(0 AS SIGNED) AS NUM_ROWS_DL_INS,       CAST('N' AS CHAR(1)) AS PMOPS_OCCURRED,       CAST(NULL AS CHAR(4000)) AS PMOP_DETAILS,       E.NUM_ROWS AS NUM_ROWS     FROM       oceanbase.__all_database A,       oceanbase.__all_table B,       oceanbase.__all_database C,       oceanbase.__all_table D,       oceanbase.__all_mview_refresh_change_stats E     WHERE A.DATABASE_ID = B.DATABASE_ID       AND C.DATABASE_ID = D.DATABASE_ID       AND E.MVIEW_ID = B.TABLE_ID       AND E.DETAIL_TABLE_ID = D.TABLE_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_mvref_stmt_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_MVREF_STMT_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_MVREF_STMT_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS MV_OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MV_NAME,       C.REFRESH_ID AS REFRESH_ID,       C.STEP AS STEP,       CAST(C.SQLID AS CHAR(32)) AS SQLID,       C.STMT AS STMT,       C.EXECUTION_TIME AS EXECUTION_TIME,       C.EXECUTION_PLAN AS EXECUTION_PLAN     FROM       oceanbase.__all_virtual_database A,       oceanbase.__all_virtual_table B,       oceanbase.__all_virtual_mview_refresh_stmt_stats C     WHERE A.DATABASE_ID = B.DATABASE_ID       AND B.TABLE_ID = C.MVIEW_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_mvref_stmt_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_MVREF_STMT_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_MVREF_STMT_STATS_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       CAST(A.DATABASE_NAME AS CHAR(128)) AS MV_OWNER,       CAST(B.TABLE_NAME AS CHAR(128)) AS MV_NAME,       C.REFRESH_ID AS REFRESH_ID,       C.STEP AS STEP,       CAST(C.SQLID AS CHAR(32)) AS SQLID,       C.STMT AS STMT,       C.EXECUTION_TIME AS EXECUTION_TIME,       C.EXECUTION_PLAN AS EXECUTION_PLAN     FROM       oceanbase.__all_database A,       oceanbase.__all_table B,       oceanbase.__all_mview_refresh_stmt_stats C     WHERE A.DATABASE_ID = B.DATABASE_ID       AND B.TABLE_ID = C.MVIEW_ID )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::gv_ob_session_ps_info_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_SESSION_PS_INFO_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_SESSION_PS_INFO_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__( SELECT   PROXY_SESSION_ID,   SESSION_ID,   PS_CLIENT_STMT_ID,   PS_INNER_STMT_ID,   STMT_TYPE,   PARAM_COUNT,   PARAM_TYPES,   REF_COUNT,   CHECKSUM FROM   oceanbase.__all_virtual_session_ps_info )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::v_ob_session_ps_info_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_SESSION_PS_INFO_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_SESSION_PS_INFO_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT     PROXY_SESSION_ID,     SESSION_ID,     PS_CLIENT_STMT_ID,     PS_INNER_STMT_ID,     STMT_TYPE,     PARAM_COUNT,     PARAM_TYPES,     REF_COUNT,     CHECKSUM   FROM oceanbase.GV$OB_SESSION_PS_INFO )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::gv_ob_tracepoint_info_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_TRACEPOINT_INFO_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_TRACEPOINT_INFO_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT           TP_NO,           TP_NAME,           TP_DESCRIBE,           TP_FREQUENCY,           TP_ERROR_CODE,           TP_OCCUR,           TP_MATCH         FROM oceanbase.__all_virtual_tracepoint_info )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::v_ob_tracepoint_info_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_TRACEPOINT_INFO_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_TRACEPOINT_INFO_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT           TP_NO,           TP_NAME,           TP_DESCRIBE,           TP_FREQUENCY,           TP_ERROR_CODE,           TP_OCCUR,           TP_MATCH     FROM OCEANBASE.GV$OB_TRACEPOINT_INFO  )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::v_ob_compatibility_control_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_COMPATIBILITY_CONTROL_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_COMPATIBILITY_CONTROL_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT       name as NAME,       description as DESCRIPTION,       CASE is_enable WHEN 1 THEN 'TRUE' ELSE 'FALSE' END AS IS_ENABLE,       enable_versions as ENABLE_VERSIONS     FROM oceanbase.__all_virtual_compatibility_control )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::dba_ob_rsrc_directives_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_RSRC_DIRECTIVES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_RSRC_DIRECTIVES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       PLAN,       GROUP_OR_SUBPLAN,       COMMENTS,       MGMT_P1,       UTILIZATION_LIMIT,       MIN_IOPS,       MAX_IOPS,       WEIGHT_IOPS,       MAX_NET_BANDWIDTH,       NET_BANDWIDTH_WEIGHT     FROM        oceanbase.__all_res_mgr_directive )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}

int ObInnerTableSchema::cdb_ob_rsrc_directives_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_OB_RSRC_DIRECTIVES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_OB_RSRC_DIRECTIVES_TNAME))) {
      LOG_ERROR("fail to set table_name", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_compress_func_name(OB_DEFAULT_COMPRESS_FUNC_NAME))) {
      LOG_ERROR("fail to set compress_func_name", K(ret));
    }
  }
  table_schema.set_part_level(PARTITION_LEVEL_ZERO);
  table_schema.set_charset_type(ObCharset::get_default_charset());
  table_schema.set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       PLAN,       GROUP_OR_SUBPLAN,       COMMENTS,       MGMT_P1,       UTILIZATION_LIMIT,       MIN_IOPS,       MAX_IOPS,       WEIGHT_IOPS,       MAX_NET_BANDWIDTH,       NET_BANDWIDTH_WEIGHT     FROM        oceanbase.__all_virtual_res_mgr_directive )__"))) {
      LOG_ERROR("fail to set view_definition", K(ret));
    }
  }
  table_schema.set_index_using_type(USING_BTREE);
  table_schema.set_row_store_type(ENCODING_ROW_STORE);
  table_schema.set_store_format(OB_STORE_FORMAT_DYNAMIC_MYSQL);
  table_schema.set_progressive_merge_round(1);
  table_schema.set_storage_format_version(3);
  table_schema.set_tablet_id(0);
  table_schema.set_micro_index_clustered(false);

  table_schema.set_max_used_column_id(column_id);
  return ret;
}


} // end namespace share
} // end namespace oceanbase
