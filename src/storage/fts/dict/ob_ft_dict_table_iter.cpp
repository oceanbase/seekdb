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

#include "storage/fts/dict/ob_ft_dict_table_iter.h"

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"

#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{
namespace
{
int split_qualified_table_name(const ObString &qualified_name,
                               ObString &database_name,
                               ObString &table_name)
{
  int ret = OB_SUCCESS;
  int64_t dot_pos = -1;
  for (int64_t index = 0; index < qualified_name.length(); ++index) {
    if ('.' == qualified_name.ptr()[index]) {
      if (dot_pos >= 0) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("dictionary table name contains multiple separators", K(ret), K(qualified_name));
        break;
      }
      dot_pos = index;
    }
  }
  if (OB_SUCC(ret)) {
    if (dot_pos <= 0 || dot_pos >= qualified_name.length() - 1) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("dictionary table name must be database qualified", K(ret), K(qualified_name));
    } else {
      database_name.assign_ptr(qualified_name.ptr(), static_cast<int32_t>(dot_pos));
      table_name.assign_ptr(qualified_name.ptr() + dot_pos + 1,
                            static_cast<int32_t>(qualified_name.length() - dot_pos - 1));
    }
  }
  return ret;
}

int append_escaped_identifier(ObSqlString &sql, const ObString &identifier)
{
  int ret = OB_SUCCESS;
  if (identifier.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary table identifier is empty", K(ret));
  } else if (OB_FAIL(sql.append("`"))) {
    LOG_WARN("failed to append dictionary identifier quote", K(ret));
  } else {
    for (int64_t index = 0; OB_SUCC(ret) && index < identifier.length(); ++index) {
      if ('`' == identifier.ptr()[index]) {
        ret = sql.append("``");
      } else {
        ret = sql.append(identifier.ptr() + index, 1);
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(sql.append("`"))) {
      LOG_WARN("failed to append dictionary identifier quote", K(ret));
    }
  }
  return ret;
}

int append_qualified_table_name(ObSqlString &sql,
                                const ObString &database_name,
                                const ObString &table_name)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append_escaped_identifier(sql, database_name))) {
    LOG_WARN("failed to append dictionary database name", K(ret), K(database_name));
  } else if (OB_FAIL(sql.append("."))) {
    LOG_WARN("failed to append dictionary name separator", K(ret));
  } else if (OB_FAIL(append_escaped_identifier(sql, table_name))) {
    LOG_WARN("failed to append dictionary table name", K(ret), K(table_name));
  }
  return ret;
}
}

ObFTDictTableIter::ObFTDictTableIter(ObISQLClient::ReadResult &result)
    : ObIFTDictIterator(), is_inited_(false), is_empty_(false), res_(result)
{
}

int ObFTDictTableIter::get_key(ObString &str)
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited.", K(ret));
  } else if (OB_FAIL(res_.get_result()->get_varchar("word", str))) {
    LOG_WARN("Failed to get varchar", K(ret));
  }
  return ret;
}

int ObFTDictTableIter::get_value()
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObFTDictTableIter::next()
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited.", K(ret));
  } else if (OB_FAIL(res_.get_result()->next())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("Failed to get next row", K(ret));
    }
  }
  return ret;
}

int ObFTDictTableIter::init(const ObString &table_name)
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  ObString database_name;
  ObString local_table_name;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Inited twice.", K(ret));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(split_qualified_table_name(table_name, database_name, local_table_name))) {
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema(database_name,
                                                   local_table_name,
                                                   false,
                                                   table_schema))) {
    LOG_WARN("get dictionary table schema failed", K(ret), K(database_name), K(local_table_name));
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("dictionary table does not exist", K(ret), K(database_name), K(local_table_name));
  } else if (!table_schema->is_valid_fulltext_dict_table_schema()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext dictionary table schema", K(ret), K(database_name), K(local_table_name));
  } else {
    SMART_VAR(ObSqlString, sql_string)
    {
      if (OB_FAIL(sql_string.append("SELECT LOWER(word) AS word FROM "))) {
        LOG_WARN("Failed to append sql", K(ret));
      } else if (OB_FAIL(append_qualified_table_name(sql_string,
                                                     database_name,
                                                     local_table_name))) {
        LOG_WARN("Failed to append dictionary table name", K(ret));
      } else if (OB_FAIL(sql_string.append(" ORDER BY word COLLATE utf8mb4_bin"))) {
        LOG_WARN("Failed to append sql", K(ret));
      } else if (OB_FAIL(sql_proxy->read(res_, sql_string.ptr()))) {
        LOG_WARN("Failed to execute sql", K(ret));
      }
    }

    if (OB_FAIL(ret)) {
      // already logged
    } else if (OB_ISNULL(res_.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Failed to get result", K(ret));
    } else if (OB_FAIL(res_.get_result()->next())) {
      if (OB_ITER_END != ret) {
        LOG_WARN("Failed to get next row", K(ret));
      } else {
        ret = OB_SUCCESS;
        is_inited_ = true;
        is_empty_ = true;
      }
    } else {
      is_inited_ = true;
    }
  }

  return ret;
}

void ObFTDictTableIter::reset()
{
  res_.close();
  is_inited_ = false;
  is_empty_ = false;
}

} //  namespace storage
} //  namespace oceanbase
