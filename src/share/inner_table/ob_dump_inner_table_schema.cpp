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

#include "ob_dump_inner_table_schema.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/schema/ob_schema_utils.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/schema/ob_table_sql_service.h"

#define USING_LOG_PREFIX RS

namespace oceanbase
{
namespace share
{
using namespace schema;

///////////////////////////////////////////////////////////////////////////////////////////
// the following code should be the same with python code in generate_inner_table_schema.py
int ObDumpInnerTableSchemaUtils::upper(ObSqlString &str)
{
  int ret = OB_SUCCESS;
  ObSqlString tmp;
  ObString raw = str.string();
  for (int64_t i = 0; OB_SUCC(ret) && i < raw.length(); i++) {
    if (raw[i] >= 'a' && raw[i] <= 'z') {
      OZ (tmp.append_fmt("%c", raw[i] - 'a' + 'A'));
    } else {
      OZ (tmp.append_fmt("%c", raw[i]));
    }
  }
  OZ (str.assign(tmp));
  return ret;
}

int ObDumpInnerTableSchemaUtils::replace(ObSqlString &str, const char a, const char b) 
{
  int ret = OB_SUCCESS;
  ObSqlString tmp;
  ObString raw = str.string();
  for (int64_t i = 0; OB_SUCC(ret) && i < raw.length(); i++) {
    if (raw[i] == a) {
      OZ (tmp.append_fmt("%c", b));
    } else {
      OZ (tmp.append_fmt("%c", raw[i]));
    }
  }
  OZ (str.assign(tmp));
  return ret;
}

int ObDumpInnerTableSchemaUtils::lstrip(ObSqlString &str, const char c)
{
  int ret = OB_SUCCESS;
  int remove = true;
  ObSqlString tmp;
  ObString raw = str.string();
  for (int64_t i = 0; OB_SUCC(ret) && i < raw.length(); i++) {
    if (raw[i] == c && remove) {
    } else {
      remove = false;
      OZ (tmp.append_fmt("%c", raw[i]));
    }
  }
  OZ (str.assign(tmp));
  return ret;
}

int ObDumpInnerTableSchemaUtils::rstrip(ObSqlString &str, const char c)
{
  int ret = OB_SUCCESS;
  int remove = true;
  ObSqlString tmp;
  ObString raw = str.string();
  for (int64_t i = 0; OB_SUCC(ret) && i < raw.length() && remove; i++) {
    if (raw[raw.length() - 1 - i] == c) {
    } else {
      OZ (tmp.assign(str.ptr(), raw.length() - i));
      remove = false;
    }
  }
  OZ (str.assign(tmp));
  return ret;
}

int ObDumpInnerTableSchemaUtils::strip(ObSqlString &str, const char c)
{
  int ret = OB_SUCCESS;
  OZ (lstrip(str, c));
  OZ (rstrip(str, c));
  return ret;
}

int ObDumpInnerTableSchemaUtils::table_name2tid(const ObString &table_name, ObSqlString &tid)
{
  int ret = OB_SUCCESS;
  ObSqlString tmp;
  OZ (tmp.assign(table_name));
  OZ (replace(tmp, '$', '_'));
  OZ (upper(tmp));
  OZ (strip(tmp, '_'));
  OZ (tid.append("OB_"));
  OZ (tid.append(tmp.string()));
  OZ (tid.append("_TID"));
  return ret;
}

int ObDumpInnerTableSchemaUtils::table_name2tname(const ObString &table_name, ObSqlString &tname)
{
  int ret = OB_SUCCESS;
  ObSqlString tmp;
  OZ (tmp.assign(table_name));
  OZ (replace(tmp, '$', '_'));
  OZ (upper(tmp));
  OZ (strip(tmp, '_'));
  OZ (tname.append("OB_"));
  OZ (tname.append(tmp.string()));
  OZ (tname.append("_TNAME"));
  return ret;
}

int ObDumpInnerTableSchemaUtils::table_name2schema_version(const ObTableSchema &table, ObSqlString &schema_version)
{
  int ret = OB_SUCCESS;
  ObSqlString tmp;
  OZ (tmp.assign(table.get_table_name()));
  OZ (replace(tmp, '$', '_'));
  OZ (upper(tmp));
  OZ (strip(tmp, '_'));
  OZ (schema_version.append("OB_"));
  OZ (schema_version.append(tmp.string()));
  if ((is_ora_virtual_table(table.get_table_id()) || is_ora_sys_view_table(table.get_table_id()))) {
    OZ (schema_version.append("_ORACLE"));
  }
  OZ (schema_version.append("_SCHEMA_VERSION"));
  return ret;
}
///////////////////////////////////////////////////////////////////////////////////////////

int ObInnerTableSchemaDumper::get_schema_pointers_(
    ObIArray<schema::ObTableSchema *> &schema_ptrs)
{
  int ret = OB_SUCCESS;
  schema_ptrs.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < schemas_.count(); i++) {
    if (OB_FAIL(schema_ptrs.push_back(&schemas_.at(i)))) {
    }
  }
  return ret;
}

int ObInnerTableSchemaDumper::get_inner_table_schema_info(ObIArray<ObLoadInnerTableSchemaInfo> &infos)
{
  int ret = OB_SUCCESS;
  ObArray<schema::ObTableSchema *> schema_ptrs;
  ObLoadInnerTableSchemaInfo info, info_history;
  infos.reset();
  int (ObInnerTableSchemaDumper::*func[])(const ObIArray<schema::ObTableSchema *> &, ObLoadInnerTableSchemaInfo &) = {
    &ObInnerTableSchemaDumper::get_all_core_table_info_,
    &ObInnerTableSchemaDumper::get_all_ddl_operation_info_,
  };
  int (ObInnerTableSchemaDumper::*func_table_column[])(const ObIArray<schema::ObTableSchema *> &, ObLoadInnerTableSchemaInfo &,
       ObLoadInnerTableSchemaInfo &) = {
    &ObInnerTableSchemaDumper::get_all_table_info_,
    &ObInnerTableSchemaDumper::get_all_column_info_,
  };
  if (OB_FAIL(get_schema_pointers_(schema_ptrs))) {
  } else {
    for (int64_t i = 0; i < ARRAYSIZEOF(func) && OB_SUCC(ret); i++) {
      if (OB_ISNULL(func[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("pointer is null", KR(ret));
      } else if (OB_FAIL((this->*(func[i]))(schema_ptrs, info))) {
      } else if (OB_FAIL(infos.push_back(info))) {
      }
    }
    for (int64_t i = 0; i < ARRAYSIZEOF(func_table_column) && OB_SUCC(ret); i++) {
      if (OB_ISNULL(func_table_column[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("pointer is null", KR(ret));
      } else if (OB_FAIL((this->*(func_table_column[i]))(schema_ptrs, info, info_history))) {
      } else if (OB_FAIL(infos.push_back(info))) {
      } else if (OB_FAIL(infos.push_back(info_history))) {
      }
    }
  }
  return ret;
}

int ObInnerTableSchemaDumper::get_table_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
    const ObString &table_name, const ObString &table_name_history, const uint64_t table_id, const uint64_t table_id_history,
    ObLoadInnerTableSchemaInfo &info, ObLoadInnerTableSchemaInfo &info_history)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  ObNotCoreTableLoadInfoConstructor constructor(table_name, table_id, allocator_);
  ObNotCoreTableLoadInfoConstructor constructor_history(table_name_history, table_id_history, allocator_);
  for (int64_t i = 0; i < schema_ptrs.count() && OB_SUCC(ret); i++) {
    ObTableSchema *table = nullptr;
    dml.reset();
    if (OB_ISNULL(table = schema_ptrs.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pointer is null", KR(ret), KP(table), K(i));
    } else if (OB_FAIL(ObTableSqlService::gen_table_dml_without_check(*table,
            false, dml))) {
    } else if (is_core_table(table->get_table_id())) {
    } else if (OB_FAIL(constructor.add_lines(table->get_table_id(), dml))) {
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(dml.add_column("is_deleted", "0"))) {
      } else if (OB_FAIL(constructor_history.add_lines(table->get_table_id(), dml))) {
      }
    }
  }
  if (FAILEDx(constructor.get_load_info(info))) {
  } else if (OB_FAIL(constructor_history.get_load_info(info_history))) {
  }
  return ret;
}

int ObInnerTableSchemaDumper::get_all_table_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
  ObLoadInnerTableSchemaInfo &info, ObLoadInnerTableSchemaInfo &info_history)
{
  return get_table_info_(schema_ptrs, OB_ALL_TABLE_TNAME, OB_ALL_TABLE_HISTORY_TNAME,
                         OB_ALL_TABLE_TID, OB_ALL_TABLE_HISTORY_TID, info, info_history);
}

int ObInnerTableSchemaDumper::get_column_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
    const ObString &table_name, const ObString &table_name_history, const uint64_t table_id, const uint64_t table_id_history,
    ObLoadInnerTableSchemaInfo &info, ObLoadInnerTableSchemaInfo &info_history)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  ObNotCoreTableLoadInfoConstructor constructor(table_name, table_id, allocator_);
  ObNotCoreTableLoadInfoConstructor constructor_history(table_name_history, table_id_history, allocator_);
  for (int64_t i = 0; i < schema_ptrs.count() && OB_SUCC(ret); i++) {
    ObTableSchema *table = nullptr;
    if (OB_ISNULL(table = schema_ptrs.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pointer is null", KR(ret), KP(table), K(i));
    } else {
      for (ObTableSchema::const_column_iterator iter = table->column_begin();
          OB_SUCC(ret) && iter != table->column_end(); ++iter) {
        dml.reset();
        if (OB_ISNULL(iter) || OB_ISNULL(*iter)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("pointer is null", KR(ret), KP(iter));
        } else if (OB_FAIL(ObTableSqlService::gen_column_dml_without_check(**iter,
                lib::Worker::CompatMode::MYSQL, dml))) {
        } else if (is_core_table(table->get_table_id()) ) {
        } else if (OB_FAIL(constructor.add_lines(table->get_table_id(), dml))) {
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(dml.add_column("is_deleted", "0"))) {
          } else if (OB_FAIL(constructor_history.add_lines(table->get_table_id(), dml))) {
          }
        }
      }
    }
  }
  if (FAILEDx(constructor.get_load_info(info))) {
  } else if (OB_FAIL(constructor_history.get_load_info(info_history))) {
  }
  return ret;
}

