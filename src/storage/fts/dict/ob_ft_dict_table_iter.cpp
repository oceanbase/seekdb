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
// A dict table name is a plain identifier, optionally qualified by one database
// name ("db.table"). It is concatenated into a SQL statement that runs over the
// internal high-privilege connection, so every character is validated against a
// whitelist instead of being escaped or quoted.
const int64_t MAX_DICT_TABLE_NAME_LENGTH = 256;

int check_dict_table_name(const ObString &table_name)
{
  int ret = OB_SUCCESS;
  const int64_t len = table_name.length();
  const char *ptr = table_name.ptr();
  int64_t dot_cnt = 0;
  int64_t cur_seg_len = 0;
  if (OB_ISNULL(ptr) || OB_UNLIKELY(len <= 0 || len > MAX_DICT_TABLE_NAME_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < len; ++i) {
    const char c = ptr[i];
    if ('.' == c) {
      if (OB_UNLIKELY(0 == cur_seg_len) || OB_UNLIKELY(++dot_cnt > 1)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        cur_seg_len = 0;
      }
    } else if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')
               || ('0' <= c && c <= '9') || '_' == c || '$' == c) {
      ++cur_seg_len;
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(0 == cur_seg_len)) {
    // empty name or a trailing dot
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}
} // anonymous namespace

ObFTDictTableIter::ObFTDictTableIter(ObISQLClient::ReadResult &result)
    : ObIFTDictIterator(), is_inited_(false), res_(result)
{
}

int ObFTDictTableIter::get_key(ObString &str)
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(res_.get_result()->get_varchar("word", str))) {
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
  } else if (OB_FAIL(res_.get_result()->next())) {
    if (OB_ITER_END != ret) {
    }
  }
  return ret;
}

int ObFTDictTableIter::init(const ObString &table_name)
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Inited twice.", K(ret));
  } else if (OB_FAIL(check_dict_table_name(table_name))) {
    LOG_WARN("illegal dict table name, only [A-Za-z0-9_$] and one optional database prefix are allowed",
             K(ret), K(table_name));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else {
    SMART_VAR(ObSqlString, sql_string)
    {
      if (OB_FAIL(sql_string.append("SELECT word FROM oceanbase."))) {
      } else if (OB_FAIL(sql_string.append(table_name))) {
      } else if (OB_FAIL(sql_string.append(" ORDER BY word"))) {
      } else if (OB_FAIL(sql_proxy->read(res_, sql_string.ptr()))) {
      }
    }

    if (OB_FAIL(ret)) {
      // already logged
    } else if (OB_ISNULL(res_.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(res_.get_result()->next())) {
      if (OB_ITER_END != ret) {
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
