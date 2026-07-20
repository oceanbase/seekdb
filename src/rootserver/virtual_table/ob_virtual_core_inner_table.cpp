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

#include "ob_virtual_core_inner_table.h"

#include "share/ob_core_table_proxy.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;

namespace rootserver
{
ObVritualCoreInnerTable::ObVritualCoreInnerTable()
  : inited_(false), sql_proxy_(NULL),
    table_name_(NULL), table_id_(OB_INVALID_ID),
    schema_guard_(NULL)
{
}

ObVritualCoreInnerTable::~ObVritualCoreInnerTable()
{
}

int ObVritualCoreInnerTable::init(
    ObMySQLProxy &sql_proxy, const char *table_name,
    const uint64_t table_id, ObSchemaGetterGuard *schema_guard)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (!sql_proxy.is_inited() || NULL == table_name
      || OB_INVALID_ID == table_id || NULL == schema_guard) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", "sql_proxy_inited", sql_proxy.is_inited(),
        KP(table_name), KT(table_id), KP(schema_guard), K(ret));
  } else {
    sql_proxy_ = &sql_proxy;
    table_name_ = table_name;
    table_id_ = table_id;
    schema_guard_ = schema_guard;
    inited_ = true;
  }
  return ret;
}

int ObVritualCoreInnerTable::inner_open()
{
  int ret = OB_SUCCESS;
  const ObTableSchema *table_schema = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init, allocator is null", KR(ret));
  } else if (OB_FAIL(schema_guard_->get_table_schema( table_id_, table_schema))) {
    LOG_WARN("fail to get table schema", KR(ret), K_(table_id));
  } else if (NULL == table_schema) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("get_table_schema failed", KT_(table_id), KR(ret));
  } else {
    {
      
      const ObSimpleTenantSchema *tenant = NULL;
      if (OB_FAIL(schema_guard_->get_tenant_info(tenant))) {
        LOG_WARN("fail to get tenant info", KR(ret));
      } else if (OB_ISNULL(tenant) || !tenant->is_normal()) {
        // skip
      } else {
        ObCoreTableProxy core_table(table_name_, *sql_proxy_);
        if (OB_FAIL(core_table.load())) {
          LOG_WARN("core_table load failed", KR(ret));
        } else {
          struct CoreHistoryRowInfo
          {
            int64_t table_id_;
            int64_t column_id_;
            int64_t schema_version_;
            int64_t row_id_;
            bool is_deleted_;
            TO_STRING_EMPTY();
          };
          ObArray<CoreHistoryRowInfo> latest_rows;
          const bool is_column_table = OB_ALL_VIRTUAL_CORE_COLUMN_TABLE_TID == table_id_;
          while (OB_SUCC(ret) && OB_SUCC(core_table.next())) {
            const ObCoreTableProxy::Row *row = NULL;
            int64_t table_id = OB_INVALID_ID;
            int64_t column_id = OB_INVALID_ID;
            int64_t schema_version = OB_INVALID_VERSION;
            int64_t deleted = 0;
            if (OB_FAIL(core_table.get_cur_row(row))) {
              LOG_WARN("get current row failed", KR(ret));
            } else if (OB_ISNULL(row)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("current row is null", KR(ret));
            } else if (OB_FAIL(row->get_int("table_id", table_id))
                || (is_column_table && OB_FAIL(row->get_int("column_id", column_id)))
                || OB_FAIL(row->get_int("schema_version", schema_version))
                || OB_FAIL(row->get_int("is_deleted", deleted))) {
              LOG_WARN("get core history rowkey failed", KR(ret), K(is_column_table));
            } else {
              int64_t idx = 0;
              for (; idx < latest_rows.count(); ++idx) {
                if (latest_rows.at(idx).table_id_ == table_id
                    && (!is_column_table || latest_rows.at(idx).column_id_ == column_id)) {
                  break;
                }
              }
              if (idx == latest_rows.count()) {
                const CoreHistoryRowInfo latest_row = {
                  table_id, column_id, schema_version, row->get_row_id(), 0 != deleted
                };
                if (OB_FAIL(latest_rows.push_back(latest_row))) {
                  LOG_WARN("save core history row failed", KR(ret), K(table_id),
                           K(column_id), K(schema_version));
                }
              } else if (schema_version > latest_rows.at(idx).schema_version_) {
                CoreHistoryRowInfo &latest_row = latest_rows.at(idx);
                latest_row.schema_version_ = schema_version;
                latest_row.row_id_ = row->get_row_id();
                latest_row.is_deleted_ = 0 != deleted;
              }
            }
          }
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          }
          if (OB_SUCC(ret) && OB_FAIL(core_table.seek_to_head())) {
            LOG_WARN("seek core history to head failed", KR(ret));
          }
          ObArray<Column> columns;
          while (OB_SUCC(ret) && OB_SUCC(core_table.next())) {
            const ObCoreTableProxy::Row *row = NULL;
            if (OB_FAIL(core_table.get_cur_row(row))) {
              LOG_WARN("get current row failed", KR(ret));
            } else if (OB_ISNULL(row)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("current row is null", KR(ret));
            } else {
              int64_t idx = 0;
              for (; idx < latest_rows.count()
                  && latest_rows.at(idx).row_id_ != row->get_row_id(); ++idx) {}
              if (idx == latest_rows.count() || latest_rows.at(idx).is_deleted_) {
                // Skip obsolete history rows and tombstones.
              } else {
                columns.reuse();
                if (OB_FAIL(get_full_row(table_schema, core_table, columns))) {
                  LOG_WARN("get_full_row failed", K(table_schema), KR(ret));
                } else if (OB_FAIL(project_row(columns, cur_row_))) {
                  LOG_WARN("project_row failed", K(columns), KR(ret));
                } else if (OB_FAIL(scanner_.add_row(cur_row_))) {
                  LOG_WARN("add_row failed", K_(cur_row), KR(ret));
                }
              }
            }
          } // end while
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          }
        }
      }
    } // end for
  }
  if (OB_SUCC(ret)) {
    scanner_it_ = scanner_.begin();
    start_to_read_ = true;
  }
  return ret;
}
int ObVritualCoreInnerTable::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(scanner_it_.get_next_row(cur_row_))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("get_next_row failed", KR(ret));
    }
  } else {
    row = &cur_row_;
  }
  return ret;
}