int ObInnerTableSchemaDumper::get_all_column_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
  ObLoadInnerTableSchemaInfo &info, ObLoadInnerTableSchemaInfo &info_history)
{
  return get_column_info_(schema_ptrs, OB_ALL_COLUMN_TNAME, OB_ALL_COLUMN_HISTORY_TNAME,
                          OB_ALL_COLUMN_TID, OB_ALL_COLUMN_HISTORY_TID, info, info_history);
}

int ObInnerTableSchemaDumper::get_all_ddl_operation_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
    ObLoadInnerTableSchemaInfo &info)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  ObNotCoreTableLoadInfoConstructor constructor(OB_ALL_DDL_OPERATION_TNAME, OB_ALL_DDL_OPERATION_TID, allocator_);
  for (int64_t i = 0; i < schema_ptrs.count() && OB_SUCC(ret); i++) {
    ObTableSchema *table = nullptr;
    dml.reset();
    if (OB_ISNULL(table = schema_ptrs.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pointer is null", KR(ret), KP(table), K(i));
    } else {
      ObSchemaOperationType op_type;
      if (table->is_index_table()) {
        op_type = table->is_global_index_table() ? OB_DDL_CREATE_GLOBAL_INDEX : OB_DDL_CREATE_INDEX;
      } else if (table->is_view_table()) {
        op_type = OB_DDL_CREATE_VIEW;
      } else {
        op_type = OB_DDL_CREATE_TABLE;
      }
      // modify here if change __all_ddl_operation schema
      const int64_t line_begin = __LINE__;
      OZ (dml.add_gmt_create());
      OZ (dml.add_gmt_modified());
      OZ (dml.add_column("schema_version", table->get_schema_version()));
      OZ (dml.add_column("user_id", 0));
      OZ (dml.add_column("database_id", table->get_database_id()));
      OZ (dml.add_column("database_name", ""));
      OZ (dml.add_column("tablegroup_id", table->get_tablegroup_id()));
      OZ (dml.add_column("table_id", table->get_table_id()));
      OZ (dml.add_column("table_name", ""));
      OZ (dml.add_column("operation_type", op_type));
      OZ (dml.add_column("ddl_stmt_str", ""));
      const int64_t line_end = __LINE__;
      if (OB_SUCC(ret) && table->get_table_id() == OB_ALL_DDL_OPERATION_TID && 
          table->get_column_count() != line_end - line_begin - 1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("__all_ddl_operation table schema is changed", KR(ret),
            "count", line_end - line_begin - 1, K(table->get_column_count()));
      }
    }
    if (FAILEDx(constructor.add_lines(table->get_table_id(), dml))) {
    }
  }
  if (FAILEDx(constructor.get_load_info(info))) {
  }
  return ret;
}

