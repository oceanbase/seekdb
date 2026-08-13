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
#include "ob_partition_sql_helper.h"

#include <stdio.h>
#include <string.h>

#include "share/ob_timezone_mgr.h"
#include "lib/net/ob_addr.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_level.h"
#include "lib/oblog/ob_log_print_kv.h"
#include "lib/utility/ob_smart_var.h"
#include "lib/utility/utility.h"
#include "mysqlclient/ob_isql_client.h"
#include "object/ob_object.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/schema/ob_schema_utils.h"
#include "timezone/ob_timezone_info.h"

namespace oceanbase {
namespace common {
class ObNewRow;
class ObRowkey;
template <class T> class ObIArray;
}  // namespace common
}  // namespace oceanbase

namespace oceanbase
{
using namespace obcall;
namespace share
{
namespace schema
{
using namespace common;

int ObPartDMLGenerator::gen_dml(ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  PartInfo part_info;
  if (OB_FAIL(extract_part_info(part_info))) {
  } else if (OB_FAIL(convert_to_dml(part_info, dml))) {
  }
  return ret;
}

int ObPartDMLGenerator::gen_high_bound_val_str(
    const ObRowkey &high_bound_val,
    ObString &high_bound_val_str,
    ObString &b_high_bound_val_str)
{
  int ret = OB_SUCCESS;

  int64_t pos = 0;
  //TODO:@yanhua add session timezone_info is better
  ObTimeZoneInfo tz_info;
  tz_info.set_offset(0);
  if (OB_FAIL(OTTZ_MGR.get_timezone_map(tz_info.get_tz_map_wrap()))) {
  } else if (OB_FAIL(ObPartitionUtils::convert_rowkey_to_sql_literal(
             high_bound_val, high_bound_val_,
             OB_MAX_B_HIGH_BOUND_VAL_LENGTH, pos, false, &tz_info))) {
  } else {
    high_bound_val_str.assign_ptr(high_bound_val_, static_cast<int32_t>(pos));
  }
  if (OB_SUCC(ret)) {
    pos = 0;
    if (OB_FAIL(ObPartitionUtils::convert_rowkey_to_hex(
        high_bound_val, b_high_bound_val_,
        OB_MAX_B_HIGH_BOUND_VAL_LENGTH, pos))) {
    } else {
      b_high_bound_val_str.assign_ptr(b_high_bound_val_, static_cast<int32_t>(pos));
    }
  }

  return ret;
}
int ObPartDMLGenerator::gen_list_val_str(
    const common::ObIArray<common::ObNewRow>& list_value,
    common::ObString &list_val_str,
    common::ObString &b_list_val_str)
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  //TODO:@yanhua add session timezone_info is better
  ObTimeZoneInfo tz_info;
  tz_info.set_offset(0);
  if (OB_FAIL(OTTZ_MGR.get_timezone_map(tz_info.get_tz_map_wrap()))) {
  } else if (OB_FAIL(ObPartitionUtils::convert_rows_to_sql_literal(
             list_value, list_val_,
             OB_MAX_B_PARTITION_EXPR_LENGTH, pos, false, &tz_info))) {
  } else {
    list_val_str.assign_ptr(list_val_, static_cast<int32_t>(pos));
  }
  pos = 0;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObPartitionUtils::convert_rows_to_hex(list_value, b_list_val_,
                                                           OB_MAX_B_PARTITION_EXPR_LENGTH, pos))) {
  } else {
    b_list_val_str.assign_ptr(b_list_val_, static_cast<int32_t>(pos));
  }
  return ret;
}

int ObPartSqlHelper::init(const ObPartitionSchema *table)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tables_.empty())) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K(tables_));
  } else if (OB_FAIL(tables_.push_back(table))) {
  }
  return ret;
}

int ObPartSqlHelper::init(ObIArray<const ObPartitionSchema *> &tables)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tables_.empty())) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret), K(tables_));
  } else if (OB_FAIL(tables_.assign(tables))) {
  }
  return ret;
}

int ObPartSqlHelper::write_batch_sql_(const bool only_history, BatchInsertCtx &ctx)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  
  if (!only_history && !ctx.sql_.empty()) {
    if (OB_FAIL(sql_client_.write(ctx.sql_.ptr(), affected_rows))) {
    } else if (affected_rows != ctx.count_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("affected_rows is unexpected", K(ret), K(ctx), K(affected_rows));
    }
  }
  if (OB_SUCC(ret) && !ctx.history_sql_.empty()) {
    affected_rows = 0;
    if (OB_FAIL(sql_client_.write(ctx.history_sql_.ptr(), affected_rows))) {
    } else if (affected_rows != ctx.count_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("history affected_rows is unexpected", K(ret), K(ctx), K(affected_rows));
    }
  }
  ctx.reset();
  return ret;
}

int ObPartSqlHelper::generate_batch_sql_(const ObDMLSqlSplicer &dml,
    const char *table_name,
    ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  if (sql.empty()) {
    if (OB_FAIL(dml.splice_insert_sql(table_name, sql))) {
    }
  } else {
    ObSqlString value_str;
    if (OB_FAIL(dml.splice_values(value_str))) {
    } else if (OB_FAIL(sql.append_fmt(", (%s)", value_str.ptr()))) {
    }
  }
  return ret;
}

int ObPartSqlHelper::generate_and_batch_write_sqls_(
    ObDMLSqlSplicer &dml,
    const bool only_history,
    const char *table_name,
    const char *history_table_name,
    BatchInsertCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (!only_history) {
    if (OB_FAIL(generate_batch_sql_(dml, table_name, ctx.sql_))) {
    }
  }
  if (FAILEDx(dml.add_column("is_deleted", is_deleted() ? 1 : 0))) {
    LOG_WARN("add column failed", K(ret));
  } else if (OB_FAIL(generate_batch_sql_(dml, history_table_name, ctx.history_sql_))) {
  } else if (FALSE_IT(ctx.count_++)) {
  } else if (ctx.count_ >= MAX_DML_NUM && OB_FAIL(write_batch_sql_(only_history, ctx))) {
    LOG_WARN("failed to write batch sql", KR(ret), K(only_history), K(ctx));
  }
  return ret;
}

int ObPartSqlHelper::iterate_all_part_(const bool only_history, const ObPartitionSchema *table,
                                       BatchInsertCtx &ctx, const bool include_hidden)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_user_partition_table()) {
    
    
    ObDMLSqlSplicer dml;
    const ObPartitionOption &part_expr = table->get_part_option();
    int64_t part_num = part_expr.get_part_num();
    ObPartition **part_array = table->get_part_array();
    ObPartition **hidden_part_array = table->get_hidden_part_array();
    int64_t hidden_part_num = include_hidden ? table->get_hidden_partition_num() : 0;
    int64_t total_part_num = part_num + hidden_part_num; 
    if (OB_ISNULL(part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part array is null", K(ret), KP(part_array));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < total_part_num; i++) {
      dml.reset();
      ObPartition *part = NULL;
      if (i < part_num) {
        part = part_array[i];
      } else if (OB_ISNULL(hidden_part_array)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("hidden part array is null", K(ret), KP(hidden_part_array)); 
      } else {
        part = hidden_part_array[i - part_num];
      }
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("part is null", K(ret), K(i), K(part_num), K(hidden_part_num));
      }
      if (FAILEDx(add_part_dml_column(table, *part, dml))) {
        LOG_WARN("add dml column failed", K(ret), K(*part));
      } else if (OB_FAIL(generate_and_batch_write_sqls_(dml, only_history, OB_ALL_PART_TNAME,
              OB_ALL_PART_HISTORY_TNAME, ctx))) {
      }
    }
  }
  return ret;
}

