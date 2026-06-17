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

int ObInnerTableSchema::gv_ob_memory_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_MEMORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_MEMORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__( SELECT      ctx_name AS CTX_NAME,      mod_name AS MOD_NAME,      sum(COUNT) AS COUNT,      sum(hold) AS HOLD,      sum(USED) AS USED FROM     oceanbase.__all_virtual_memory_info WHERE         mod_type='user' GROUP BY ctx_name, mod_name ORDER BY ctx_name, mod_name )__"))) {
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

int ObInnerTableSchema::v_ob_memory_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_MEMORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_MEMORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__( SELECT     CTX_NAME,     MOD_NAME,     COUNT,     HOLD,     USED FROM     oceanbase.GV$OB_MEMORY )__"))) {
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

int ObInnerTableSchema::gv_ob_memstore_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_MEMSTORE_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_MEMSTORE_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__( SELECT     ACTIVE_SPAN,     FREEZE_TRIGGER,     FREEZE_CNT,     MEMSTORE_USED,     MEMSTORE_LIMIT FROM     oceanbase.__all_virtual_tenant_memstore_info )__"))) {
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

int ObInnerTableSchema::v_ob_memstore_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_MEMSTORE_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_MEMSTORE_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__( SELECT     ACTIVE_SPAN,     FREEZE_TRIGGER,     FREEZE_CNT,     MEMSTORE_USED,     MEMSTORE_LIMIT FROM     OCEANBASE.GV$OB_MEMSTORE )__"))) {
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

int ObInnerTableSchema::gv_ob_memstore_info_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_MEMSTORE_INFO_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_MEMSTORE_INFO_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__( SELECT     TABLET_ID,     IS_ACTIVE,     START_SCN,     END_SCN,     FREEZE_TS FROM     oceanbase.__all_virtual_memstore_info )__"))) {
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

int ObInnerTableSchema::v_ob_memstore_info_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_MEMSTORE_INFO_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_MEMSTORE_INFO_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__( SELECT     TABLET_ID,     IS_ACTIVE,     START_SCN,     END_SCN,     FREEZE_TS FROM     OCEANBASE.GV$OB_MEMSTORE_INFO )__"))) {
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

int ObInnerTableSchema::v_ob_plan_cache_stat_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_PLAN_CACHE_STAT_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_PLAN_CACHE_STAT_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT SQL_NUM,MEM_USED,MEM_HOLD,ACCESS_COUNT,   HIT_COUNT,HIT_RATE,PLAN_NUM,MEM_LIMIT,HASH_BUCKET,STMTKEY_NUM   FROM oceanbase.GV$OB_PLAN_CACHE_STAT )__"))) {
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

int ObInnerTableSchema::v_ob_plan_cache_plan_stat_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_PLAN_CACHE_PLAN_STAT_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_PLAN_CACHE_PLAN_STAT_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT PLAN_ID,SQL_ID,TYPE,IS_BIND_SENSITIVE,IS_BIND_AWARE,     DB_ID,STATEMENT,QUERY_SQL,SPECIAL_PARAMS,PARAM_INFOS, SYS_VARS, CONFIGS, PLAN_HASH,     FIRST_LOAD_TIME,SCHEMA_VERSION,LAST_ACTIVE_TIME,AVG_EXE_USEC,SLOWEST_EXE_TIME,SLOWEST_EXE_USEC,     SLOW_COUNT,HIT_COUNT,PLAN_SIZE,EXECUTIONS,DISK_READS,DIRECT_WRITES,BUFFER_GETS,APPLICATION_WAIT_TIME,     CONCURRENCY_WAIT_TIME,USER_IO_WAIT_TIME,ROWS_PROCESSED,ELAPSED_TIME,CPU_TIME,LARGE_QUERYS,     DELAYED_LARGE_QUERYS,DELAYED_PX_QUERYS,OUTLINE_VERSION,OUTLINE_ID,OUTLINE_DATA,ACS_SEL_INFO,     TABLE_SCAN,EVOLUTION, EVO_EXECUTIONS, EVO_CPU_TIME, TIMEOUT_COUNT, PS_STMT_ID, SESSID,     TEMP_TABLES, IS_USE_JIT,OBJECT_TYPE,HINTS_INFO,HINTS_ALL_WORKED, PL_SCHEMA_ID,     IS_BATCHED_MULTI_STMT, RULE_NAME, PLAN_STATUS, ADAPTIVE_FEEDBACK_TIMES,     FIRST_GET_PLAN_TIME, FIRST_EXE_USEC   FROM oceanbase.GV$OB_PLAN_CACHE_PLAN_STAT )__"))) {
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

