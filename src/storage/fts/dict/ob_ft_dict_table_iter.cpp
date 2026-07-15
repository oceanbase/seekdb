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
ObFTDictTableIter::ObFTDictTableIter(ObISQLClient::ReadResult &result)
    : ObIFTDictIterator(), is_inited_(false), has_current_row_(false), res_(result)
{
}

int ObFTDictTableIter::get_key(ObString &str)
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited.", K(ret));
  } else if (!has_current_row_) {
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
  } else if (OB_FAIL(res_.get_result()->next())) {
    if (OB_ITER_END == ret) {
      has_current_row_ = false;
    } else {
      LOG_WARN("Failed to get next row", K(ret));
    }
  } else {
    has_current_row_ = true;
  }
  return ret;
}

int ObFTDictTableIter::init(const ObString &table_name)
{
  return init(
      ObString::make_string("oceanbase"),
      table_name);
}

int ObFTDictTableIter::init(
    const ObString &database_name,
    const ObString &table_name)
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("dictionary table iterator initialized twice",
             K(ret));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (database_name.empty()
             || table_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("database name or table name is empty",
             K(ret),
             K(database_name),
             K(table_name));
  } else {
    SMART_VAR(ObSqlString, sql_string)
    {
      if (OB_FAIL(sql_string.append_fmt(
              "SELECT word FROM `%.*s`.`%.*s` ORDER BY word",
              database_name.length(),
              database_name.ptr(),
              table_name.length(),
              table_name.ptr()))) {
        LOG_WARN("failed to build dictionary query",
                 K(ret),
                 K(database_name),
                 K(table_name));
      } else if (OB_FAIL(
                     sql_proxy->read(
                         res_,
                         sql_string.ptr()))) {
        LOG_WARN("failed to query dictionary table",
                 K(ret),
                 K(database_name),
                 K(table_name),
                 K(sql_string));
      }
    }

    if (OB_FAIL(ret)) {
      // Error has already been logged.
    } else if (OB_ISNULL(res_.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dictionary query result is null",
               K(ret));
    } else {
      ret = res_.get_result()->next();

      if (OB_ITER_END == ret) {
        // An empty dictionary table is valid and contains no words.
        ret = OB_SUCCESS;
        is_inited_ = true;
        has_current_row_ = false;
      } else if (OB_FAIL(ret)) {
        LOG_WARN("failed to read first dictionary word",
                 K(ret),
                 K(database_name),
                 K(table_name));
      } else {
        is_inited_ = true;
        has_current_row_ = true;
      }
    }
  }

  return ret;
}

void ObFTDictTableIter::reset()
{
  res_.close();
  is_inited_ = false;
  has_current_row_ = false;
}

} //  namespace storage
} //  namespace oceanbase