int ObPartSqlHelper::iterate_all_sub_part_(const bool only_history,
    const ObPartitionSchema *table, BatchInsertCtx &ctx, const bool include_hidden)
{
  int ret = OB_SUCCESS;
  UNUSED(include_hidden);
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_user_subpartition_table()) {
    
    
    ObDMLSqlSplicer dml;
    int64_t part_num = table->get_part_option().get_part_num();
    ObPartition **part_array = table->get_part_array();
    ObSubPartition **subpart_array = NULL;
    ObSubPartition *subpart = NULL;
    int64_t sub_part_num = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < part_num; i++) {
      int64_t part_id = -1;
      if (OB_ISNULL(part_array) || OB_ISNULL(part_array[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("subpart_array is invalid", KR(ret));
      } else {
        subpart_array = part_array[i]->get_subpart_array();
        sub_part_num = part_array[i]->get_subpartition_num();
        part_id = part_array[i]->get_part_id();
        if (part_id < 0 || sub_part_num < 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("part_id or sub_part_num is invalid", K(ret), K(part_id), K(sub_part_num));
        }
      }
      for (int64_t j = 0; OB_SUCC(ret) && j < sub_part_num; j++) {
        dml.reset();
        int64_t sub_part_id = -1;
        if (OB_ISNULL(subpart_array) || OB_ISNULL(subpart_array[j])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("subpart_array is invalid", KR(ret));
        } else {
          subpart = subpart_array[j];
          sub_part_id = subpart->get_sub_part_id();
          if (sub_part_id < 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("sub_part_id or is invalid", K(ret), K(sub_part_id));
          } else if (OB_FAIL(add_subpart_dml_column(table, part_id, sub_part_id, *subpart, dml))) {
          } else if (OB_FAIL(generate_and_batch_write_sqls_(dml, only_history,
                  OB_ALL_SUB_PART_TNAME, OB_ALL_SUB_PART_HISTORY_TNAME, ctx))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPartSqlHelper::iterate_all_def_sub_part_(const bool only_history,
    const ObPartitionSchema *table, BatchInsertCtx &ctx, const bool include_hidden)
{
  int ret = OB_SUCCESS;
  UNUSED(include_hidden);
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_user_subpartition_table()
             && table->has_sub_part_template_def()) {
    
    
    ObDMLSqlSplicer dml;
    const int64_t def_sub_part_num = table->get_sub_part_option().get_part_num();
    ObSubPartition **def_subpart_array = table->get_def_subpart_array();
    for (int64_t j = 0; OB_SUCC(ret) && j < def_sub_part_num; j++) {
      dml.reset();
      if (OB_ISNULL(def_subpart_array) || OB_ISNULL(def_subpart_array[j])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("def_subpart is null", KR(ret), KP(def_subpart_array), K(j));
      } else if (OB_FAIL(add_def_subpart_dml_column(
                 table, j, *(def_subpart_array[j]), dml))) {
      } else if (OB_FAIL(generate_and_batch_write_sqls_(dml, only_history,
              OB_ALL_DEF_SUB_PART_TNAME, OB_ALL_DEF_SUB_PART_HISTORY_TNAME, ctx))) {
      }
    }
  }
  return ret;
}

int ObPartSqlHelper::iterate_part_info_(const bool only_history,
    const ObPartitionSchema *table, BatchInsertCtx &ctx, const bool include_hidden)
{
  int ret = OB_SUCCESS;
  UNUSED(include_hidden);
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_user_partition_table()) {
    ObDMLSqlSplicer dml;
    
    
    if (OB_FAIL(add_part_info_dml_column(table, dml))) {
    } else if (OB_FAIL(generate_and_batch_write_sqls_(dml, only_history,
            OB_ALL_PART_INFO_TNAME, OB_ALL_PART_INFO_HISTORY_TNAME, ctx))) {
    }
  }
  return ret;
}

#define ITERATE_ALL_TABLE(func) \
  int ObPartSqlHelper::func(const bool only_history, const bool include_hidden) \
  { \
    int ret = OB_SUCCESS; \
    BatchInsertCtx ctx; \
    for (int64_t i = 0; i < tables_.count() && OB_SUCC(ret); i++) { \
      if (OB_FAIL(func##_(only_history, tables_.at(i), ctx, include_hidden))) { \
        LOG_WARN("failed to " #func, K(ret), K(only_history), KPC(tables_.at(i)), K(ctx)); \
      } \
    } \
    if (FAILEDx(write_batch_sql_(only_history, ctx))) { \
      LOG_WARN("failed to write batch sql", KR(ret), K(only_history), K(ctx)); \
    } \
    return ret; \
  }

ITERATE_ALL_TABLE(iterate_part_info);
ITERATE_ALL_TABLE(iterate_all_part);
ITERATE_ALL_TABLE(iterate_all_sub_part);
ITERATE_ALL_TABLE(iterate_all_def_sub_part);

#undef ITERATE_ALL_TABLE

int ObAddPartInfoHelper::add_partition_info()
{
  int ret = OB_SUCCESS;
  const bool is_only_history = false;
  const bool is_include_hidden = true;
  if (OB_FAIL(iterate_part_info(is_only_history))) {
  } else if (OB_FAIL(iterate_all_part(is_only_history, is_include_hidden))) {
  } else if (OB_FAIL(iterate_all_sub_part(is_only_history))) {
  } else if (OB_FAIL(iterate_all_def_sub_part(is_only_history))) {
  }
  return ret;
}

int ObAddPartInfoHelper::add_part_info_dml_column(const ObPartitionSchema *table,
                                                  ObDMLSqlSplicer &dml)
{
   int ret = OB_SUCCESS;
   if (OB_ISNULL(table)) {
     ret = OB_ERR_UNEXPECTED;
     LOG_WARN("table is null", K(ret));
   } else {
     const ObPartitionOption &part_option = table->get_part_option();
     const ObSubPartitionOption &subpart_option = table->get_sub_part_option();
     if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                                table->get_table_id())))
       || OB_FAIL(dml.add_column("part_type", part_option.get_part_func_type()))
       || OB_FAIL(dml.add_column(OBJ_GET_K(part_option, part_num)))
       || OB_FAIL(dml.add_column("part_space", 0))
       || OB_FAIL(dml.add_column("part_expr", part_option.get_part_func_expr_str()))
       || OB_FAIL(dml.add_column("sub_part_type", subpart_option.get_part_func_type()))
       || OB_FAIL(dml.add_column("def_sub_part_num", subpart_option.get_part_num()))
       || OB_FAIL(dml.add_pk_column("schema_version", table->get_schema_version()))
       || OB_FAIL(dml.add_column("sub_part_expr", subpart_option.get_part_func_expr()))) {
       LOG_WARN("add column failed", K(ret));
     }
   }
   return ret;
}

int ObAddPartInfoHelper::add_part_dml_column(const ObPartitionSchema *table,
                                             const ObPartition &part,
                                             ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (part.get_part_idx() < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("part_idx is invalid", KR(ret), K(part));
  } else {
    int64_t sub_part_num = 0;
    if (PARTITION_LEVEL_TWO == table->get_part_level()) {
      sub_part_num = part.get_sub_part_num();
    }
    PartitionType partition_type = part.get_partition_type();
    if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                                 table->get_table_id())))
        || OB_FAIL(dml.add_pk_column("part_id", part.get_part_id()))
        || OB_FAIL(dml.add_pk_column("part_idx", part.get_part_idx()))
        || OB_FAIL(dml.add_column("schema_version", table->get_schema_version()))
        || OB_FAIL(dml.add_column("sub_part_num", sub_part_num))
        || OB_FAIL(dml.add_column("sub_part_space", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("new_sub_part_space", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("status", PARTITION_STATUS_INVALID))
        || OB_FAIL(dml.add_column("spare1", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("spare2", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("spare3", "" /*unused now*/))
        || OB_FAIL(dml.add_column("comment", "" /*unused now*/))
        || OB_FAIL(dml.add_column("tablespace_id", ObSchemaUtils::get_extract_schema_id(
                                                   part.get_tablespace_id())))
        || OB_FAIL(dml.add_column("partition_type", partition_type))
        || OB_FAIL(dml.add_column("tablet_id", part.get_tablet_id().id()))
        || OB_FAIL(dml.add_column("part_name", ObHexEscapeSqlStr(part.get_part_name())))) {
      LOG_WARN("dml add part info failed", K(ret));
    } else if (OB_FAIL(add_part_high_bound_val_column(table, part, dml))) {
    } else if (OB_FAIL(add_part_list_val_column(table, part, dml))) {
    }
  }
  return ret;
}

// For some paths, hash-like subpartition info will only store in sub_part_option from resolver,
// which means subpart from def_sub_part_array/sub_part_array may be NULL.
int ObAddPartInfoHelper::add_subpart_dml_column(const ObPartitionSchema *table,
                                                const int64_t part_id,
                                                const int64_t subpart_id,
                                                const ObSubPartition &subpart,
                                                ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (subpart.get_sub_part_idx() < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("subpart_idx is invalid", KR(ret), K(subpart));
  } else {
    PartitionType partition_type = subpart.get_partition_type();
    if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                                 table->get_table_id())))
        || OB_FAIL(dml.add_pk_column("part_id", part_id))
        || OB_FAIL(dml.add_pk_column("sub_part_id", subpart_id))
        || OB_FAIL(dml.add_column("schema_version", table->get_schema_version()))
        || OB_FAIL(dml.add_column("status", PARTITION_STATUS_INVALID))
        || OB_FAIL(dml.add_column("spare1", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("spare2", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("spare3", "" /*unused now*/))
        || OB_FAIL(dml.add_column("comment", "" /*unused now*/))
        || OB_FAIL(dml.add_column("sub_part_idx", subpart.get_sub_part_idx()))
        || OB_FAIL(dml.add_column("source_partition_id", -1))
        || OB_FAIL(dml.add_column("tablespace_id", ObSchemaUtils::get_extract_schema_id(
                                                   subpart.get_tablespace_id())))
        || OB_FAIL(dml.add_column("tablet_id", subpart.get_tablet_id().id()))
        || OB_FAIL(dml.add_column("sub_part_name", ObHexEscapeSqlStr(subpart.get_part_name())))) {
        LOG_WARN("dml add part info failed", K(ret));
    } else if (OB_FAIL(add_subpart_high_bound_val_column(table, subpart, dml))) {
    } else if (OB_FAIL(add_subpart_list_val_column(table, subpart, dml))) {
    }
  }
  return ret;
}

