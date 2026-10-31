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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/cmd/ob_ai_column_ddl_util.h"

#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/string/ob_sql_string.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/schema/ob_table_schema.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace sql
{

using namespace common;
using namespace share;
using namespace share::schema;

namespace
{

int fetch_column_ids(common::ObISQLClient &client,
                     const uint64_t table_id,
                     common::ObIArray<common::ObString> &names,
                     common::ObIArray<uint64_t> &ids)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_FAIL(sql.append_fmt(
        "SELECT column_id, column_name FROM oceanbase.__all_column WHERE table_id = %lu",
        table_id))) {
  } else {
    ObISQLClient::ReadResult res;
    if (OB_FAIL(client.read(res, sql.ptr()))) {
      LOG_WARN("failed to read column ids", K(ret), K(table_id));
    } else if (OB_ISNULL(res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      common::sqlclient::ObMySQLResult *result = res.get_result();
      while (OB_SUCC(ret)) {
        int64_t column_id = 0;
        ObString column_name;
        if (OB_FAIL(result->next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          }
        } else if (OB_FAIL(result->get_int("column_id", column_id))) {
        } else if (OB_FAIL(result->get_varchar("column_name", column_name))) {
        } else if (OB_FAIL(names.push_back(column_name))) {
        } else if (OB_FAIL(ids.push_back(static_cast<uint64_t>(column_id)))) {
        }
      }
    }
  }
  return ret;
}

uint64_t find_column_id(const common::ObIArray<common::ObString> &names,
                        const common::ObIArray<uint64_t> &ids,
                        const common::ObString &name)
{
  uint64_t column_id = 0;
  for (int64_t i = 0; i < names.count(); ++i) {
    if (names.at(i) == name) {
      column_id = ids.at(i);
      break;
    }
  }
  return column_id;
}

// Collects column names/ids straight from the in-memory table schema, so the
// DDL executor never has to issue inner SQL (which deadlocks on the request
// worker while the DDL statement is still executing).
int collect_column_ids_from_schema(const share::schema::ObTableSchema &table_schema,
                                   common::ObIArray<common::ObString> &names,
                                   common::ObIArray<uint64_t> &ids)
{
  int ret = OB_SUCCESS;
  const int64_t column_count = table_schema.get_column_count();
  for (int64_t i = 0; OB_SUCC(ret) && i < column_count; ++i) {
    const share::schema::ObColumnSchemaV2 *column = table_schema.get_column_schema_by_idx(i);
    if (OB_ISNULL(column)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("column schema is null", K(ret), K(i));
    } else if (OB_FAIL(names.push_back(column->get_column_name_str()))
               || OB_FAIL(ids.push_back(column->get_column_id()))) {
    }
  }
  return ret;
}

int build_hidden_column_name(const common::ObString &column_name,
                             const char *suffix,
                             common::ObSqlString &out)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(out.append_fmt("__ai_%.*s%s",
                             column_name.length(), column_name.ptr(), suffix))) {
  }
  return ret;
}

} // anonymous namespace

