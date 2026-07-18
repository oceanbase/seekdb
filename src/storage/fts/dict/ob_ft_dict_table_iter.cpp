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

#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{
namespace
{
bool is_valid_identifier(const ObString &identifier)
{
  bool valid = !identifier.empty();
  for (int64_t i = 0; valid && i < identifier.length(); ++i) {
    const char c = identifier.ptr()[i];
    valid = ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')
            || ('0' <= c && c <= '9') || '_' == c || '$' == c;
  }
  return valid;
}
}

ObFTDictTableIter::ObFTDictTableIter(ObISQLClient::ReadResult &result)
    : ObIFTDictIterator(), is_inited_(false), res_(result)
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
  ObString pure_table_name;
  const char *dot = table_name.find('.');

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Inited twice.", K(ret));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("SQL proxy is null", K(ret));
  } else if (OB_NOT_NULL(dot)) {
    database_name.assign_ptr(table_name.ptr(), static_cast<int32_t>(dot - table_name.ptr()));
    pure_table_name.assign_ptr(dot + 1,
                               static_cast<int32_t>(table_name.ptr() + table_name.length() - dot - 1));
    if (OB_NOT_NULL(pure_table_name.find('.'))
        || !is_valid_identifier(database_name)
        || !is_valid_identifier(pure_table_name)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Invalid qualified dictionary table name", K(ret), K(table_name));
    }
  } else if (!is_valid_identifier(table_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid dictionary table name", K(ret), K(table_name));
  } else {
    pure_table_name = table_name;
  }

  if (OB_FAIL(ret)) {
  } else {
    SMART_VAR(ObSqlString, sql_string)
    {
      if (!database_name.empty()
          && OB_FAIL(sql_string.append_fmt("SELECT word FROM `%.*s`.`%.*s` ORDER BY word",
                                           database_name.length(), database_name.ptr(),
                                           pure_table_name.length(), pure_table_name.ptr()))) {
        LOG_WARN("Failed to format qualified dictionary query", K(ret));
      } else if (database_name.empty()
                 && OB_FAIL(sql_string.append_fmt("SELECT word FROM `%.*s` ORDER BY word",
                                                  pure_table_name.length(), pure_table_name.ptr()))) {
        LOG_WARN("Failed to format dictionary query", K(ret));
      } else if (OB_FAIL(sql_proxy->read(res_, sql_string.ptr()))) {
        LOG_WARN("Failed to execute dictionary query", K(ret), K(sql_string));
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
}

} //  namespace storage
} //  namespace oceanbase
