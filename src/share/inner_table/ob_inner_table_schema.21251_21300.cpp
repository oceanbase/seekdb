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

int ObInnerTableSchema::dba_tab_statistics_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_TAB_STATISTICS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_TAB_STATISTICS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT     CAST(DB.DATABASE_NAME AS     CHAR(128)) AS OWNER,     CAST(V.TABLE_NAME       AS  CHAR(128)) AS TABLE_NAME,     CAST(V.PARTITION_NAME   AS  CHAR(128)) AS PARTITION_NAME,     CAST(V.PARTITION_POSITION AS    NUMBER) AS PARTITION_POSITION,     CAST(V.SUBPARTITION_NAME  AS    CHAR(128)) AS SUBPARTITION_NAME,     CAST(V.SUBPARTITION_POSITION AS NUMBER) AS SUBPARTITION_POSITION,     CAST(V.OBJECT_TYPE AS   CHAR(12)) AS OBJECT_TYPE,     CAST(STAT.ROW_CNT AS    NUMBER) AS NUM_ROWS,     CAST(NULL AS    NUMBER) AS BLOCKS,     CAST(NULL AS    NUMBER) AS EMPTY_BLOCKS,     CAST(NULL AS    NUMBER) AS AVG_SPACE,     CAST(NULL AS    NUMBER) AS CHAIN_CNT,     CAST(STAT.AVG_ROW_LEN AS    NUMBER) AS AVG_ROW_LEN,     CAST(NULL AS    NUMBER) AS AVG_SPACE_FREELIST_BLOCKS,     CAST(NULL AS    NUMBER) AS NUM_FREELIST_BLOCKS,     CAST(NULL AS    NUMBER) AS AVG_CACHED_BLOCKS,     CAST(NULL AS    NUMBER) AS AVG_CACHE_HIT_RATIO,     CAST(NULL AS    NUMBER) AS IM_IMCU_COUNT,     CAST(NULL AS    NUMBER) AS IM_BLOCK_COUNT,     CAST(NULL AS    DATETIME) AS IM_STAT_UPDATE_TIME,     CAST(NULL AS    NUMBER) AS SCAN_RATE,     CAST(STAT.SPARE1 AS    DECIMAL(20, 0)) AS SAMPLE_SIZE,     CAST(STAT.LAST_ANALYZED AS DATETIME(6)) AS LAST_ANALYZED,     CAST((CASE STAT.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,     CAST((CASE STAT.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,     CAST((CASE WHEN STAT.STATTYPE_LOCKED & 15 IS NULL THEN NULL ELSE (CASE STAT.STATTYPE_LOCKED & 15 WHEN 0 THEN NULL WHEN 1 THEN 'DATA' WHEN 2 THEN 'CACHE' ELSE 'ALL' END) END) AS CHAR(5)) AS STATTYPE_LOCKED,     CAST((CASE STAT.STALE_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS STALE_STATS,     CAST(NULL AS    CHAR(7)) AS SCOPE     FROM     (       (SELECT DATABASE_ID,               TABLE_ID,               -2 AS PARTITION_ID,               TABLE_NAME,               NULL AS PARTITION_NAME,               NULL AS SUBPARTITION_NAME,               NULL AS PARTITION_POSITION,               NULL AS SUBPARTITION_POSITION,               'TABLE' AS OBJECT_TYPE           FROM             OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE           WHERE TABLE_TYPE IN (0,2,3,6,14,15)         UNION ALL         SELECT DATABASE_ID,                TABLE_ID,                CASE WHEN PART_LEVEL = 0 THEN -2 ELSE -1 END AS PARTITION_ID,                TABLE_NAME,                NULL AS PARTITION_NAME,                NULL AS SUBPARTITION_NAME,                NULL AS PARTITION_POSITION,                NULL AS SUBPARTITION_POSITION,                'TABLE' AS OBJECT_TYPE         FROM             oceanbase.__all_table T         WHERE T.TABLE_TYPE IN (0,2,3,6,14,15)         AND T.TABLE_MODE >> 12 & 15 in (0,1)         AND T.INDEX_ATTRIBUTES_SET & 16 = 0)     UNION ALL         SELECT T.DATABASE_ID,                 T.TABLE_ID,                 P.PART_ID,                 T.TABLE_NAME,                 P.PART_NAME,                 NULL,                 P.PART_IDX + 1,                 NULL,                 'PARTITION'         FROM             oceanbase.__all_table T           JOIN             oceanbase.__all_part P             ON T.TABLE_ID = P.TABLE_ID         WHERE T.TABLE_TYPE IN (0,2,3,6,14,15)               AND T.TABLE_MODE >> 12 & 15 in (0,1)               AND (P.PARTITION_TYPE = 0 OR P.PARTITION_TYPE IS NULL)               AND T.INDEX_ATTRIBUTES_SET & 16 = 0     UNION ALL         SELECT T.DATABASE_ID,                T.TABLE_ID,                SP.SUB_PART_ID AS PARTITION_ID,                T.TABLE_NAME,                  P.PART_NAME,                  SP.SUB_PART_NAME,                  P.PART_IDX + 1,                  SP.SUB_PART_IDX + 1,                  'SUBPARTITION'         FROM             oceanbase.__all_table T         JOIN             oceanbase.__all_part P             ON T.TABLE_ID = P.TABLE_ID         JOIN             oceanbase.__all_sub_part SP             ON T.TABLE_ID = SP.TABLE_ID             AND P.PART_ID = SP.PART_ID         WHERE T.TABLE_TYPE IN (0,2,3,6,14,15)               AND T.TABLE_MODE >> 12 & 15 in (0,1)               AND (P.PARTITION_TYPE = 0 OR P.PARTITION_TYPE IS NULL)               AND (SP.PARTITION_TYPE = 0 OR SP.PARTITION_TYPE IS NULL)               AND T.INDEX_ATTRIBUTES_SET & 16 = 0     ) V     JOIN         oceanbase.__all_database DB         ON DB.DATABASE_ID = V.DATABASE_ID     LEFT JOIN         oceanbase.__all_table_stat STAT         ON V.TABLE_ID = STAT.TABLE_ID         AND (V.PARTITION_ID = STAT.PARTITION_ID OR V.PARTITION_ID = -2)         AND STAT.INDEX_TYPE = 0 )__"))) {
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

int ObInnerTableSchema::dba_tab_col_statistics_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_TAB_COL_STATISTICS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_TAB_COL_STATISTICS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT   cast(db.database_name as CHAR(128)) as OWNER,   cast(tc.table_name as CHAR(128)) as  TABLE_NAME,   cast(tc.column_name as CHAR(128)) as  COLUMN_NAME,   cast(stat.distinct_cnt as NUMBER) as  NUM_DISTINCT,   cast(stat.min_value as CHAR(128)) as  LOW_VALUE,   cast(stat.max_value as CHAR(128)) as  HIGH_VALUE,   cast(stat.density as NUMBER) as  DENSITY,   cast(stat.null_cnt as NUMBER) as  NUM_NULLS,   cast(stat.bucket_cnt as NUMBER) as  NUM_BUCKETS,   cast(stat.last_analyzed as DATETIME(6)) as  LAST_ANALYZED,   cast(stat.sample_size as NUMBER) as  SAMPLE_SIZE,   CAST((CASE stat.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,   CAST((CASE stat.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,   cast(NULL as CHAR(80)) as  NOTES,   cast(stat.avg_len as NUMBER) as  AVG_COL_LEN,   cast((case when stat.histogram_type = 1 then 'FREQUENCY'         when stat.histogram_type = 3 then 'TOP-FREQUENCY'         when stat.histogram_type = 4 then 'HYBRID'         else NULL end) as CHAR(15)) as HISTOGRAM,   cast(NULL as CHAR(7)) SCOPE     FROM     (SELECT t.DATABASE_ID,             t.TABLE_ID,             t.TABLE_NAME,             c.COLUMN_ID,             c.COLUMN_NAME,             c.IS_HIDDEN           FROM             oceanbase.__all_virtual_core_all_table t,             oceanbase.__all_virtual_core_column_table c           WHERE c.table_id = t.table_id      UNION ALL      SELECT t.database_id,             t.table_id,             t.table_name,             c.COLUMN_ID,             c.COLUMN_NAME,             c.IS_HIDDEN       FROM oceanbase.__all_table t,            oceanbase.__all_column c       where t.table_type in (0,2,3,6,14)         and t.table_mode >> 12 & 15 in (0,1)         and t.index_attributes_set & 16 = 0         and c.table_id = t.table_id) tc   JOIN     oceanbase.__all_database db     ON db.database_id = tc.database_id   left join     oceanbase.__all_column_stat stat     ON tc.table_id = stat.table_id     AND tc.column_id = stat.column_id     AND stat.object_type = 1 WHERE   tc.is_hidden = 0 )__"))) {
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

int ObInnerTableSchema::dba_part_col_statistics_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_PART_COL_STATISTICS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_PART_COL_STATISTICS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT   cast(db.database_name as CHAR(128)) as OWNER,   cast(t.table_name as CHAR(128)) as  TABLE_NAME,   cast (part.part_name as CHAR(128)) as PARTITION_NAME,   cast(c.column_name as CHAR(128)) as  COLUMN_NAME,   cast(stat.distinct_cnt as NUMBER) as  NUM_DISTINCT,   cast(stat.min_value as CHAR(128)) as  LOW_VALUE,   cast(stat.max_value as CHAR(128)) as  HIGH_VALUE,   cast(stat.density as NUMBER) as  DENSITY,   cast(stat.null_cnt as NUMBER) as  NUM_NULLS,   cast(stat.bucket_cnt as NUMBER) as  NUM_BUCKETS,   cast(stat.last_analyzed as DATETIME(6)) as  LAST_ANALYZED,   cast(stat.sample_size as NUMBER) as  SAMPLE_SIZE,   CAST((CASE stat.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,   CAST((CASE stat.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,   cast(NULL as CHAR(80)) as  NOTES,   cast(stat.avg_len as NUMBER) as  AVG_COL_LEN,   cast((case when stat.histogram_type = 1 then 'FREQUENCY'         when stat.histogram_type = 3 then 'TOP-FREQUENCY'         when stat.histogram_type = 4 then 'HYBRID'         else NULL end) as CHAR(15)) as HISTOGRAM     FROM     oceanbase.__all_table t   JOIN     oceanbase.__all_database db     ON db.database_id = t.database_id   JOIN     oceanbase.__all_column c     ON c.table_id = t.table_id   JOIN     oceanbase.__all_part part     on t.table_id = part.table_id   left join     oceanbase.__all_column_stat stat     ON c.table_id = stat.table_id     AND c.column_id = stat.column_id     AND part.part_id = stat.partition_id     AND stat.object_type = 2 WHERE   c.is_hidden = 0   AND t.table_type in (0,3,6,14)   AND t.table_mode >> 12 & 15 in (0,1)   AND part.partition_type = 0   AND t.index_attributes_set & 16 = 0 )__"))) {
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

int ObInnerTableSchema::dba_subpart_col_statistics_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_SUBPART_COL_STATISTICS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_SUBPART_COL_STATISTICS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT   cast(db.database_name as CHAR(128)) as OWNER,   cast(t.table_name as CHAR(128)) as  TABLE_NAME,   cast (subpart.sub_part_name as CHAR(128)) as SUBPARTITION_NAME,   cast(c.column_name as CHAR(128)) as  COLUMN_NAME,   cast(stat.distinct_cnt as NUMBER) as  NUM_DISTINCT,   cast(stat.min_value as CHAR(128)) as  LOW_VALUE,   cast(stat.max_value as CHAR(128)) as  HIGH_VALUE,   cast(stat.density as NUMBER) as  DENSITY,   cast(stat.null_cnt as NUMBER) as  NUM_NULLS,   cast(stat.bucket_cnt as NUMBER) as  NUM_BUCKETS,   cast(stat.last_analyzed as DATETIME(6)) as  LAST_ANALYZED,   cast(stat.sample_size as NUMBER) as  SAMPLE_SIZE,   CAST((CASE stat.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,   CAST((CASE stat.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,   cast(NULL as CHAR(80)) as  NOTES,   cast(stat.avg_len as NUMBER) as  AVG_COL_LEN,   cast((case when stat.histogram_type = 1 then 'FREQUENCY'         when stat.histogram_type = 3 then 'TOP-FREQUENCY'         when stat.histogram_type = 4 then 'HYBRID'         else NULL end) as CHAR(15)) as HISTOGRAM     FROM     oceanbase.__all_table t   JOIN     oceanbase.__all_database db     ON db.database_id = t.database_id   JOIN     oceanbase.__all_column c     ON c.table_id = t.table_id   JOIN     oceanbase.__all_sub_part subpart     on t.table_id = subpart.table_id   left join     oceanbase.__all_column_stat stat     ON c.table_id = stat.table_id     AND c.column_id = stat.column_id     AND stat.partition_id = subpart.sub_part_id     AND stat.object_type = 3 WHERE   c.is_hidden = 0   AND t.table_type in (0,3,6,14)   AND t.table_mode >> 12 & 15 in (0,1)   AND subpart.partition_type = 0   AND t.index_attributes_set & 16 = 0 )__"))) {
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

int ObInnerTableSchema::dba_tab_histograms_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_TAB_HISTOGRAMS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_TAB_HISTOGRAMS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(select   cast(db.database_name as CHAR(128)) as OWNER,   cast(t.table_name as CHAR(128)) as  TABLE_NAME,   cast(c.column_name as CHAR(128)) as  COLUMN_NAME,   cast(hist.endpoint_num as NUMBER) as  ENDPOINT_NUMBER,   cast(NULL as NUMBER) as  ENDPOINT_VALUE,   cast(hist.endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE,   cast(hist.b_endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE_RAW,   cast(hist.endpoint_repeat_cnt as NUMBER) as ENDPOINT_REPEAT_COUNT,   cast(NULL as CHAR(7)) as SCOPE     FROM     (SELECT DATABASE_ID,             TABLE_ID,             TABLE_NAME           FROM             oceanbase.__all_virtual_core_all_table      UNION ALL      SELECT database_id,             table_id,             table_name       FROM oceanbase.__all_table where table_type in (0,3,6,14)       and table_mode >> 12 & 15 in (0,1)       and index_attributes_set & 16 = 0) t   JOIN     oceanbase.__all_database db     ON db.database_id = t.database_id   JOIN     oceanbase.__all_column c     ON c.table_id = t.table_id   JOIN     oceanbase.__all_histogram_stat hist     ON c.table_id = hist.table_id     AND c.column_id = hist.column_id     AND hist.object_type = 1 WHERE   c.is_hidden = 0 )__"))) {
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

int ObInnerTableSchema::dba_part_histograms_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_PART_HISTOGRAMS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_PART_HISTOGRAMS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(select     cast(db.database_name as CHAR(128)) as OWNER,     cast(t.table_name as CHAR(128)) as  TABLE_NAME,     cast(part.part_name as CHAR(128)) as PARTITION_NAME,     cast(c.column_name as CHAR(128)) as  COLUMN_NAME,     cast(hist.endpoint_num as NUMBER) as  ENDPOINT_NUMBER,     cast(NULL as NUMBER) as  ENDPOINT_VALUE,     cast(hist.endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE,     cast(hist.b_endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE_RAW,     cast(hist.endpoint_repeat_cnt as NUMBER) as ENDPOINT_REPEAT_COUNT     FROM       oceanbase.__all_table t     JOIN       oceanbase.__all_database db       ON db.database_id = t.database_id     JOIN       oceanbase.__all_column c       ON c.table_id = t.table_id     JOIN       oceanbase.__all_part part       on t.table_id = part.table_id     JOIN       oceanbase.__all_histogram_stat hist       ON c.table_id = hist.table_id       AND c.column_id = hist.column_id       AND part.part_id = hist.partition_id       AND hist.object_type = 2   WHERE     c.is_hidden = 0     AND t.table_type in (0,3,6,14)     AND t.table_mode >> 12 & 15 in (0,1)     AND part.partition_type = 0     AND t.index_attributes_set & 16 = 0   )__"))) {
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

int ObInnerTableSchema::dba_subpart_histograms_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_SUBPART_HISTOGRAMS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_SUBPART_HISTOGRAMS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(select     cast(db.database_name as CHAR(128)) as OWNER,     cast(t.table_name as CHAR(128)) as  TABLE_NAME,     cast(subpart.sub_part_name as CHAR(128)) as SUBPARTITION_NAME,     cast(c.column_name as CHAR(128)) as  COLUMN_NAME,     cast(hist.endpoint_num as NUMBER) as  ENDPOINT_NUMBER,     cast(NULL as NUMBER) as  ENDPOINT_VALUE,     cast(hist.endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE,     cast(hist.b_endpoint_value as CHAR(4000)) as ENDPOINT_ACTUAL_VALUE_RAW,     cast(hist.endpoint_repeat_cnt as NUMBER) as ENDPOINT_REPEAT_COUNT     FROM       oceanbase.__all_table t     JOIN       oceanbase.__all_database db       ON db.database_id = t.database_id     JOIN       oceanbase.__all_column c       ON c.table_id = t.table_id     JOIN       oceanbase.__all_sub_part subpart       on t.table_id = subpart.table_id     JOIN       oceanbase.__all_histogram_stat hist       ON c.table_id = hist.table_id       AND c.column_id = hist.column_id       AND hist.partition_id = subpart.sub_part_id       AND hist.object_type = 3   WHERE     c.is_hidden = 0     AND t.table_type in (0,3,6,14)     AND t.table_mode >> 12 & 15 in (0,1)     AND subpart.partition_type = 0     AND t.index_attributes_set & 16 = 0   )__"))) {
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

int ObInnerTableSchema::dba_tab_stats_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_TAB_STATS_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_TAB_STATS_HISTORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT     CAST(DB.DATABASE_NAME AS     CHAR(128)) AS OWNER,     CAST(V.TABLE_NAME       AS  CHAR(128)) AS TABLE_NAME,     CAST(V.PARTITION_NAME   AS  CHAR(128)) AS PARTITION_NAME,     CAST(V.SUBPARTITION_NAME  AS    CHAR(128)) AS SUBPARTITION_NAME,     CAST(STAT.SAVTIME AS DATETIME(6)) AS STATS_UPDATE_TIME     FROM     (       (SELECT DATABASE_ID,               TABLE_ID,               -2 AS PARTITION_ID,               TABLE_NAME,               NULL AS PARTITION_NAME,               NULL AS SUBPARTITION_NAME,               NULL AS PARTITION_POSITION,               NULL AS SUBPARTITION_POSITION,               'TABLE' AS OBJECT_TYPE           FROM             OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE       UNION ALL         SELECT DATABASE_ID,                TABLE_ID,                CASE WHEN PART_LEVEL = 0 THEN -2 ELSE -1 END AS PARTITION_ID,                TABLE_NAME,                NULL AS PARTITION_NAME,                NULL AS SUBPARTITION_NAME,                NULL AS PARTITION_POSITION,                 NULL AS SUBPARTITION_POSITION,                'TABLE' AS OBJECT_TYPE         FROM             oceanbase.__all_table T         WHERE T.TABLE_TYPE IN (0,3,6,14)         AND T.TABLE_MODE >> 12 & 15 in (0,1)         AND T.INDEX_ATTRIBUTES_SET & 16 = 0)     UNION ALL         SELECT T.DATABASE_ID,                 T.TABLE_ID,                 P.PART_ID,                 T.TABLE_NAME,                 P.PART_NAME,                 NULL,                 P.PART_IDX + 1,                 NULL,                 'PARTITION'         FROM             oceanbase.__all_table T           JOIN             oceanbase.__all_part P             ON T.TABLE_ID = P.TABLE_ID             AND T.TABLE_MODE >> 12 & 15 in (0,1)             AND T.INDEX_ATTRIBUTES_SET & 16 = 0         WHERE T.TABLE_TYPE IN (0,3,6,14)     UNION ALL         SELECT T.DATABASE_ID,                T.TABLE_ID,                SP.SUB_PART_ID AS PARTITION_ID,                T.TABLE_NAME,                  P.PART_NAME,                  SP.SUB_PART_NAME,                  P.PART_IDX + 1,                  SP.SUB_PART_IDX + 1,                  'SUBPARTITION'         FROM             oceanbase.__all_table T         JOIN             oceanbase.__all_part P             ON T.TABLE_ID = P.TABLE_ID             AND T.TABLE_MODE >> 12 & 15 in (0,1)             AND T.INDEX_ATTRIBUTES_SET & 16 = 0         JOIN             oceanbase.__all_sub_part SP             ON T.TABLE_ID = SP.TABLE_ID             AND P.PART_ID = SP.PART_ID         WHERE T.TABLE_TYPE IN (0,3,6,14)     ) V     JOIN         oceanbase.__all_database DB         ON DB.DATABASE_ID = V.DATABASE_ID     LEFT JOIN         oceanbase.__all_table_stat_history STAT         ON V.TABLE_ID = STAT.TABLE_ID         AND (V.PARTITION_ID = STAT.PARTITION_ID OR V.PARTITION_ID = -2)         AND STAT.INDEX_TYPE = 0 )__"))) {
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

int ObInnerTableSchema::dba_ind_statistics_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_IND_STATISTICS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_IND_STATISTICS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT     CAST(DB.DATABASE_NAME AS     CHAR(128)) AS OWNER,     CAST(V.INDEX_NAME AS     CHAR(128)) AS INDEX_NAME,     CAST(DB.DATABASE_NAME AS     CHAR(128)) AS TABLE_OWNER,     CAST(T.TABLE_NAME       AS  CHAR(128)) AS TABLE_NAME,     CAST(V.PARTITION_NAME   AS  CHAR(128)) AS PARTITION_NAME,     CAST(V.PARTITION_POSITION AS    NUMBER) AS PARTITION_POSITION,     CAST(V.SUBPARTITION_NAME  AS    CHAR(128)) AS SUBPARTITION_NAME,     CAST(V.SUBPARTITION_POSITION AS NUMBER) AS SUBPARTITION_POSITION,     CAST(V.OBJECT_TYPE AS   CHAR(12)) AS OBJECT_TYPE,     CAST(NULL AS    NUMBER) AS BLEVEL,     CAST(NULL AS    NUMBER) AS LEAF_BLOCKS,     CAST(NULL AS    NUMBER) AS DISTINCT_KEYS,     CAST(NULL AS    NUMBER) AS AVG_LEAF_BLOCKS_PER_KEY,     CAST(NULL AS    NUMBER) AS AVG_DATA_BLOCKS_PER_KEY,     CAST(NULL AS    NUMBER) AS CLUSTERING_FACTOR,     CAST(STAT.ROW_CNT AS    NUMBER) AS NUM_ROWS,     CAST(NULL AS    NUMBER) AS AVG_CACHED_BLOCKS,     CAST(NULL AS    NUMBER) AS AVG_CACHE_HIT_RATIO,     CAST(NULL AS    NUMBER) AS SAMPLE_SIZE,     CAST(STAT.LAST_ANALYZED AS DATETIME(6)) AS LAST_ANALYZED,     CAST((CASE STAT.GLOBAL_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS GLOBAL_STATS,     CAST((CASE STAT.USER_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS USER_STATS,     CAST((CASE WHEN STAT.STATTYPE_LOCKED & 15 IS NULL THEN NULL ELSE (CASE STAT.STATTYPE_LOCKED & 15 WHEN 0 THEN NULL WHEN 1 THEN 'DATA' WHEN 2 THEN 'CACHE' ELSE 'ALL' END) END) AS CHAR(5)) AS STATTYPE_LOCKED,     CAST((CASE STAT.STALE_STATS WHEN 0 THEN 'NO' WHEN 1 THEN 'YES' ELSE NULL END) AS CHAR(3)) AS STALE_STATS,     CAST(NULL AS    CHAR(7)) AS SCOPE     FROM     (         (SELECT DATABASE_ID,                 TABLE_ID,                 DATA_TABLE_ID,                 -2 AS PARTITION_ID,                 SUBSTR(TABLE_NAME, 7 + INSTR(SUBSTR(TABLE_NAME, 7), '_')) AS INDEX_NAME,                 NULL AS PARTITION_NAME,                 NULL AS SUBPARTITION_NAME,                 NULL AS PARTITION_POSITION,                 NULL AS SUBPARTITION_POSITION,                 'INDEX' AS OBJECT_TYPE           FROM             OCEANBASE.__ALL_VIRTUAL_CORE_ALL_TABLE T           WHERE T.TABLE_TYPE = 5 AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)         UNION ALL          SELECT DATABASE_ID,                 TABLE_ID,                 DATA_TABLE_ID,                 CASE WHEN PART_LEVEL = 0 THEN -2 ELSE -1 END AS PARTITION_ID,                 SUBSTR(TABLE_NAME, 7 + INSTR(SUBSTR(TABLE_NAME, 7), '_')) AS INDEX_NAME,                 NULL AS PARTITION_NAME,                 NULL AS SUBPARTITION_NAME,                 NULL AS PARTITION_POSITION,                 NULL AS SUBPARTITION_POSITION,                 'INDEX' AS OBJECT_TYPE         FROM             oceanbase.__all_table T         WHERE T.TABLE_TYPE = 5 AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)         AND T.TABLE_MODE >> 12 & 15 in (0,1)         AND T.INDEX_ATTRIBUTES_SET & 16 = 0)     UNION ALL         SELECT T.DATABASE_ID,                 T.TABLE_ID,                 T.DATA_TABLE_ID,                 P.PART_ID,                 SUBSTR(T.TABLE_NAME, 7 + INSTR(SUBSTR(T.TABLE_NAME, 7), '_')) AS INDEX_NAME,                 P.PART_NAME,                 NULL,                 P.PART_IDX + 1,                 NULL,                 'PARTITION'         FROM             oceanbase.__all_table T           JOIN             oceanbase.__all_part P             ON T.TABLE_ID = P.TABLE_ID         WHERE T.TABLE_TYPE = 5               AND P.PARTITION_TYPE = 0               AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)     UNION ALL         SELECT T.DATABASE_ID,                T.TABLE_ID,                T.DATA_TABLE_ID,                SP.SUB_PART_ID AS PARTITION_ID,                SUBSTR(T.TABLE_NAME, 7 + INSTR(SUBSTR(T.TABLE_NAME, 7), '_')) AS INDEX_NAME,                P.PART_NAME,                SP.SUB_PART_NAME,                P.PART_IDX + 1,                SP.SUB_PART_IDX + 1,                'SUBPARTITION'         FROM             oceanbase.__all_table T         JOIN             oceanbase.__all_part P             ON T.TABLE_ID = P.TABLE_ID         JOIN             oceanbase.__all_sub_part SP             ON T.TABLE_ID = SP.TABLE_ID             AND P.PART_ID = SP.PART_ID         WHERE T.TABLE_TYPE = 5               AND P.PARTITION_TYPE = 0               AND SP.PARTITION_TYPE = 0               AND T.INDEX_TYPE NOT IN (13, 14, 16, 17, 19, 20, 22)     ) V     JOIN oceanbase.__all_table T          ON T.TABLE_ID = V.DATA_TABLE_ID          AND T.DATABASE_ID = V.DATABASE_ID     JOIN         oceanbase.__all_database DB         ON DB.DATABASE_ID = V.DATABASE_ID     LEFT JOIN         oceanbase.__all_table_stat STAT         ON V.TABLE_ID = STAT.TABLE_ID         AND (V.PARTITION_ID = STAT.PARTITION_ID OR V.PARTITION_ID = -2)         AND STAT.INDEX_TYPE = 1 )__"))) {
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

int ObInnerTableSchema::gv_active_session_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_ACTIVE_SESSION_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_ACTIVE_SESSION_HISTORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT SAMPLE_ID, SAMPLE_TIME, CON_ID, USER_ID, SESSION_ID, SESSION_TYPE, SESSION_STATE, SQL_ID, PLAN_ID, TRACE_ID, EVENT, EVENT_NO, EVENT_ID, P1TEXT, P1, P2TEXT, P2, P3TEXT, P3, WAIT_CLASS, WAIT_CLASS_ID, TIME_WAITED, SQL_PLAN_LINE_ID, GROUP_ID, PLAN_HASH, THREAD_ID, STMT_TYPE, TIME_MODEL, IN_PARSE, IN_PL_PARSE, IN_PLAN_CACHE, IN_SQL_OPTIMIZE, IN_SQL_EXECUTION, IN_PX_EXECUTION, IN_SEQUENCE_LOAD, IN_COMMITTING, IN_STORAGE_READ, IN_STORAGE_WRITE, IN_REMOTE_DAS_EXECUTION, IN_FILTER_ROWS, IN_RPC_ENCODE, IN_RPC_DECODE, IN_CONNECTION_MGR, PROGRAM, MODULE, ACTION, CLIENT_ID, BACKTRACE, TM_DELTA_TIME, TM_DELTA_CPU_TIME, TM_DELTA_DB_TIME, TOP_LEVEL_SQL_ID, IN_PLSQL_COMPILATION, IN_PLSQL_EXECUTION, PLSQL_ENTRY_OBJECT_ID, PLSQL_ENTRY_SUBPROGRAM_ID, PLSQL_ENTRY_SUBPROGRAM_NAME, PLSQL_OBJECT_ID, PLSQL_SUBPROGRAM_ID, PLSQL_SUBPROGRAM_NAME, TX_ID, BLOCKING_SESSION_ID, TABLET_ID, PROXY_SID, DELTA_READ_IO_REQUESTS, DELTA_READ_IO_BYTES, DELTA_WRITE_IO_REQUESTS, DELTA_WRITE_IO_BYTES FROM oceanbase.GV$OB_ACTIVE_SESSION_HISTORY )__"))) {
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

int ObInnerTableSchema::v_active_session_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_ACTIVE_SESSION_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_ACTIVE_SESSION_HISTORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT SAMPLE_ID, SAMPLE_TIME, CON_ID, USER_ID, SESSION_ID, SESSION_TYPE, SESSION_STATE, SQL_ID, PLAN_ID, TRACE_ID, EVENT, EVENT_NO, EVENT_ID, P1TEXT, P1, P2TEXT, P2, P3TEXT, P3, WAIT_CLASS, WAIT_CLASS_ID, TIME_WAITED, SQL_PLAN_LINE_ID, GROUP_ID, PLAN_HASH, THREAD_ID, STMT_TYPE, TIME_MODEL, IN_PARSE, IN_PL_PARSE, IN_PLAN_CACHE, IN_SQL_OPTIMIZE, IN_SQL_EXECUTION, IN_PX_EXECUTION, IN_SEQUENCE_LOAD, IN_COMMITTING, IN_STORAGE_READ, IN_STORAGE_WRITE, IN_REMOTE_DAS_EXECUTION, IN_FILTER_ROWS, IN_RPC_ENCODE, IN_RPC_DECODE, IN_CONNECTION_MGR, PROGRAM, MODULE, ACTION, CLIENT_ID, BACKTRACE, TM_DELTA_TIME, TM_DELTA_CPU_TIME, TM_DELTA_DB_TIME, TOP_LEVEL_SQL_ID, IN_PLSQL_COMPILATION, IN_PLSQL_EXECUTION, PLSQL_ENTRY_OBJECT_ID, PLSQL_ENTRY_SUBPROGRAM_ID, PLSQL_ENTRY_SUBPROGRAM_NAME, PLSQL_OBJECT_ID, PLSQL_SUBPROGRAM_ID, PLSQL_SUBPROGRAM_NAME, TX_ID, BLOCKING_SESSION_ID, TABLET_ID, PROXY_SID, DELTA_READ_IO_REQUESTS, DELTA_READ_IO_BYTES, DELTA_WRITE_IO_REQUESTS, DELTA_WRITE_IO_BYTES FROM oceanbase.gv$active_session_history )__"))) {
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

int ObInnerTableSchema::gv_dml_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_GV_DML_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_GV_DML_STATS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT           CAST(1 AS SIGNED) AS INST_ID,           CAST(TABLE_ID AS SIGNED) AS OBJN,           CAST(INSERT_ROW_COUNT AS SIGNED) AS INS,           CAST(UPDATE_ROW_COUNT AS SIGNED) AS UPD,           CAST(DELETE_ROW_COUNT AS SIGNED) AS DEL,           CAST(NULL AS SIGNED) AS DROPSEG,           CAST(NULL AS SIGNED) AS CURROWS,           CAST(TABLET_ID AS SIGNED) AS PAROBJN,           CAST(NULL AS SIGNED) AS LASTUSED,           CAST(NULL AS SIGNED) AS FLAGS,           CAST(NULL AS SIGNED) AS CON_ID           FROM oceanbase.__all_virtual_dml_stats )__"))) {
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

int ObInnerTableSchema::v_dml_stats_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_V_DML_STATS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_V_DML_STATS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT     INST_ID,     OBJN,     INS,     UPD,     DEL,     DROPSEG,     CURROWS,     PAROBJN,     LASTUSED,     FLAGS,     CON_ID FROM oceanbase.GV$DML_STATS )__"))) {
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

int ObInnerTableSchema::dba_tab_modifications_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_TAB_MODIFICATIONS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_TAB_MODIFICATIONS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT   CAST(DB.DATABASE_NAME AS     CHAR(128)) AS TABLE_OWNER,   CAST(T.TABLE_NAME AS         CHAR(128)) AS TABLE_NAME,   CAST(P.PART_NAME AS     CHAR(128)) AS PARTITION_NAME,   CAST(SP.SUB_PART_NAME AS CHAR(128)) AS SUBPARTITION_NAME,   CAST(V.INSERTS AS     SIGNED) AS INSERTS,   CAST(V.UPDATES AS     SIGNED) AS UPDATES,   CAST(V.DELETES AS     SIGNED) AS DELETES,   CAST(V.MODIFIED_TIME AS DATE) AS TIMESTAMP,   CAST(NULL AS     CHAR(3)) AS TRUNCATED,   CAST(NULL AS     SIGNED) AS DROP_SEGMENTS   FROM     (SELECT      CASE WHEN T.TABLE_ID IS NOT NULL THEN T.TABLE_ID ELSE VT.TABLE_ID END AS TABLE_ID,      CASE WHEN T.TABLET_ID IS NOT NULL THEN T.TABLET_ID ELSE VT.TABLET_ID END AS TABLET_ID,       CASE WHEN T.TABLET_ID IS NOT NULL AND VT.TABLET_ID IS NOT NULL THEN T.INSERTS + VT.INSERT_ROW_COUNT - T.LAST_INSERTS ELSE        (CASE WHEN T.TABLET_ID IS NOT NULL THEN T.INSERTS - T.LAST_INSERTS ELSE VT.INSERT_ROW_COUNT END) END AS INSERTS,       CASE WHEN T.TABLET_ID IS NOT NULL AND VT.TABLET_ID IS NOT NULL THEN T.UPDATES + VT.UPDATE_ROW_COUNT - T.LAST_UPDATES  ELSE        (CASE WHEN T.TABLET_ID IS NOT NULL THEN T.UPDATES - T.LAST_UPDATES  ELSE VT.UPDATE_ROW_COUNT END) END AS UPDATES,       CASE WHEN T.TABLET_ID IS NOT NULL AND VT.TABLET_ID IS NOT NULL THEN T.DELETES + VT.DELETE_ROW_COUNT - T.LAST_DELETES ELSE        (CASE WHEN T.TABLET_ID IS NOT NULL THEN T.DELETES - T.LAST_DELETES ELSE VT.DELETE_ROW_COUNT END) END AS DELETES,       CASE WHEN T.GMT_MODIFIED IS NOT NULL THEN T.GMT_MODIFIED ELSE NULL END AS MODIFIED_TIME      FROM      OCEANBASE.__ALL_MONITOR_MODIFIED T      FULL JOIN      OCEANBASE.__ALL_VIRTUAL_DML_STATS VT      ON T.TABLET_ID = VT.TABLET_ID     )V     JOIN OCEANBASE.__ALL_TABLE T          ON V.TABLE_ID = T.TABLE_ID          AND T.TABLE_TYPE in (0, 3, 6)          AND T.TABLE_MODE >> 12 & 15 in (0,1)          AND T.INDEX_ATTRIBUTES_SET & 16 = 0     JOIN         OCEANBASE.__ALL_DATABASE DB         ON DB.DATABASE_ID = T.DATABASE_ID     LEFT JOIN         OCEANBASE.__ALL_PART P         ON V.TABLE_ID = P.TABLE_ID         AND V.TABLET_ID = P.TABLET_ID     LEFT JOIN         OCEANBASE.__ALL_SUB_PART SP         ON V.TABLE_ID = SP.TABLE_ID         AND V.TABLET_ID = SP.TABLET_ID   )__"))) {
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

int ObInnerTableSchema::dba_scheduler_jobs_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_SCHEDULER_JOBS_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_SCHEDULER_JOBS_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(SELECT     CAST(T.POWNER AS CHAR(128)) AS OWNER,     CAST(T.JOB_NAME AS CHAR(128)) AS JOB_NAME,     CAST(NULL AS CHAR(128)) AS JOB_SUBNAME,     CAST(T.JOB_STYLE AS CHAR(17)) AS JOB_STYLE,     CAST(NULL AS CHAR(128)) AS JOB_CREATOR,     CAST(NULL AS CHAR(65)) AS CLIENT_ID,     CAST(NULL AS CHAR(33)) AS GLOBAL_UID,     CAST(T.POWNER AS CHAR(4000)) AS PROGRAM_OWNER,     CAST(T.PROGRAM_NAME AS CHAR(4000)) AS PROGRAM_NAME,     CAST(T.JOB_TYPE AS CHAR(16)) AS JOB_TYPE,     CAST(T.JOB_ACTION AS CHAR(4000)) AS JOB_ACTION,     CAST(T.NUMBER_OF_ARGUMENT AS SIGNED) AS NUMBER_OF_ARGUMENTS,     CAST(NULL AS CHAR(4000)) AS SCHEDULE_OWNER,     CAST(NULL AS CHAR(4000)) AS SCHEDULE_NAME,     CAST(NULL AS CHAR(12)) AS SCHEDULE_TYPE,     CAST(T.START_DATE AS DATETIME(6)) AS START_DATE,     CAST(T.REPEAT_INTERVAL AS CHAR(4000)) AS REPEAT_INTERVAL,     CAST(NULL AS CHAR(128)) AS EVENT_QUEUE_OWNER,     CAST(NULL AS CHAR(128)) AS EVENT_QUEUE_NAME,     CAST(NULL AS CHAR(523)) AS EVENT_QUEUE_AGENT,     CAST(NULL AS CHAR(4000)) AS EVENT_CONDITION,     CAST(NULL AS CHAR(261)) AS EVENT_RULE,     CAST(NULL AS CHAR(261)) AS FILE_WATCHER_OWNER,     CAST(NULL AS CHAR(261)) AS FILE_WATCHER_NAME,     CAST(T.END_DATE AS DATETIME(6)) AS END_DATE,     CAST(T.JOB_CLASS AS CHAR(128)) AS JOB_CLASS,     CAST(T.ENABLED AS CHAR(5)) AS ENABLED,     CAST(T.AUTO_DROP AS CHAR(5)) AS AUTO_DROP,     CAST(NULL AS CHAR(5)) AS RESTART_ON_RECOVERY,     CAST(NULL AS CHAR(5)) AS RESTART_ON_FAILURE,     CAST(T.STATE AS CHAR(15)) AS STATE,     CAST(NULL AS SIGNED) AS JOB_PRIORITY,     CAST(T.RUN_COUNT AS SIGNED) AS RUN_COUNT,     CAST(NULL AS SIGNED) AS MAX_RUNS,     CAST(T.FAILURES AS SIGNED) AS FAILURE_COUNT,     CAST(NULL AS SIGNED) AS MAX_FAILURES,     CAST(T.RETRY_COUNT AS SIGNED) AS RETRY_COUNT,     CAST(T.LAST_DATE AS DATETIME(6)) AS LAST_START_DATE,     CAST(T.LAST_RUN_DURATION AS SIGNED) AS LAST_RUN_DURATION,     CAST(T.NEXT_DATE AS DATETIME(6)) AS NEXT_RUN_DATE,     CAST(NULL AS SIGNED) AS SCHEDULE_LIMIT,     CAST(T.MAX_RUN_DURATION AS SIGNED) AS MAX_RUN_DURATION,     CAST(NULL AS CHAR(11)) AS LOGGING_LEVEL,     CAST(NULL AS CHAR(5)) AS STORE_OUTPUT,     CAST(NULL AS CHAR(5)) AS STOP_ON_WINDOW_CLOSE,     CAST(NULL AS CHAR(5)) AS INSTANCE_STICKINESS,     CAST(NULL AS CHAR(4000)) AS RAISE_EVENTS,     CAST(NULL AS CHAR(5)) AS SYSTEM,     CAST(NULL AS SIGNED) AS JOB_WEIGHT,     CAST(T.NLSENV AS CHAR(4000)) AS NLS_ENV,     CAST(NULL AS CHAR(128)) AS SOURCE,     CAST(NULL AS SIGNED) AS NUMBER_OF_DESTINATIONS,     CAST(NULL AS CHAR(261)) AS DESTINATION_OWNER,     CAST(NULL AS CHAR(261)) AS DESTINATION,     CAST(NULL AS CHAR(128)) AS CREDENTIAL_OWNER,     CAST(NULL AS CHAR(128)) AS CREDENTIAL_NAME,     CAST(T.FIELD1 AS CHAR(128)) AS INSTANCE_ID,     CAST(NULL AS CHAR(5)) AS DEFERRED_DROP,     CAST(NULL AS CHAR(5)) AS ALLOW_RUNS_IN_RESTRICTED_MODE,     CAST(T.COMMENTS AS CHAR(4000)) AS COMMENTS,     CAST(T.FLAG AS SIGNED) AS FLAGS,     CAST(NULL AS CHAR(5)) AS RESTARTABLE,     CAST(NULL AS CHAR(128)) AS CONNECT_CREDENTIAL_OWNER,     CAST(NULL AS CHAR(128)) AS CONNECT_CREDENTIAL_NAME   FROM oceanbase.__all_tenant_scheduler_job T WHERE T.JOB_NAME != '__dummy_guard' and T.JOB > 0 )__"))) {
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

int ObInnerTableSchema::dba_ob_outline_concurrent_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_OUTLINE_CONCURRENT_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_OUTLINE_CONCURRENT_HISTORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       GMT_CREATE AS CREATE_TIME,       GMT_MODIFIED AS MODIFY_TIME,       DATABASE_ID,       OUTLINE_ID,       NAME AS OUTLINE_NAME,       SQL_TEXT,       OUTLINE_PARAMS,       OUTLINE_TARGET,       CAST(SQL_ID AS CHAR(32)) AS SQL_ID,       OUTLINE_CONTENT,       CASE WHEN IS_DELETED = 1 THEN 'YES' ELSE 'NO' END AS IS_DELETED,       CASE WHEN ENABLED = 1 THEN 'YES' ELSE 'NO' END AS ENABLED     FROM oceanbase.__all_outline_history )__"))) {
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

int ObInnerTableSchema::dba_ob_outlines_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_OUTLINES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_OUTLINES_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       B.GMT_CREATE AS CREATE_TIME,       B.GMT_MODIFIED AS MODIFY_TIME,       A.DATABASE_ID,       A.OUTLINE_ID,       A.DATABASE_NAME,       A.OUTLINE_NAME,       A.VISIBLE_SIGNATURE,       A.SQL_TEXT,       A.OUTLINE_TARGET,       A.OUTLINE_SQL,       A.SQL_ID,       A.OUTLINE_CONTENT     FROM oceanbase.__tenant_virtual_outline A, oceanbase.__all_outline B     WHERE A.OUTLINE_ID = B.OUTLINE_ID AND B.FORMAT_OUTLINE = 0 )__"))) {
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

int ObInnerTableSchema::dba_ob_concurrent_limit_sql_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_CONCURRENT_LIMIT_SQL_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_CONCURRENT_LIMIT_SQL_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(     SELECT       B.GMT_CREATE AS CREATE_TIME,       B.GMT_MODIFIED AS MODIFY_TIME,       A.DATABASE_ID,       A.OUTLINE_ID,       A.DATABASE_NAME,       A.OUTLINE_NAME,       A.OUTLINE_CONTENT,       A.VISIBLE_SIGNATURE,       A.SQL_TEXT,       A.CONCURRENT_NUM,       A.LIMIT_TARGET     FROM oceanbase.__tenant_virtual_concurrent_limit_sql A, oceanbase.__all_outline B     WHERE A.OUTLINE_ID = B.OUTLINE_ID )__"))) {
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

int ObInnerTableSchema::dba_ob_deadlock_event_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_DBA_OB_DEADLOCK_EVENT_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_DBA_OB_DEADLOCK_EVENT_HISTORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT EVENT_ID,          DETECTOR_ID,          REPORT_TIME,          CYCLE_IDX,          CYCLE_SIZE,          ROLE,          PRIORITY_LEVEL,          PRIORITY,          CREATE_TIME,          START_DELAY AS START_DELAY_US,          MODULE,          VISITOR,          OBJECT,          EXTRA_NAME1,          EXTRA_VALUE1,          EXTRA_NAME2,          EXTRA_VALUE2,          EXTRA_NAME3,          EXTRA_VALUE3   FROM OCEANBASE.__ALL_VIRTUAL_DEADLOCK_EVENT_HISTORY   )__"))) {
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

int ObInnerTableSchema::cdb_ob_deadlock_event_history_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_OB_DEADLOCK_EVENT_HISTORY_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_OB_DEADLOCK_EVENT_HISTORY_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT EVENT_ID,          DETECTOR_ID,          REPORT_TIME,          CYCLE_IDX,          CYCLE_SIZE,          ROLE,          PRIORITY_LEVEL,          PRIORITY,          CREATE_TIME,          START_DELAY AS START_DELAY_US,          MODULE,          VISITOR,          OBJECT,          EXTRA_NAME1,          EXTRA_VALUE1,          EXTRA_NAME2,          EXTRA_VALUE2,          EXTRA_NAME3,          EXTRA_VALUE3   FROM OCEANBASE.__ALL_VIRTUAL_DEADLOCK_EVENT_HISTORY   )__"))) {
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

int ObInnerTableSchema::cdb_ob_sys_variables_schema(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  uint64_t column_id = OB_APP_MIN_COLUMN_ID - 1;

  //generated fields:
  table_schema.set_tenant_id(OB_SYS_TENANT_ID);
  table_schema.set_tablegroup_id(OB_INVALID_ID);
  table_schema.set_database_id(OB_SYS_DATABASE_ID);
  table_schema.set_table_id(OB_CDB_OB_SYS_VARIABLES_TID);
  table_schema.set_rowkey_split_pos(0);
  table_schema.set_is_use_bloomfilter(false);
  table_schema.set_progressive_merge_num(0);
  table_schema.set_rowkey_column_num(0);
  table_schema.set_load_type(TABLE_LOAD_TYPE_IN_DISK);
  table_schema.set_table_type(SYSTEM_VIEW);
  table_schema.set_index_type(INDEX_TYPE_IS_NOT);
  table_schema.set_def_type(TABLE_DEF_TYPE_INTERNAL);

  if (OB_SUCC(ret)) {
    if (OB_FAIL(table_schema.set_table_name(OB_CDB_OB_SYS_VARIABLES_TNAME))) {
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
    if (OB_FAIL(table_schema.set_view_definition(R"__(   SELECT     a.GMT_CREATE AS CREATE_TIME,     a.GMT_MODIFIED AS MODIFY_TIME,     a.NAME as NAME,     a.VALUE as VALUE,     a.MIN_VAL as MIN_VALUE,     a.MAX_VAL as MAX_VALUE,     CASE a.FLAGS & 0x3         WHEN 1 THEN "GLOBAL_ONLY"         WHEN 2 THEN "SESSION_ONLY"         WHEN 3 THEN "GLOBAL | SESSION"         ELSE NULL     END as SCOPE,     a.INFO as INFO,     b.DEFAULT_VALUE as DEFAULT_VALUE,     CAST (CASE WHEN a.VALUE = b.DEFAULT_VALUE           THEN 'YES'           ELSE 'NO'           END AS CHAR(3)) AS ISDEFAULT   FROM oceanbase.__all_virtual_sys_variable a   join oceanbase.__all_virtual_sys_variable_default_value b   where a.name = b.variable_name;   )__"))) {
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