int ObInnerTableSchemaDumper::get_all_core_table_info_(const ObIArray<schema::ObTableSchema *> &schema_ptrs,
    ObLoadInnerTableSchemaInfo &info)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer dml;
  ObCoreTableLoadInfoConstructor table_constructor(OB_ALL_TABLE_TNAME, allocator_);
  ObCoreTableLoadInfoConstructor column_constructor(OB_ALL_COLUMN_TNAME, allocator_);
  ObMergeLoadInfoConstructor constructor(OB_ALL_CORE_TABLE_TNAME, OB_ALL_CORE_TABLE_TID, allocator_);
  for (int64_t i = 0; i < schema_ptrs.count() && OB_SUCC(ret); i++) {
    ObTableSchema *table = nullptr;
    dml.reset();
    if (OB_ISNULL(table = schema_ptrs.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("pointer is null", KR(ret), KP(table), K(i));
    } else if (!is_core_table(table->get_table_id())) {
    } else if (OB_FAIL(ObTableSqlService::gen_table_dml_without_check(*table,
            false, dml))) {
    } else if (OB_FAIL(table_constructor.add_lines(table->get_table_id(), dml))) {
    } else {
      for (ObTableSchema::const_column_iterator iter = table->column_begin();
          OB_SUCC(ret) && iter != table->column_end(); ++iter) {
        dml.reset();
        if (OB_ISNULL(iter) || OB_ISNULL(*iter)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("pointer is null", KR(ret), KP(iter));
        } else if (OB_FAIL(ObTableSqlService::gen_column_dml_without_check(**iter,
                lib::Worker::CompatMode::MYSQL, dml))) {
        } else if (OB_FAIL(column_constructor.add_lines(table->get_table_id(), dml))) {
        }
      }
    }
  }
  if (FAILEDx(constructor.add_constructor(table_constructor))) {
  } else if (OB_FAIL(constructor.add_constructor(column_constructor))) {
  } else if (OB_FAIL(constructor.get_load_info(info))) {
  }
  return ret;
}