int ObInnerTableSchema::gv_ob_plan_cache_plan_explain_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_OB_PLAN_CACHE_PLAN_EXPLAIN_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_OB_PLAN_CACHE_PLAN_EXPLAIN_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT PLAN_ID,            PLAN_DEPTH,            PLAN_LINE_ID,            OPERATOR,            NAME,            ROWS,            COST,            PROPERTY    FROM oceanbase.__all_virtual_plan_cache_plan_explain )__"))) {
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

int ObInnerTableSchema::v_ob_plan_cache_plan_explain_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_OB_PLAN_CACHE_PLAN_EXPLAIN_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_OB_PLAN_CACHE_PLAN_EXPLAIN_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT PLAN_ID,            PLAN_DEPTH,            PLAN_LINE_ID,            OPERATOR,            NAME,            ROWS,            COST,            PROPERTY FROM oceanbase.GV$OB_PLAN_CACHE_PLAN_EXPLAIN )__"))) {
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

int ObInnerTableSchema::gv_sql_plan_monitor_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_SQL_PLAN_MONITOR_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_SQL_PLAN_MONITOR_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(           SELECT           1 as CON_ID,           REQUEST_ID,           CAST(NULL as UNSIGNED) AS `KEY`,           CAST(NULL AS CHAR(19)) as STATUS,           TRACE_ID,           DB_TIME,           USER_IO_WAIT_TIME,           CAST(NULL AS UNSIGNED) AS OTHER_WAIT_TIME,           FIRST_REFRESH_TIME,           LAST_REFRESH_TIME,           FIRST_CHANGE_TIME,           LAST_CHANGE_TIME,           CAST(NULL AS UNSIGNED) AS REFRESH_COUNT,           CAST(NULL AS UNSIGNED) AS SID,           THREAD_ID  PROCESS_NAME,           SQL_ID,           CAST(NULL AS UNSIGNED) AS SQL_EXEC_START,           CAST(NULL AS UNSIGNED) AS SQL_EXEC_ID,           PLAN_HASH_VALUE AS SQL_PLAN_HASH_VALUE,           CAST(NULL AS BINARY(8)) AS SQL_CHILD_ADDRESS,           CAST(NULL AS UNSIGNED) AS PLAN_PARENT_ID,           PLAN_LINE_ID,           PLAN_OPERATION,           CAST(NULL AS CHAR(30)) PLAN_OPTIONS,           CAST(NULL AS CHAR(128)) PLAN_OBJECT_OWNER,           CAST(NULL AS CHAR(128)) PLAN_OBJECT_NAME,           CAST(NULL AS CHAR(80)) PLAN_OBJECT_TYPE,           PLAN_DEPTH,           CAST( NULL AS UNSIGNED) AS PLAN_POSITION,           CAST( NULL AS UNSIGNED) AS PLAN_COST,           CAST( NULL AS UNSIGNED) AS PLAN_CARDINALITY,           CAST( NULL AS UNSIGNED) AS PLAN_BYTES,           CAST( NULL AS UNSIGNED) AS PLAN_TIME,           CAST( NULL AS UNSIGNED) AS PLAN_PARTITION_START,           CAST( NULL AS UNSIGNED) AS PLAN_PARTITION_STOP,           CAST( NULL AS UNSIGNED) AS PLAN_CPU_COST,           CAST( NULL AS UNSIGNED) AS PLAN_IO_COST,           CAST( NULL AS UNSIGNED) AS PLAN_TEMP_SPACE,           STARTS,           OUTPUT_ROWS,           CAST( NULL AS UNSIGNED) AS IO_INTERCONNECT_BYTES,           CAST( NULL AS UNSIGNED) AS PHYSICAL_READ_REQUESTS,           CAST( NULL AS UNSIGNED) AS PHYSICAL_READ_BYTES,           CAST( NULL AS UNSIGNED) AS PHYSICAL_WRITE_REQUESTS,           CAST( NULL AS UNSIGNED) AS PHYSICAL_WRITE_BYTES,           CAST( WORKAREA_MEM AS UNSIGNED) AS WORKAREA_MEM,           CAST( WORKAREA_MAX_MEM AS UNSIGNED) AS WORKAREA_MAX_MEM,           CAST( WORKAREA_TEMPSEG AS UNSIGNED) AS WORKAREA_TEMPSEG,           CAST( WORKAREA_MAX_TEMPSEG AS UNSIGNED) AS WORKAREA_MAX_TEMPSEG,           CAST( NULL AS UNSIGNED) AS OTHERSTAT_GROUP_ID,           OTHERSTAT_1_ID,           CAST(NULL AS UNSIGNED) AS OTHERSTAT_1_TYPE,           OTHERSTAT_1_VALUE,           OTHERSTAT_2_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_2_TYPE,           OTHERSTAT_2_VALUE,           OTHERSTAT_3_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_3_TYPE,           OTHERSTAT_3_VALUE,           OTHERSTAT_4_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_4_TYPE,           OTHERSTAT_4_VALUE,           OTHERSTAT_5_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_5_TYPE,           OTHERSTAT_5_VALUE,           OTHERSTAT_6_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_6_TYPE,           OTHERSTAT_6_VALUE,           OTHERSTAT_7_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_7_TYPE,           OTHERSTAT_7_VALUE,           OTHERSTAT_8_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_8_TYPE,           OTHERSTAT_8_VALUE,           OTHERSTAT_9_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_9_TYPE,           OTHERSTAT_9_VALUE,           OTHERSTAT_10_ID,           CAST(NULL AS UNSIGNED) OTHERSTAT_10_TYPE,           OTHERSTAT_10_VALUE,           CAST(NULL AS CHAR(255)) AS OTHER_XML,           CAST(NULL AS UNSIGNED) AS PLAN_OPERATION_INACTIVE,           OUTPUT_BATCHES,           SKIPPED_ROWS_COUNT         FROM oceanbase.__all_virtual_sql_plan_monitor )__"))) {
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