int ObAiColumnDdlUtil::persist_ai_columns(const uint64_t table_id,
                                          const common::ObIArray<ObAiColumnDef> &defs,
                                          const share::schema::ObTableSchema *table_schema)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  ObSEArray<ObString, 32> column_names;
  ObSEArray<uint64_t, 32> column_ids;
  if (defs.empty()) {
    // nothing to do
  } else if (OB_INVALID_ID == table_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table id for ai column definition", K(ret), K(table_id));
  } else if (NULL != table_schema
             && OB_FAIL(collect_column_ids_from_schema(*table_schema, column_names, column_ids))) {
  } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
    LOG_WARN("failed to start transaction", K(ret));
  } else if (NULL == table_schema
             && OB_FAIL(fetch_column_ids(trans, table_id, column_names, column_ids))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < defs.count(); ++i) {
      const ObAiColumnDef &def = defs.at(i);
      const uint64_t result_column_id = find_column_id(column_names, column_ids, def.column_name_);
      ObSqlString state_name;
      ObSqlString request_name;
      ObSqlString spec_name;
      uint64_t state_column_id = 0;
      uint64_t request_column_id = 0;
      uint64_t spec_column_id = 0;
      if (0 == result_column_id) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ai column is not found in the catalog", K(ret), K(table_id), K(def.column_name_));
      } else if (OB_FAIL(build_hidden_column_name(def.column_name_, AI_COLUMN_STATE_SUFFIX, state_name))) {
      } else if (OB_FAIL(build_hidden_column_name(def.column_name_, AI_COLUMN_REQUEST_SUFFIX, request_name))) {
      } else if (OB_FAIL(build_hidden_column_name(def.column_name_, AI_COLUMN_SPEC_SUFFIX, spec_name))) {
      } else {
        state_column_id = find_column_id(column_names, column_ids, state_name.string());
        request_column_id = find_column_id(column_names, column_ids, request_name.string());
        spec_column_id = find_column_id(column_names, column_ids, spec_name.string());
      }
      if (OB_SUCC(ret)) {
        ObDMLSqlSplicer dml;
        ObSqlString buffer;
        int64_t affected_rows = 0;
        if (OB_FAIL(dml.add_pk_column("table_id", static_cast<int64_t>(table_id)))) {
        } else if (OB_FAIL(dml.add_pk_column("column_id", static_cast<int64_t>(result_column_id)))) {
        } else if (OB_FAIL(dml.add_column("column_name", ObHexEscapeSqlStr(def.column_name_)))) {
        } else if (OB_FAIL(dml.add_column("model_name", ObHexEscapeSqlStr(def.model_name_)))) {
        } else if (OB_FAIL(dml.add_column("ai_func_type", def.ai_func_type_))) {
        } else if (OB_FAIL(dml.add_column("result_type", def.result_type_))) {
        } else if (OB_FAIL(dml.add_column("normalized_expr", ObHexEscapeSqlStr(def.normalized_expr_)))) {
        } else if (OB_FAIL(dml.add_column("input_expr", ObHexEscapeSqlStr(def.input_expr_)))) {
        } else if (OB_FAIL(dml.add_column("max_retries", def.max_retries_))) {
        } else if (OB_FAIL(dml.add_column("sync_mode", def.sync_mode_))) {
        } else if (OB_FAIL(dml.add_column("definition_version", static_cast<int64_t>(def.definition_version_)))) {
        } else if (OB_FAIL(dml.add_column("state_column_id", static_cast<int64_t>(state_column_id)))) {
        } else if (OB_FAIL(dml.add_column("request_column_id", static_cast<int64_t>(request_column_id)))) {
        } else if (OB_FAIL(dml.add_column("result_spec_column_id", static_cast<int64_t>(spec_column_id)))) {
        } else if (OB_FAIL(dml.splice_insert_sql(OB_ALL_AI_COLUMN_TNAME, buffer))) {
        } else if (OB_FAIL(trans.write(buffer.ptr(), affected_rows))) {
        } else if (!is_single_row(affected_rows)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected affected rows", K(ret), K(affected_rows));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(trans.end(true))) {
      LOG_WARN("failed to commit ai column definition", K(ret), K(table_id));
    }
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
      LOG_WARN("failed to rollback ai column definition", K(tmp_ret));
    }
  }
  return ret;
}

int ObAiColumnDdlUtil::drop_ai_columns(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_INVALID_ID == table_id) {
    // nothing to do
  } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
  } else if (OB_FAIL(sql.append_fmt("DELETE FROM %s WHERE table_id = %lu",
                                    OB_ALL_AI_COLUMN_TNAME, table_id))) {
  } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
  } else if (FALSE_IT(sql.reuse())) {
  } else if (OB_FAIL(sql.append_fmt("DELETE FROM %s WHERE table_id = %lu",
                                    OB_ALL_AI_COLUMN_TASK_TNAME, table_id))) {
  } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(trans.end(true))) {
    }
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
      LOG_WARN("failed to rollback ai column cleanup", K(tmp_ret));
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
