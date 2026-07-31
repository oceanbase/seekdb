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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_SHOW_TABLE_STATUS_SQL_H_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_SHOW_TABLE_STATUS_SQL_H_

#define NEW_TABLE_STATUS_SQL  "select /*+ leading(a) no_use_nl(ts)*/" \
                    "a.database_id as DATABASE_ID," \
                    "a.table_name as TABLE_NAME," \
                    "a.table_type as TABLE_TYPE," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE 'oceanbase' END as ENGINE," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE 0 END as VERSION," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE store_format END as ROW_FORMAT," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE cast( coalesce(ts.row_cnt,0) as unsigned) END as ROWS," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE cast( coalesce(ts.avg_row_len, 0) as unsigned) END as AVG_ROW_LENGTH," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE cast( coalesce(ts.data_size,0) as unsigned) END as DATA_LENGTH," \
                    "NULL as MAX_DATA_LENGTH," \
                    "NULL as INDEX_LENGTH," \
                    "NULL as DATA_FREE," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE inner_table_sequence_getter(a.table_id, a.autoinc_column_id) END as AUTO_INCREMENT," \
                    "a.gmt_create as CREATE_TIME," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE a.gmt_modified END as UPDATE_TIME," \
                    "NULL as CHECK_TIME," \
                    "CASE a.collation_type " \
                    "WHEN 45 THEN 'utf8mb4_general_ci' " \
                    "WHEN 46 THEN 'utf8mb4_bin' " \
                    "WHEN 63 THEN 'binary' " \
                    "ELSE NULL END as COLLATION," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE 0 END as CHECKSUM," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE inner_table_option_printer(a.database_id, a.table_id) END as CREATE_OPTIONS," \
                    "CASE a.table_type WHEN 4 THEN 'VIEW' ELSE a.comment END as COMMENT " \
                    "from " \
                    "(" \
                    "select " \
                    "database_id," \
                    "table_id," \
                    "table_name, " \
                    "table_type," \
                    "gmt_create," \
                    "gmt_modified," \
                    "store_format," \
                    "CASE table_type WHEN 4 THEN NULL ELSE collation_type END as collation_type," \
                    "comment," \
                    "autoinc_column_id " \
                    "from oceanbase.__all_table where table_type in (0, 1, 2, 3, 4, 14)) a " \
                    "join oceanbase.__all_database b " \
                    "on a.database_id = b.database_id " \
                    "left join (" \
                    "select " \
                    "table_id," \
                    "row_cnt," \
                    "avg_row_len," \
                    "row_cnt * avg_row_len as data_size " \
                    "from oceanbase.__all_table_stat " \
                    "where partition_id = -1 or partition_id = table_id) ts " \
                    "on a.table_id = ts.table_id " \
                    "where a.table_type in (0, 1, 2, 3, 4, 14) " \
                    "and b.database_name != '__recyclebin' " \
                    "and b.in_recyclebin = 0 " \
                    "and 0 = sys_privilege_check('table_acc', 1, b.database_name, a.table_name) "

#define NEW_TABLE_STATUS_SQL_ORA  "select /*+ leading(a) no_use_nl(ts)*/" \
                    "a.database_id as DATABASE_ID," \
                    "a.table_name as TABLE_NAME," \
                    "a.table_type as TABLE_TYPE," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE 'oceanbase' END as ENGINE," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE 0 END as VERSION," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE store_format END as ROW_FORMAT," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE coalesce(ts.row_cnt,0) END as \"ROWS\"," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE coalesce(ts.avg_row_len, 0) END as AVG_ROW_LENGTH," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE coalesce(ts.data_size,0) END as DATA_LENGTH," \
                    "NULL as MAX_DATA_LENGTH," \
                    "NULL as INDEX_LENGTH," \
                    "NULL as DATA_FREE," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE inner_table_sequence_getter(a.table_id, a.autoinc_column_id) END as AUTO_INCREMENT," \
                    "a.gmt_create as CREATE_TIME," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE a.gmt_modified END as UPDATE_TIME," \
                    "NULL as CHECK_TIME," \
                    "CASE a.collation_type " \
                    "WHEN 45 THEN 'utf8mb4_general_ci' " \
                    "WHEN 46 THEN 'utf8mb4_bin' " \
                    "WHEN 63 THEN 'binary' " \
                    "ELSE NULL END as COLLATION," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE 0 END as CHECKSUM," \
                    "CASE a.table_type WHEN 4 THEN NULL ELSE inner_table_option_printer(a.database_id, a.table_id) END as CREATE_OPTIONS," \
                    "CASE a.table_type WHEN 4 THEN 'VIEW' ELSE a.cmt END as \"COMMENT\" " \
                    "from " \
                    "(" \
                    "select " \
                    "database_id," \
                    "table_id," \
                    "table_name, " \
                    "table_type," \
                    "gmt_create," \
                    "gmt_modified," \
                    "store_format," \
                    "CASE table_type WHEN 4 THEN NULL ELSE collation_type END as collation_type," \
                    "\"COMMENT\" as cmt," \
                    "autoinc_column_id " \
                    "from SYS.ALL_VIRTUAL_TABLE_REAL_AGENT where table_type in (0, 1, 2, 3, 4, 14)) a " \
                    "join SYS.ALL_VIRTUAL_DATABASE_REAL_AGENT b " \
                    "on a.database_id = b.database_id " \
                    "left join (" \
                    "select " \
                    "table_id," \
                    "row_cnt," \
                    "avg_row_len," \
                    "row_cnt * avg_row_len as data_size " \
                    "from SYS.ALL_VIRTUAL_TABLE_STAT_REAL_AGENT " \
                    "where partition_id = -1 or partition_id = table_id) ts " \
                    "on a.table_id = ts.table_id " \
                    "where a.table_type in (0, 1, 2, 3, 4, 14) " \
                    "and b.database_name != '__recyclebin' " \
                    "and b.in_recyclebin = 0 " \
                    "and (a.database_id = USERENV('SCHEMAID') or USER_CAN_ACCESS_OBJ(1, a.table_id, a.database_id)=1)"

#endif // OCEANBASE_SQL_RESOLVER_CMD_OB_SHOW_TABLE_STATUS_SQL_H_
