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
#include "lib/string/ob_sql_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_server_struct.h"

#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{
namespace
{
int append_escaped_identifier(ObSqlString &sql_string, const ObString &identifier)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(identifier.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("identifier is empty", K(ret));
  } else if (OB_FAIL(sql_string.append("`"))) {
    LOG_WARN("Failed to append sql", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < identifier.length(); ++i) {
      const char ch = identifier.ptr()[i];
      if ('`' == ch) {
        if (OB_FAIL(sql_string.append("``"))) {
          LOG_WARN("Failed to append escaped identifier quote", K(ret));
        }
      } else if (OB_FAIL(sql_string.append(&ch, 1))) {
        LOG_WARN("Failed to append identifier char", K(ret));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(sql_string.append("`"))) {
      LOG_WARN("Failed to append sql", K(ret));
    }
  }
  return ret;
}

int split_qualified_table_name(const ObString &qualified_table_name,
                               ObString &database_name,
                               ObString &table_name)
{
  int ret = OB_SUCCESS;
  const char *dot = qualified_table_name.find('.');
  if (OB_ISNULL(dot)
      || OB_UNLIKELY(dot == qualified_table_name.ptr())
      || OB_UNLIKELY(dot == qualified_table_name.ptr() + qualified_table_name.length() - 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dict table name", K(ret), K(qualified_table_name));
  } else {
    database_name.assign_ptr(qualified_table_name.ptr(),
                             static_cast<int32_t>(dot - qualified_table_name.ptr()));
    table_name.assign_ptr(dot + 1,
                          static_cast<int32_t>(qualified_table_name.length()
                                               - (dot - qualified_table_name.ptr()) - 1));
  }
  return ret;
}
}

ObFTDictTableIter::ObFTDictTableIter(ObISQLClient::ReadResult &result)
    : ObIFTDictIterator(), is_inited_(false), is_iter_end_(false), res_(result)
{
}

int ObFTDictTableIter::get_key(ObString &str)
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited.", K(ret));
  } else if (is_iter_end_) {
    ret = OB_ITER_END;
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
  } else if (is_iter_end_) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(res_.get_result()->next())) {
    if (OB_ITER_END != ret) {
      LOG_WARN("Failed to get next row", K(ret));
    } else {
      is_iter_end_ = true;
    }
  }
  return ret;
}

int ObFTDictTableIter::init(const ObString &table_name)
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  ObString database_name;
  ObString real_table_name;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Inited twice.", K(ret));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (OB_FAIL(split_qualified_table_name(table_name, database_name, real_table_name))) {
    LOG_WARN("Failed to split dict table name", K(ret), K(table_name));
  } else {
    SMART_VAR(ObSqlString, sql_string)
    {
      if (OB_FAIL(sql_string.append("SELECT word FROM "))) {
        LOG_WARN("Failed to append sql", K(ret));
      } else if (OB_FAIL(append_escaped_identifier(sql_string, database_name))) {
        LOG_WARN("Failed to append sql", K(ret));
      } else if (OB_FAIL(sql_string.append("."))) {
        LOG_WARN("Failed to append sql", K(ret));
      } else if (OB_FAIL(append_escaped_identifier(sql_string, real_table_name))) {
        LOG_WARN("Failed to append sql", K(ret));
      } else if (OB_FAIL(sql_string.append(" ORDER BY word"))) {
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
        is_inited_ = true;
        is_iter_end_ = true;
        ret = OB_SUCCESS;
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
  is_iter_end_ = false;
}

} //  namespace storage
} //  namespace oceanbase