int ObInnerTableSchema::v_sql_plan_monitor_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_SQL_PLAN_MONITOR_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_SQL_PLAN_MONITOR_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT  CON_ID,     REQUEST_ID,     `KEY`,     STATUS,     TRACE_ID,     DB_TIME,     USER_IO_WAIT_TIME,     OTHER_WAIT_TIME,     FIRST_REFRESH_TIME,     LAST_REFRESH_TIME,     FIRST_CHANGE_TIME,     LAST_CHANGE_TIME,     REFRESH_COUNT,     SID,     PROCESS_NAME,     SQL_ID,     SQL_EXEC_START,     SQL_EXEC_ID,     SQL_PLAN_HASH_VALUE,     SQL_CHILD_ADDRESS,     PLAN_PARENT_ID,     PLAN_LINE_ID,     PLAN_OPERATION,     PLAN_OPTIONS,     PLAN_OBJECT_OWNER,     PLAN_OBJECT_NAME,     PLAN_OBJECT_TYPE,     PLAN_DEPTH,     PLAN_POSITION,     PLAN_COST,     PLAN_CARDINALITY,     PLAN_BYTES,     PLAN_TIME,     PLAN_PARTITION_START,     PLAN_PARTITION_STOP,     PLAN_CPU_COST,     PLAN_IO_COST,     PLAN_TEMP_SPACE,     STARTS,     OUTPUT_ROWS,     IO_INTERCONNECT_BYTES,     PHYSICAL_READ_REQUESTS,     PHYSICAL_READ_BYTES,     PHYSICAL_WRITE_REQUESTS,     PHYSICAL_WRITE_BYTES,     WORKAREA_MEM,     WORKAREA_MAX_MEM,     WORKAREA_TEMPSEG,     WORKAREA_MAX_TEMPSEG,     OTHERSTAT_GROUP_ID,     OTHERSTAT_1_ID,     OTHERSTAT_1_TYPE,     OTHERSTAT_1_VALUE,     OTHERSTAT_2_ID,     OTHERSTAT_2_TYPE,     OTHERSTAT_2_VALUE,     OTHERSTAT_3_ID,     OTHERSTAT_3_TYPE,     OTHERSTAT_3_VALUE,     OTHERSTAT_4_ID,     OTHERSTAT_4_TYPE,     OTHERSTAT_4_VALUE,     OTHERSTAT_5_ID,     OTHERSTAT_5_TYPE,     OTHERSTAT_5_VALUE,     OTHERSTAT_6_ID,     OTHERSTAT_6_TYPE,     OTHERSTAT_6_VALUE,     OTHERSTAT_7_ID,     OTHERSTAT_7_TYPE,     OTHERSTAT_7_VALUE,     OTHERSTAT_8_ID,     OTHERSTAT_8_TYPE,     OTHERSTAT_8_VALUE,     OTHERSTAT_9_ID,     OTHERSTAT_9_TYPE,     OTHERSTAT_9_VALUE,     OTHERSTAT_10_ID,     OTHERSTAT_10_TYPE,     OTHERSTAT_10_VALUE,     OTHER_XML,     PLAN_OPERATION_INACTIVE,     OUTPUT_BATCHES,     SKIPPED_ROWS_COUNT  FROM OCEANBASE.GV$SQL_PLAN_MONITOR  )__"))) {
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