// For some paths, hash-like subpartition info will only store in sub_part_option from resolver,
// which means subpart from def_sub_part_array/sub_part_array may be NULL.
int ObAddPartInfoHelper::add_def_subpart_dml_column(const ObPartitionSchema *table,
                                                    const int64_t def_subpart_idx,
                                                    const ObSubPartition &subpart,
                                                    ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else {
    // For def subpartition, sub_part_idx and sub_part_id should be equal
    if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                                 table->get_table_id())))
        || OB_FAIL(dml.add_pk_column("sub_part_id", def_subpart_idx))
        || OB_FAIL(dml.add_column("schema_version", table->get_schema_version()))
        || OB_FAIL(dml.add_column("spare1", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("spare2", 0 /*unused now*/))
        || OB_FAIL(dml.add_column("spare3", "" /*unused now*/))
        || OB_FAIL(dml.add_column("comment", "" /*unused now*/))
        || OB_FAIL(dml.add_column("sub_part_idx", def_subpart_idx))
        || OB_FAIL(dml.add_column("tablespace_id", ObSchemaUtils::get_extract_schema_id(
                                                   subpart.get_tablespace_id())))
        || OB_FAIL(dml.add_column("sub_part_name", ObHexEscapeSqlStr(subpart.get_part_name())))) {
        LOG_WARN("dml add part info failed", K(ret));
    } else if (OB_FAIL(add_subpart_high_bound_val_column(table, subpart, dml))) {
    } else if (OB_FAIL(add_subpart_list_val_column(table, subpart, dml))) {
    }
  }

  return ret;
}

int ObAddPartInfoHelper::add_part_high_bound_val_column(const ObPartitionSchema *table,
                                                        const ObBasePartition &part,
                                                        ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_range_part()) {
    if (OB_FAIL(add_high_bound_val_column(table, part, dml))) {
    }
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "high_bound_val"))) {
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "b_high_bound_val"))) {
  }
  return ret;
}

int ObAddPartInfoHelper::add_part_list_val_column(const ObPartitionSchema *table,
                                                        const ObBasePartition &part,
                                                        ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_list_part()) {
    if (OB_FAIL(add_list_val_column(table, part, dml))) {
    }
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "list_val"))) {
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "b_list_val"))) {
  }
  return ret;
}

int ObAddPartInfoHelper::add_subpart_high_bound_val_column(const ObPartitionSchema *table,
                                                        const ObBasePartition &part,
                                                        ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_range_subpart()) {
    if (OB_FAIL(add_high_bound_val_column(table, part, dml))) {
    }
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "high_bound_val"))) {
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "b_high_bound_val"))) {
  }
  return ret;
}

int ObAddPartInfoHelper::add_subpart_list_val_column(const ObPartitionSchema *table,
                                                        const ObBasePartition &part,
                                                        ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (table->is_list_subpart()) {
    if (OB_FAIL(add_list_val_column(table, part, dml))) {
    }
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "list_val"))) {
  } else if (OB_FAIL(dml.add_column(true /* is_null */, "b_list_val"))) {
  }
  return ret;
}