int ObLoadInnerTableSchemaInfoConstructor::add_line(const ObString &line,
    const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObString tmp_line;

  if (FAILEDx(ob_write_string(allocator_, line, tmp_line, true/*c_style*/))) {
  } else if (OB_FAIL(rows_.push_back(tmp_line))) {
  } else if (OB_FAIL(table_ids_.push_back(table_id))) {
  }
  return ret;
}

bool ObLoadInnerTableSchemaInfoConstructor::is_valid() const
{
  bool valid = true;
  if (!is_system_table(table_id_)) {
    valid = false;
  } else if (rows_.count() != table_ids_.count()) {
    valid = false;
  }
  return valid;
}

int ObLoadInnerTableSchemaInfoConstructor::get_load_info(ObLoadInnerTableSchemaInfo &info)
{
  int ret = OB_SUCCESS;
  uint64_t *table_id_buf = nullptr;
  const char **row_buf = nullptr;
  ObString table_name;
  ObString header;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("constructor is invalid", KR(ret), K(*this));
  } else if (OB_FAIL(ob_write_string(allocator_, table_name_, table_name, true/*c_style*/))) {
  } else if (OB_FAIL(ob_write_string(allocator_, header_, header, true/*c_style*/))) {
  } else if (OB_ISNULL(table_id_buf = static_cast<uint64_t *>(allocator_.alloc(sizeof(table_id_buf[0]) * table_ids_.count())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc table_ids_ memory", KR(ret), K(table_ids_.count()));
  } else if (OB_ISNULL(row_buf = static_cast<const char **>(allocator_.alloc(sizeof(row_buf[0]) * rows_.count())))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc rows_ memory", KR(ret), K(rows_.count()));
  } else {
    for (int64_t i = 0; i < table_ids_.count(); i++) {
      table_id_buf[i] = table_ids_[i];
    }
    for (int64_t i = 0; i < rows_.count(); i++) {
      row_buf[i] = rows_[i].ptr();
    }
    info = ObLoadInnerTableSchemaInfo(
        table_id_,
        table_name.ptr(),
        header.ptr(),
        table_id_buf,
        row_buf,
        rows_.count()
    );
  }
  return ret;
}

int ObNotCoreTableLoadInfoConstructor::add_header(ObDMLSqlSplicer &splicer)
{
  int ret = OB_SUCCESS;
  if (header_.empty()) {
    ObSqlString header;
    if (OB_FAIL(splicer.splice_column_names(header))) {
    } else if (OB_FAIL(ob_write_string(allocator_, header.string(), header_, true/*c_style*/))) {
    }
  }
  return ret;
}

int ObNotCoreTableLoadInfoConstructor::add_lines(const uint64_t table_id, ObDMLSqlSplicer &splicer)
{
  int ret = OB_SUCCESS;
  ObSqlString line;
  if (OB_FAIL(add_header(splicer))) {
  } else if (OB_FAIL(splicer.splice_values(line))) {
  } else if (OB_FAIL(add_line(line.string(), table_id))) {
  }
  return ret;
}

int ObCoreTableLoadInfoConstructor::add_lines(const uint64_t table_id, ObDMLSqlSplicer &splicer)
{
  int ret = OB_SUCCESS;
  ObArray<ObCoreTableProxy::UpdateCell> cells;
  const char *header = "table_name,row_id,column_name,column_value";
  header_ = header;
  if (OB_FAIL(splicer.splice_core_cells(store_cell_, cells))) {
  } else {
    row_id_++;
    ObSqlString line;
    for (int64_t i = 0; i < cells.count() && OB_SUCC(ret); i++) {
      ObCoreTableProxy::Cell cell = cells.at(i).cell_;
      line.reset();
      if (OB_ISNULL(cell.value_.ptr())) {
        if (OB_FAIL(line.assign_fmt("'%.*s', %ld, '%.*s', NULL", core_table_name_.length(),
                core_table_name_.ptr(), row_id_, cell.name_.length(), cell.name_.ptr()))) {
        }
      } else if (cell.is_hex_value_) {
        if (OB_FAIL(line.append_fmt("'%.*s', %ld, '%.*s', %.*s", core_table_name_.length(),
                core_table_name_.ptr(), row_id_, cell.name_.length(), cell.name_.ptr(),
                cell.value_.length(), cell.value_.ptr()))) {
        }
      } else {
        if (OB_FAIL(line.append_fmt("'%.*s', %ld, '%.*s', '%.*s'", core_table_name_.length(),
                core_table_name_.ptr(), row_id_, cell.name_.length(), cell.name_.ptr(),
                cell.value_.length(), cell.value_.ptr()))) {
        }
      }
      if (FAILEDx(add_line(line.string(), table_id))) {
      }
    }
  }
  return ret;
}

int ObCoreTableLoadInfoConstructor::DumpCoreTableStoreCell::store_string(
    const common::ObString &src, common::ObString &dest)
{
  return ob_write_string(allocator_, src, dest, true/*c_style*/);
}

int ObCoreTableLoadInfoConstructor::DumpCoreTableStoreCell::store_cell(
    const ObCoreTableCell &src, ObCoreTableCell &dest)
{
  int ret = OB_SUCCESS;
  if (!src.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(src));
  } else if (OB_FAIL(store_string(src.name_, dest.name_))) {
  } else if (OB_FAIL(store_string(src.value_, dest.value_))) {
  } else {
    dest.is_hex_value_ = src.is_hex_value_;
  }
  return ret;
}

int ObMergeLoadInfoConstructor::add_lines(const uint64_t table_id, ObDMLSqlSplicer &splicer)
{
  return OB_NOT_SUPPORTED;
}

int ObMergeLoadInfoConstructor::add_constructor(ObLoadInnerTableSchemaInfoConstructor &constructor)
{
  int ret = OB_SUCCESS;
  if (constructor.get_table_id() != table_id_ || constructor.get_table_name() != table_name_ 
      || &constructor.get_allocator() != &allocator_ || (!header_.empty() && constructor.get_header() != header_) 
      || !is_valid() || !constructor.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("two construct is not same, cannot merge", KR(ret), KPC(this), K(constructor));
  } else if (OB_FAIL(rows_.push_back(constructor.get_rows()))) {
  } else if (OB_FAIL(table_ids_.push_back(constructor.get_table_ids()))) {
  } else {
    header_ = constructor.get_header();
  }
  return ret;
}

}
}