int ObInnerTableSchema::dba_recyclebin_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_RECYCLEBIN_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_RECYCLEBIN_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT   CAST(B.DATABASE_NAME AS CHAR(128)) AS OWNER,   CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,   CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,   CAST(NULL AS CHAR(9)) AS OPERATION,   CAST(CASE A.TYPE        WHEN 1 THEN 'TABLE'        WHEN 2 THEN 'NORMAL INDEX'        WHEN 3 THEN 'VIEW'        ELSE NULL END AS CHAR(25)) AS TYPE,   CAST(NULL AS CHAR(30)) AS TS_NAME,   CAST(C.GMT_CREATE AS DATE) AS CREATETIME,   CAST(C.GMT_MODIFIED AS DATE) AS DROPTIME,   CAST(NULL AS SIGNED) AS DROPSCN,   CAST(NULL AS CHAR(128)) AS PARTITION_NAME,   CAST('YES' AS CHAR(3)) AS CAN_UNDROP,   CAST('YES' AS CHAR(3)) AS CAN_PURGE,   CAST(NULL AS SIGNED) AS RELATED,   CAST(NULL AS SIGNED) AS BASE_OBJECT,   CAST(NULL AS SIGNED) AS PURGE_OBJECT,   CAST(NULL AS SIGNED) AS SPACE   FROM OCEANBASE.__ALL_RECYCLEBIN A   JOIN OCEANBASE.__ALL_DATABASE B     ON A.DATABASE_ID = B.DATABASE_ID   JOIN OCEANBASE.__ALL_TABLE C     ON A.TABLE_ID = C.TABLE_ID   WHERE A.TYPE IN (1, 2, 3)     AND C.TABLE_MODE >> 12 & 15 in (0,1)     AND C.INDEX_ATTRIBUTES_SET & 16 = 0    UNION ALL    SELECT   CAST(A.ORIGINAL_NAME AS CHAR(128)) AS OWNER,   CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,   CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,   CAST(NULL AS CHAR(9)) AS OPERATION,   CAST('DATABASE' AS CHAR(25)) AS TYPE,   CAST(NULL AS CHAR(30)) AS TS_NAME,   CAST(B.GMT_CREATE AS DATE) AS CREATETIME,   CAST(B.GMT_MODIFIED AS DATE) AS DROPTIME,   CAST(NULL AS SIGNED) AS DROPSCN,   CAST(NULL AS CHAR(128)) AS PARTITION_NAME,   CAST('YES' AS CHAR(3)) AS CAN_UNDROP,   CAST('YES' AS CHAR(3)) AS CAN_PURGE,   CAST(NULL AS SIGNED) AS RELATED,   CAST(NULL AS SIGNED) AS BASE_OBJECT,   CAST(NULL AS SIGNED) AS PURGE_OBJECT,   CAST(NULL AS SIGNED) AS SPACE   FROM OCEANBASE.__ALL_RECYCLEBIN A   JOIN OCEANBASE.__ALL_DATABASE B     ON A.DATABASE_ID = B.DATABASE_ID   WHERE A.TYPE = 4    UNION ALL    SELECT   CAST(B.DATABASE_NAME AS CHAR(128)) AS OWNER,   CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,   CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,   CAST(NULL AS CHAR(9)) AS OPERATION,   CAST('TRIGGER' AS CHAR(25)) AS TYPE,   CAST(NULL AS CHAR(30)) AS TS_NAME,   CAST(C.GMT_CREATE AS DATE) AS CREATETIME,   CAST(C.GMT_MODIFIED AS DATE) AS DROPTIME,   CAST(NULL AS SIGNED) AS DROPSCN,   CAST(NULL AS CHAR(128)) AS PARTITION_NAME,   CAST('YES' AS CHAR(3)) AS CAN_UNDROP,   CAST('YES' AS CHAR(3)) AS CAN_PURGE,   CAST(NULL AS SIGNED) AS RELATED,   CAST(NULL AS SIGNED) AS BASE_OBJECT,   CAST(NULL AS SIGNED) AS PURGE_OBJECT,   CAST(NULL AS SIGNED) AS SPACE   FROM OCEANBASE.__ALL_RECYCLEBIN A   JOIN OCEANBASE.__ALL_DATABASE B     ON A.DATABASE_ID = B.DATABASE_ID   JOIN OCEANBASE.__ALL_TENANT_TRIGGER C     ON A.TABLE_ID = C.TRIGGER_ID   WHERE A.TYPE = 6    UNION ALL    SELECT   CAST(NULL AS CHAR(128)) AS OWNER,   CAST(A.OBJECT_NAME AS CHAR(128)) AS OBJECT_NAME,   CAST(A.ORIGINAL_NAME AS CHAR(128)) AS ORIGINAL_NAME,   CAST(NULL AS CHAR(9)) AS OPERATION,   CAST('TENANT' AS CHAR(25)) AS TYPE,   CAST(NULL AS CHAR(30)) AS TS_NAME,   CAST(USEC_TO_TIME(B.SCHEMA_VERSION) AS DATE) AS CREATETIME,   CAST(A.GMT_CREATE AS DATE) AS DROPTIME,   CAST(NULL AS SIGNED) AS DROPSCN,   CAST(NULL AS CHAR(128)) AS PARTITION_NAME,   CAST('YES' AS CHAR(3)) AS CAN_UNDROP,   CAST('YES' AS CHAR(3)) AS CAN_PURGE,   CAST(NULL AS SIGNED) AS RELATED,   CAST(NULL AS SIGNED) AS BASE_OBJECT,   CAST(NULL AS SIGNED) AS PURGE_OBJECT,   CAST(NULL AS SIGNED) AS SPACE   FROM OCEANBASE.__ALL_RECYCLEBIN A   JOIN OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE B     ON B.TABLE_NAME = '__all_core_table'   WHERE A.TYPE = 7 )__"))) {
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