template<typename P>
int ObAddPartInfoHelper::add_high_bound_val_column(
    const ObPartitionSchema *table,
    const P &part_option,
    ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (high_bound_val_ == NULL) {
    high_bound_val_ = static_cast<char *>(allocator_.alloc(OB_MAX_B_HIGH_BOUND_VAL_LENGTH));
    if (OB_ISNULL(high_bound_val_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("high_bound_val is null", K(ret), K(high_bound_val_));
    }
  }
  // determine if it is a list partition
  if (OB_SUCC(ret)) {
    MEMSET(high_bound_val_, 0, OB_MAX_B_HIGH_BOUND_VAL_LENGTH);
    int64_t pos = 0;
    //TODO:@yanhua add session timezone_info is better
    ObTimeZoneInfo tz_info;
    tz_info.set_offset(0);
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table ptr is null", KR(ret));
    } else if (OB_FAIL(OTTZ_MGR.get_timezone_map(tz_info.get_tz_map_wrap()))) {
    } else if (OB_FAIL(ObPartitionUtils::convert_rowkey_to_sql_literal(
               part_option.get_high_bound_val(), high_bound_val_,
               OB_MAX_B_HIGH_BOUND_VAL_LENGTH, pos, false, &tz_info))) {
    } else if (OB_FAIL(dml.add_column("high_bound_val",
                                      ObHexEscapeSqlStr(ObString(pos, high_bound_val_))))) {
    } else if (FALSE_IT(pos = 0)) {
    } else if (OB_FAIL(ObPartitionUtils::convert_rowkey_to_hex(
        part_option.get_high_bound_val(), high_bound_val_,
        OB_MAX_B_HIGH_BOUND_VAL_LENGTH, pos))) {
    } else if (OB_FAIL(dml.add_column("b_high_bound_val", ObString(pos, high_bound_val_)))) {
    } else {
      LOG_DEBUG("high bound info", "high_bound_val", ObString(pos, high_bound_val_).ptr(), K(pos));
    } //do nothing
  }
  return ret;
}

template<class P>
int ObAddPartInfoHelper::add_list_val_column(
    const ObPartitionSchema *table,
    const P &part_option,
    ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (list_val_ == NULL) {
    list_val_ = static_cast<char *>(allocator_.alloc(OB_MAX_B_PARTITION_EXPR_LENGTH));
    if (OB_ISNULL(list_val_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("list_val is null", K(ret), K(list_val_));
    }
  }
  // determine if it is a list partition, if it is a list partition
  if (OB_SUCC(ret)) {
    MEMSET(list_val_, 0, OB_MAX_B_PARTITION_EXPR_LENGTH);
    int64_t pos = 0;
    //TODO:@yanhua add session timezone_info is better
    ObTimeZoneInfo tz_info;
    tz_info.set_offset(0);
    if (OB_ISNULL(table)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table ptr is null", KR(ret));
    } else if (OB_FAIL(OTTZ_MGR.get_timezone_map(tz_info.get_tz_map_wrap()))) {
    } else if (OB_FAIL(ObPartitionUtils::convert_rows_to_sql_literal(
               part_option.get_list_row_values(), list_val_,
               OB_MAX_B_PARTITION_EXPR_LENGTH, pos, false, &tz_info))) {
    } else if (OB_FAIL(dml.add_column("list_val",
                                      ObHexEscapeSqlStr(ObString(pos, list_val_))))) {
    } else if (FALSE_IT(pos = 0)) {
    } else if (OB_FAIL(ObPartitionUtils::convert_rows_to_hex(
        part_option.get_list_row_values(), list_val_, OB_MAX_B_PARTITION_EXPR_LENGTH, pos))) {
    } else if (OB_FAIL(dml.add_column("b_list_val", ObString(pos, list_val_)))) {
    }
  }
  return ret;
}

int ObDropPartInfoHelper::delete_partition_info()
{
  int ret = OB_SUCCESS;
  const bool is_only_history = true;
  const bool is_include_hidden = true;
  if (OB_FAIL(iterate_part_info(is_only_history))) {
  } else if (OB_FAIL(iterate_all_part(is_only_history, is_include_hidden))) {
  } else if (OB_FAIL(iterate_all_sub_part(is_only_history))) {
  } else if (OB_FAIL(iterate_all_def_sub_part(is_only_history))) {
  }
  return ret;
}

int ObDropPartInfoHelper::add_part_info_dml_column(
    const ObPartitionSchema *table,
    ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                                      table->get_table_id())))
             || OB_FAIL(dml.add_pk_column("schema_version", table->get_schema_version()))) {
    LOG_WARN("dml add part info failed", K(ret));
  }
  return ret;
}

int ObDropPartInfoHelper::add_part_dml_column(const ObPartitionSchema *table,
                                              const ObPartition &part,
                                              share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                                      table->get_table_id())))
             || OB_FAIL(dml.add_pk_column("part_id", part.get_part_id()))
             || OB_FAIL(dml.add_pk_column("schema_version", table->get_schema_version()))) {
    LOG_WARN("dml add part info failed", K(ret));
  }
  return ret;
}

int ObDropPartInfoHelper::add_subpart_dml_column(const ObPartitionSchema *table,
                                                 const int64_t part_id,
                                                 const int64_t subpart_id,
                                                 const ObSubPartition &subpart,
                                                 share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  UNUSED(subpart);
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                                      table->get_table_id())))
             || OB_FAIL(dml.add_pk_column("part_id", part_id))
             || OB_FAIL(dml.add_pk_column("sub_part_id", subpart_id))
             || OB_FAIL(dml.add_pk_column("schema_version", table->get_schema_version()))) {
    LOG_WARN("dml add part info failed", K(ret));
  }
  return ret;
}

int ObDropPartInfoHelper::add_def_subpart_dml_column(const ObPartitionSchema *table,
                                                     const int64_t subpart_idx,
                                                     const ObSubPartition &subpart,
                                                     share::ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  UNUSED(subpart);
  if (OB_ISNULL(table)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                               table->get_table_id())))
      || OB_FAIL(dml.add_pk_column("sub_part_id", subpart_idx))
      || OB_FAIL(dml.add_pk_column("schema_version", table->get_schema_version()))) {
    LOG_WARN("dml add part info failed", K(ret));
  }
  return ret;
}