int ObVritualCoreInnerTable::get_full_row(const ObTableSchema *table,
    const ObCoreTableProxy &core_table,
    ObIArray<Column> &columns)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (NULL == table) {
    // core_table doesn't need to check
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table is null", K(ret));
  } else {
    const ObColumnSchemaV2 *column_schema = NULL;
    const char *column_name = NULL;
    ObString str_column;
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      if (NULL == (column_schema = table->get_column_schema(output_column_ids_.at(i)))) {
        ret = OB_SCHEMA_ERROR;
        LOG_WARN("column id not exist", "column_id", output_column_ids_.at(i), K(ret));
      } else if (NULL == (column_name = column_schema->get_column_name())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column name is null", K(column_schema), K(ret));
      } else {
        int inner_ret = OB_SUCCESS;
        if (ObVarcharType == column_schema->get_data_type()
            || ObLongTextType == column_schema->get_data_type()) {
          str_column.reset();
          if (OB_SUCCESS == (inner_ret = core_table.get_varchar(
              column_name, str_column))) {
            if (ObVarcharType == column_schema->get_data_type()) {
              ADD_COLUMN(set_varchar, table, column_name, str_column, columns);
            } else if (ObLongTextType == column_schema->get_data_type()) {
              ADD_TEXT_COLUMN(ObLongTextType, table, column_name, str_column, columns);
            }
          } else if (OB_ERR_NULL_VALUE == inner_ret) {
            ADD_NULL_COLUMN(table, column_name, columns);
          } else {
            ret = inner_ret;
            LOG_WARN("get_varchar failed", K(column_name), K(ret));
          }
        } else if (ObIntType == column_schema->get_data_type()
            || ObTinyIntType == column_schema->get_data_type()
            || ObUInt64Type == column_schema->get_data_type()) {
          int64_t int_column = 0;
          if (OB_SUCCESS == (inner_ret = core_table.get_int(
              column_name, int_column))) {
            if (ObIntType == column_schema->get_data_type()) {
              ADD_COLUMN(set_int, table, column_name, int_column, columns);
            } else if (ObTinyIntType == column_schema->get_data_type()) {
              ADD_COLUMN(set_tinyint, table, column_name, static_cast<int8_t>(int_column), columns);
            } else {
              ADD_COLUMN(set_uint64, table, column_name, int_column, columns);
            }
          } else if (OB_ERR_NULL_VALUE == inner_ret) {
            ADD_NULL_COLUMN(table, column_name, columns);
          } else {
            ret = inner_ret;
            LOG_WARN("get_int failed", K(column_name), K(ret));
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("data type is not expected", "data_type",
              column_schema->get_data_type(), K(ret));
        }
      }
    }
  }
  return ret;
}

}//end namespace rootserver
}//end namespace oceanbase