int ObAddIncSubPartDMLGenerator::convert_to_dml(const PartInfo &part_info, ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  
  
  PartitionType partition_type = part_info.partition_type_;
  int64_t subpart_idx = part_info.sub_part_idx_;
  if (subpart_idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("subpart_idx is invalid", KR(ret), K(part_info));
  } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                               part_info.table_id_)))
      || OB_FAIL(dml.add_pk_column("part_id", part_info.part_id_))
      || OB_FAIL(dml.add_pk_column("sub_part_id", part_info.sub_part_id_))
      || OB_FAIL(dml.add_pk_column("sub_part_idx", subpart_idx))
      || OB_FAIL(dml.add_column("sub_part_name", ObHexEscapeSqlStr(part_info.part_name_)))
      || OB_FAIL(dml.add_column("schema_version", part_info.schema_version_))
      || OB_FAIL(dml.add_column("status", part_info.status_))
      || OB_FAIL(dml.add_column("spare1", 0 /*unused now*/))
      || OB_FAIL(dml.add_column("spare2", 0 /*unused now*/))
      || OB_FAIL(dml.add_column("spare3", "" /*unused now*/))
      || OB_FAIL(dml.add_column("comment", "" /*unused now*/))
      || OB_FAIL(dml.add_column("high_bound_val",
                                ObHexEscapeSqlStr(part_info.high_bound_val_)))
      || OB_FAIL(dml.add_column("b_high_bound_val",
                                part_info.b_high_bound_val_))
      || OB_FAIL(dml.add_column("list_val", ObHexEscapeSqlStr(part_info.list_val_)))
      || OB_FAIL(dml.add_column("b_list_val", part_info.b_list_val_))
      || OB_FAIL(dml.add_column("partition_type", partition_type))
      || OB_FAIL(dml.add_column("tablet_id", part_info.tablet_id_.id()))) {
    LOG_WARN("dml add part info failed", K(ret));
  }
  if (OB_FAIL(ret)) {
    //nothing todo
  }
  return ret;
}

int ObAddIncSubPartDMLGenerator::extract_part_info(PartInfo &part_info)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ori_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (part_idx_ < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid part_idx", K(part_idx_), K(ret));
  } else {
    
    part_info.table_id_ = ori_table_->get_table_id();
    part_info.part_id_ = part_.get_part_id();
    part_info.sub_part_id_ = sub_part_.get_sub_part_id();
    part_info.part_name_ = sub_part_.get_part_name();
    part_info.schema_version_ = schema_version_;
    part_info.status_ = PARTITION_STATUS_INVALID;
    part_info.sub_part_idx_ = sub_part_.get_sub_part_idx();
    part_info.partition_type_ = sub_part_.get_partition_type();
    part_info.tablet_id_ = sub_part_.get_tablet_id();
    if (OB_FAIL(ret)) {
    } else if (ori_table_->is_range_subpart()) {
      if (OB_FAIL(gen_high_bound_val_str(sub_part_.get_high_bound_val(),
                                         part_info.high_bound_val_,
                                         part_info.b_high_bound_val_))) {
      }
    } else if (ori_table_->is_list_subpart()) {
      if (OB_FAIL(gen_list_val_str(
                  sub_part_.get_list_row_values(),
                  part_info.list_val_,
                  part_info.b_list_val_))) {
      }
    } else if (is_hash_like_part(ori_table_->get_sub_part_option().get_part_func_type())) {
      part_info.sub_part_idx_ = subpart_idx_;
    }
  }

  return ret;
}

int ObAddIncPartDMLGenerator::convert_to_dml(const PartInfo &part_info, ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  
  
  PartitionType partition_type = part_info.partition_type_;
  int64_t part_idx = part_info.part_idx_;
  if (part_idx < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("part_idx is invalid", KR(ret), K(part_info));
  } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                               part_info.table_id_)))
      || OB_FAIL(dml.add_pk_column("part_id", part_info.part_id_))
      || OB_FAIL(dml.add_pk_column("part_idx", part_idx))
      || OB_FAIL(dml.add_pk_column("part_name", ObHexEscapeSqlStr(part_info.part_name_)))
      || OB_FAIL(dml.add_column("schema_version", part_info.schema_version_))
      || OB_FAIL(dml.add_column("sub_part_num", part_info.sub_part_num_))
      || OB_FAIL(dml.add_column("sub_part_space", 0 /*unused now*/))
      || OB_FAIL(dml.add_column("new_sub_part_space", 0 /*unused now*/))
      || OB_FAIL(dml.add_column("status", part_info.status_))
      || OB_FAIL(dml.add_column("spare1", 0 /*unused now*/))
      || OB_FAIL(dml.add_column("spare2", 0 /*unused now*/))
      || OB_FAIL(dml.add_column("spare3", ""/*unused now*/))
      || OB_FAIL(dml.add_column("comment", "" /*unused now*/))
      || OB_FAIL(dml.add_column("high_bound_val",
                                ObHexEscapeSqlStr(part_info.high_bound_val_)))
      || OB_FAIL(dml.add_column("b_high_bound_val",
                                part_info.b_high_bound_val_))
      || OB_FAIL(dml.add_column("list_val", ObHexEscapeSqlStr(part_info.list_val_)))
      || OB_FAIL(dml.add_column("b_list_val", part_info.b_list_val_))
      || OB_FAIL(dml.add_column("partition_type", partition_type))
      || OB_FAIL(dml.add_column("tablet_id", part_info.tablet_id_.id()))) {
    LOG_WARN("dml add part info failed", K(ret));
  }
  if (OB_FAIL(ret)) {
    //nothing todo
  }
  return ret;
}

int ObAddIncPartDMLGenerator::extract_part_info(PartInfo &part_info)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ori_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else {
    int64_t sub_part_num = 0;
    if (PARTITION_LEVEL_TWO == ori_table_->get_part_level()) {
      sub_part_num = part_.get_sub_part_num();
    }
    
    part_info.table_id_ = ori_table_->get_table_id();
    part_info.part_id_ = part_.get_part_id();
    part_info.schema_version_ = schema_version_;
    part_info.sub_part_num_ = sub_part_num;
    part_info.status_ = PARTITION_STATUS_INVALID;
    part_info.part_idx_ = part_.get_part_idx();
    part_info.partition_type_ = part_.get_partition_type();
    part_info.tablet_id_ = part_.get_tablet_id();
    part_info.part_name_ = part_.get_part_name();

    if (OB_FAIL(ret)) {
    } else if (ori_table_->is_range_part()) {
      if (OB_FAIL(gen_high_bound_val_str(part_.get_high_bound_val(),
                                         part_info.high_bound_val_,
                                         part_info.b_high_bound_val_))) {
      }
    } else if (ori_table_->is_list_part()) {
      if (OB_FAIL(gen_list_val_str(part_.get_list_row_values(),
                                   part_info.list_val_,
                                   part_info.b_list_val_))) {
      }
    } else if (is_hash_like_part(ori_table_->get_part_option().get_part_func_type())) {
      part_info.part_idx_ = part_.get_part_idx();
    }
  }

  return ret;
}

int ObDropIncSubPartDMLGenerator::convert_to_dml(const PartInfo &part_info, ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  
  
  const int64_t deleted = true;
  if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                               part_info.table_id_)))
      || OB_FAIL(dml.add_pk_column("part_id", part_info.part_id_))
      || OB_FAIL(dml.add_pk_column("sub_part_id", part_info.sub_part_id_))
      || OB_FAIL(dml.add_column("is_deleted", deleted))
      || OB_FAIL(dml.add_column("schema_version", part_info.schema_version_))) {
    LOG_WARN("dml drop part info failed", K(ret));
  }
  return ret;
}

int ObDropIncSubPartDMLGenerator::extract_part_info(PartInfo &part_info)
{
  int ret = OB_SUCCESS;

  
  part_info.table_id_ = sub_part_.get_table_id();
  part_info.part_id_ = sub_part_.get_part_id();
  part_info.sub_part_id_ = sub_part_.get_sub_part_id();
  part_info.schema_version_ = schema_version_;

  return ret;
}

int ObDropIncPartDMLGenerator::convert_to_dml(const PartInfo &part_info, ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  
  
  if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(
                                               part_info.table_id_)))
      || OB_FAIL(dml.add_pk_column("part_id", part_info.part_id_))
      || OB_FAIL(dml.add_column("schema_version", part_info.schema_version_))) {
    LOG_WARN("dml drop part info failed", K(ret));
  }
  return ret;
}

int ObDropIncPartDMLGenerator::extract_part_info(PartInfo &part_info)
{
  int ret = OB_SUCCESS;

  
  part_info.table_id_ = part_.get_table_id();
  part_info.part_id_ = part_.get_part_id();
  part_info.schema_version_ = schema_version_;

  return ret;
}

int ObUpdatePartHelper::update_partition_info()
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ori_table_) || OB_ISNULL(upd_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (!ori_table_->is_user_partition_table()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("attempt to update partition informations of non-partitioned table", KR(ret), KPC(ori_table_));
  } else {
    
    
    ObDMLSqlSplicer dml;
    ObDMLSqlSplicer history_dml;
    const int64_t deleted = false;
    const int64_t part_num = upd_table_->get_partition_num();
    const int64_t hidden_part_num = upd_table_->get_hidden_partition_num();
    const int64_t all_part_num = part_num + hidden_part_num;
    ObPartition **part_array = upd_table_->get_part_array();
    ObPartition **hidden_part_array = upd_table_->get_hidden_part_array();

    if (OB_ISNULL(part_array) && OB_ISNULL(hidden_part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part array is null", K(ret), K(upd_table_));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < all_part_num; ++i) {
      ObPartition *part = nullptr;
      if (i < part_num) {
        part = part_array[i];
      } else {
        part = hidden_part_array[i - part_num];
      }
      if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("part array is null", K(ret), K(i), K(part_num), K(hidden_part_num), KPC(upd_table_));
      } else {
        HEAP_VAR(ObAddIncPartDMLGenerator, update_dml_gen,
                 ori_table_, *part, all_part_num, i, schema_version_) {
          if (OB_FAIL(update_dml_gen.gen_dml(dml))) {
          } else if (OB_FAIL(dml.finish_row())) {
          } else if (OB_FAIL(update_dml_gen.gen_dml(history_dml))) {
          } else if (OB_FAIL(history_dml.add_column("is_deleted", deleted))) {
          } else if (OB_FAIL(history_dml.finish_row())) {
          }
        }

        if (OB_FAIL(ret)) {
        } else if (PARTITION_LEVEL_TWO != ori_table_->get_part_level()) {
          // skip
        } else if (OB_ISNULL(part->get_subpart_array())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("subpart array is null", K(ret));
        } else {
          // TODO: need to implement code to update subpartition
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("not support to update subpartition info", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ObSqlString part_history_sql;
      if (OB_FAIL(history_dml.splice_batch_insert_sql(share::OB_ALL_PART_HISTORY_TNAME,
                                                      part_history_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_history_sql.ptr(), affected_rows))) {
      } else if (affected_rows != all_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("history affected_rows is unexpected", K(ret), K(affected_rows), K(part_num), K(hidden_part_num), K(part_history_sql));
      }
    }

    if (OB_SUCC(ret)) {
      ObSqlString part_sql;
      int64_t affected_rows = 0;
      if (OB_FAIL(dml.splice_batch_insert_update_sql(share::OB_ALL_PART_TNAME, part_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_sql.ptr(), affected_rows))) {
      } else if (affected_rows != 2 * all_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows is unexpected", K(ret), K(affected_rows), K(part_num), K(hidden_part_num), K(part_sql));
      }
    }
  }
  return ret;
}

// Add the incremental partition definitions recorded by inc_table_.
int ObAddIncPartHelper::add_partition_info()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ori_table_) || OB_ISNULL(inc_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (ori_table_->is_user_partition_table()) {
    
    
    ObDMLSqlSplicer dml;
    ObDMLSqlSplicer history_dml;
    ObDMLSqlSplicer sub_dml;
    ObDMLSqlSplicer history_sub_dml;
    const int64_t inc_part_num = inc_table_->get_partition_num();
    ObPartition **part_array = inc_table_->get_part_array();
    int64_t inc_sub_part_num = 0;
    const int64_t deleted = false;
    if (OB_ISNULL(part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part array is null", K(ret), K(inc_table_));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < inc_part_num; ++i) {
      ObPartition *part = part_array[i];
      if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("part array is null", K(ret), K(i), K(inc_table_));
      } else {
        HEAP_VAR(ObAddIncPartDMLGenerator, part_dml_gen,
                 ori_table_, *part, inc_part_num, i, schema_version_) {
          if (OB_FAIL(part_dml_gen.gen_dml(dml))) {
          } else if (OB_FAIL(dml.finish_row())) {
          } else if (OB_FAIL(part_dml_gen.gen_dml(history_dml))) {
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(history_dml.add_column("is_deleted", deleted))) {
        } else if (OB_FAIL(history_dml.finish_row())) {
        }

        if (OB_FAIL(ret)) {
        } else if (PARTITION_LEVEL_TWO != ori_table_->get_part_level()) {
          // skip
        } else if (OB_ISNULL(part->get_subpart_array())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("subpart array is null", K(ret));
        } else {
          for (int64_t j = 0; OB_SUCC(ret) && j < part->get_subpartition_num(); j++) {
            inc_sub_part_num++;
            HEAP_VAR(ObAddIncSubPartDMLGenerator, sub_part_dml_gen,
                     ori_table_, *part, *part->get_subpart_array()[j], inc_part_num, i, j, schema_version_) {
              if (OB_FAIL(sub_part_dml_gen.gen_dml(sub_dml))) {
              } else if (OB_FAIL(sub_dml.finish_row())) {
              } else if (OB_FAIL(sub_part_dml_gen.gen_dml(history_sub_dml))) {
              }
            }

            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(history_sub_dml.add_column("is_deleted", deleted))) {
            } else if (OB_FAIL(history_sub_dml.finish_row())) {
            }
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ObSqlString part_history_sql;
      if (OB_FAIL(history_dml.splice_batch_insert_sql(share::OB_ALL_PART_HISTORY_TNAME,
                                                      part_history_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_history_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("history affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }

    if (OB_SUCC(ret)) {
      ObSqlString part_sql;
      int64_t affected_rows = 0;
      if (OB_FAIL(dml.splice_batch_insert_sql(share::OB_ALL_PART_TNAME, part_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }

    if (OB_SUCC(ret) && inc_sub_part_num > 0) {
      int64_t affected_rows = 0;
      ObSqlString part_history_sql;
      if (OB_FAIL(history_sub_dml.splice_batch_insert_sql(share::OB_ALL_SUB_PART_HISTORY_TNAME,
                                                      part_history_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_history_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_sub_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("history affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }

    if (OB_SUCC(ret) && inc_sub_part_num> 0) {
      ObSqlString part_sql;
      int64_t affected_rows = 0;
      if (OB_FAIL(sub_dml.splice_batch_insert_sql(share::OB_ALL_SUB_PART_TNAME, part_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_sub_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }
  }
  return ret;
}

int ObAddIncSubPartHelper::add_subpartition_info(const bool is_subpart_idx_specified)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ori_table_) || OB_ISNULL(inc_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else {
    
    
    ObDMLSqlSplicer dml;
    ObDMLSqlSplicer history_dml;
    ObDMLSqlSplicer sub_dml;
    ObDMLSqlSplicer history_sub_dml;
    const int64_t inc_part_num = inc_table_->get_partition_num();
    ObPartition **part_array = inc_table_->get_part_array();
    int64_t inc_sub_part_num = 0;
    const int64_t deleted = false;
    if (OB_ISNULL(part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("part array is null", K(ret), K(inc_table_));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < inc_part_num; ++i) {
      ObPartition *part = part_array[i];
      if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("part array is null", K(ret), K(i), K(inc_table_));
      } else {
        if (OB_ISNULL(part->get_subpart_array())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("subpart array is null", K(ret));
        } else {
          for (int64_t j = 0; OB_SUCC(ret) && j < part->get_subpartition_num(); j++) {
            inc_sub_part_num++;
            const ObSubPartition *sub_part = part->get_subpart_array()[j];
            int64_t subpart_idx = j;
            if (OB_ISNULL(sub_part)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected null sub part", KR(ret), KP(part->get_subpart_array()[j]), K(j));
            } else if (is_subpart_idx_specified) {
              subpart_idx = sub_part->get_sub_part_idx();
            }

            HEAP_VAR(ObAddIncSubPartDMLGenerator, sub_part_dml_gen,
                     ori_table_, *part, *sub_part, inc_part_num, i, subpart_idx, schema_version_) {
              if (OB_FAIL(sub_part_dml_gen.gen_dml(sub_dml))) {
              } else if (OB_FAIL(sub_dml.finish_row())) {
              } else if (OB_FAIL(sub_part_dml_gen.gen_dml(history_sub_dml))) {
              }
            }

            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(history_sub_dml.add_column("is_deleted", deleted))) {
            } else if (OB_FAIL(history_sub_dml.finish_row())) {
            }
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      int64_t affected_rows = 0;
      ObSqlString part_history_sql;
      if (OB_FAIL(history_sub_dml.splice_batch_insert_sql(share::OB_ALL_SUB_PART_HISTORY_TNAME,
                                                      part_history_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_history_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_sub_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("history affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }

    if (OB_SUCC(ret)) {
      ObSqlString part_sql;
      int64_t affected_rows = 0;
      if (OB_FAIL(sub_dml.splice_batch_insert_sql(share::OB_ALL_SUB_PART_TNAME, part_sql))) {
      } else if (OB_FAIL(sql_client_.write(part_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_sub_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }
  }
  return ret;
}

int ObDropIncPartHelper::drop_partition_info()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ori_table_) || OB_ISNULL(inc_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (ori_table_->is_user_partition_table()) {
    
    
    ObDMLSqlSplicer dml;
    ObDMLSqlSplicer sub_dml;
    ObSqlString part_history_sql;
    ObSqlString sub_part_history_sql;
    ObSqlString value_str;
    const int64_t inc_part_num = inc_table_->get_partition_num();
    ObPartition **part_array = inc_table_->get_part_array();
    int64_t inc_sub_part_num = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < inc_part_num; i++) {
      dml.reset();
      ObPartition *part = part_array[i];
      // delete __all_part_history
      HEAP_VAR(ObDropIncPartDMLGenerator, part_dml_gen, *part, schema_version_) {
        if (OB_ISNULL(part)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("part is null", KR(ret), KP(part));
        } else if (OB_FAIL(part_dml_gen.gen_dml(dml))) {
        } else {
          const int64_t deleted = true;
          if (OB_FAIL(dml.add_column("is_deleted", deleted))) {
          } else if (0 == i) {
            if (OB_FAIL(dml.splice_insert_sql(share::OB_ALL_PART_HISTORY_TNAME, part_history_sql))) {
            }
          } else {
            value_str.reset();
            if (OB_FAIL(dml.splice_values(value_str))) {
            } else if (OB_FAIL(part_history_sql.append_fmt(", (%s)", value_str.ptr()))) {
            }
          }
        }
      } // end HEAP_VAR

      // delete __all_sub_part_history
      for (int64_t j = 0; OB_SUCC(ret) && j < part->get_subpartition_num(); j++) {
        sub_dml.reset();
        inc_sub_part_num++;
        HEAP_VAR(ObDropIncSubPartDMLGenerator, sub_part_dml_gen,
                 *part->get_subpart_array()[j], schema_version_) {
          if (OB_FAIL(sub_part_dml_gen.gen_dml(sub_dml))) {
          } else if (0 == i && 0 == j) {
            if (OB_FAIL(sub_dml.splice_insert_sql(share::OB_ALL_SUB_PART_HISTORY_TNAME, sub_part_history_sql))) {
            }
          } else {
            value_str.reset();
            if (OB_FAIL(sub_dml.splice_values(value_str))) {
            } else if (OB_FAIL(sub_part_history_sql.append_fmt(", (%s)", value_str.ptr()))) {
            }
          }
        } // end HEAP_VAR
      }
    } // end for
    if (OB_SUCC(ret) && inc_part_num > 0) {
      int64_t affected_rows = 0;
      if (OB_FAIL(sql_client_.write(part_history_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("history affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }
    if (OB_SUCC(ret) && inc_sub_part_num > 0) {
      int64_t affected_rows = 0;
      if (OB_FAIL(sql_client_.write(sub_part_history_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_sub_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("history affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }
  }
  return ret;
}

int ObDropIncSubPartHelper::drop_subpartition_info()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ori_table_) || OB_ISNULL(inc_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", K(ret));
  } else if (ori_table_->is_user_partition_table()) {
    
    
    ObDMLSqlSplicer dml;
    ObDMLSqlSplicer sub_dml;
    ObSqlString part_history_sql;
    ObSqlString sub_part_history_sql;
    ObSqlString value_str;
    const int64_t inc_part_num = inc_table_->get_partition_num();
    ObPartition **part_array = inc_table_->get_part_array();
    int64_t inc_sub_part_num = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < inc_part_num; i++) {
      ObPartition *part = part_array[i];
      if (OB_ISNULL(part)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("part_array[i] is null", K(ret), K(i));
      } else {
        int64_t subpart_num = part->get_subpartition_num();
        ObSubPartition **subpart_array = part->get_subpart_array();
        for (int64_t j = 0; OB_SUCC(ret) && j < subpart_num; j++) {
          sub_dml.reset();
          inc_sub_part_num++;
          HEAP_VAR(ObDropIncSubPartDMLGenerator, sub_part_dml_gen,
                   *subpart_array[j], schema_version_) {
            if (OB_FAIL(sub_part_dml_gen.gen_dml(sub_dml))) {
            } else if (0 == i && 0 == j) {
              if (OB_FAIL(sub_dml.splice_insert_sql(share::OB_ALL_SUB_PART_HISTORY_TNAME, sub_part_history_sql))) {
              }
            } else {
              value_str.reset();
              if (OB_FAIL(sub_dml.splice_values(value_str))) {
              } else if (OB_FAIL(sub_part_history_sql.append_fmt(", (%s)", value_str.ptr()))) {
              }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret) && inc_sub_part_num > 0) {
      int64_t affected_rows = 0;
      if (OB_FAIL(sql_client_.write(sub_part_history_sql.ptr(), affected_rows))) {
      } else if (affected_rows != inc_sub_part_num) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("history affected_rows is unexpected", K(ret), K(inc_part_num), K(affected_rows));
      }
    }
  }
  return ret;
}

int ObRenameIncPartHelper::rename_partition_info(const bool update_part_idx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ori_table_) || OB_ISNULL(inc_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", KR(ret), KP(ori_table_), KP(inc_table_));
  } else if (!ori_table_->is_user_partition_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupport behavior on not user partition table", KR(ret), KPC(ori_table_));
  } else {
    
    const uint64_t table_id = ori_table_->get_table_id();
    
    ObDMLSqlSplicer dml;
    ObSqlString part_sql;
    ObPartition **part_array = inc_table_->get_part_array();
    ObPartition *inc_part = nullptr;
    const int64_t inc_part_num = inc_table_->get_partition_num();
    int64_t affected_rows = 0;
    if (OB_ISNULL(part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inc table part_array is null", KR(ret), KP(inc_table_));
    } else if (OB_UNLIKELY(1 != inc_part_num)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inc part num should be 1", KR(ret), K(inc_part_num));
    } else if (OB_ISNULL(inc_part = part_array[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inc_part is null", KR(ret));
    } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(table_id)))
          || OB_FAIL(dml.add_pk_column("part_id", inc_part->get_part_id()))
          || OB_FAIL(dml.add_column("schema_version", schema_version_))
          || OB_FAIL(dml.add_column("part_name", inc_part->get_part_name().ptr()))) {
      LOG_WARN("dml add column failed", KR(ret));
    } else if (update_part_idx && OB_FAIL(dml.add_column("part_idx", inc_part->get_part_idx()))) {
      LOG_WARN("dml add column failed", KR(ret));
    } else if (OB_FAIL(dml.splice_update_sql(share::OB_ALL_PART_TNAME, part_sql))) {
    } else if (OB_FAIL(sql_client_.write(part_sql.ptr(), affected_rows))) {
    } else if (OB_UNLIKELY(inc_part_num != affected_rows)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected affected rows", KR(ret), K(inc_part_num), K(affected_rows));
    } else {
      ObDMLSqlSplicer history_dml;
      ObSqlString part_history_sql;
      affected_rows = 0;
      HEAP_VAR(ObAddIncPartDMLGenerator, part_dml_gen,
                ori_table_, *inc_part, inc_part_num, inc_part->get_part_idx(), schema_version_) {
        if (OB_FAIL(part_dml_gen.gen_dml(history_dml))) {
        } else if (OB_FAIL(history_dml.add_column("is_deleted", false))) {
        } else if (OB_FAIL(history_dml.splice_insert_sql(share::OB_ALL_PART_HISTORY_TNAME,
                                                        part_history_sql))) {
        } else if (OB_FAIL(sql_client_.write(part_history_sql.ptr(), affected_rows))) {
        } else if (OB_UNLIKELY(inc_part_num != affected_rows)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("history affected_rows is unexpected", KR(ret), K(inc_part_num), K(affected_rows));
        }
      }
    }
  }
  return ret;
}

int ObRenameIncSubpartHelper::rename_subpartition_info()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ori_table_) || OB_ISNULL(inc_table_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table is null", KR(ret), KP(ori_table_), KP(inc_table_));
  } else if (!ori_table_->is_user_subpartition_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("unsupport behavior on not user subpartition table", KR(ret), KPC(ori_table_));
  } else {
    
    const uint64_t table_id = ori_table_->get_table_id();
    
    ObDMLSqlSplicer dml;
    ObSqlString subpart_sql;
    ObPartition **part_array = inc_table_->get_part_array();
    ObPartition *inc_part = nullptr;
    const int64_t inc_part_num = inc_table_->get_partition_num();
    if (OB_ISNULL(part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("partition array is null", KR(ret), KP(inc_table_));
    } else if (OB_UNLIKELY(1 != inc_part_num)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inc part num should be 1", KR(ret), K(inc_part_num));
    } else if (OB_ISNULL(inc_part = part_array[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("inc part is null", KR(ret));
    } else {
      ObSubPartition **subpart_array = inc_part->get_subpart_array();
      ObSubPartition *inc_subpart = nullptr;
      const int64_t inc_subpart_num = inc_part->get_subpartition_num();
      int64_t affected_rows = 0;
      if (OB_ISNULL(subpart_array)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("subpart_array is null", KR(ret));
      } else if (OB_UNLIKELY(1 != inc_subpart_num)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inc subpart num should be 1", KR(ret), K(inc_subpart_num));
      } else if (OB_ISNULL(inc_subpart = subpart_array[0])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("inc_subpart is null", KR(ret));
      } else if (OB_FAIL(dml.add_pk_column("table_id", ObSchemaUtils::get_extract_schema_id(table_id)))
              ||OB_FAIL(dml.add_pk_column("part_id", inc_part->get_part_id()))
              ||OB_FAIL(dml.add_pk_column("sub_part_id", inc_subpart->get_sub_part_id()))
              ||OB_FAIL(dml.add_column("schema_version", schema_version_))
              ||OB_FAIL(dml.add_column("sub_part_name", inc_subpart->get_part_name().ptr()))) {
        LOG_WARN("dml add column failed", KR(ret));
      } else if (OB_FAIL(dml.splice_update_sql(share::OB_ALL_SUB_PART_TNAME, subpart_sql))) {
      } else if (OB_FAIL(sql_client_.write(subpart_sql.ptr(), affected_rows))) {
      } else if (OB_UNLIKELY(inc_subpart_num != affected_rows)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected affected rows", KR(ret), K(inc_subpart_num), K(affected_rows));
      } else {
        ObDMLSqlSplicer history_sub_dml;
        ObSqlString subpart_history_sql;
        affected_rows = 0;
        HEAP_VAR(ObAddIncSubPartDMLGenerator, sub_part_dml_gen,
                ori_table_, *inc_part, *inc_subpart, inc_part_num, inc_part->get_part_idx(),
                inc_subpart->get_sub_part_idx(), schema_version_) {
          if (OB_FAIL(sub_part_dml_gen.gen_dml(history_sub_dml))) {
          } else if (OB_FAIL(history_sub_dml.add_column("is_deleted", false))) {
          } else if (OB_FAIL(history_sub_dml.splice_insert_sql(share::OB_ALL_SUB_PART_HISTORY_TNAME,
                                                              subpart_history_sql))) {
          } else if (OB_FAIL(sql_client_.write(subpart_history_sql.ptr(), affected_rows))) {
          } else if (OB_UNLIKELY(inc_subpart_num != affected_rows)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("history affected_rows is unexpected", KR(ret), K(inc_part_num), K(affected_rows));
          }
        }
      }
    }
  }
  return ret;
}
} //end of schema
} //end of share
} //end of oceanbase
