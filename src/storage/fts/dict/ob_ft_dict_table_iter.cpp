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
    : ObIFTDictIterator(), is_inited_(false), iter_end_(false), res_(result)
{
}

int ObFTDictTableIter::get_key(ObString &str)
{
  int ret = OB_SUCCESS;
  if (!IS_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not inited.", K(ret));
  } else if (iter_end_) {
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
  } else if (iter_end_) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(res_.get_result()->next())) {
    if (OB_ITER_END == ret) {
      iter_end_ = true;
    } else {
      LOG_WARN("Failed to get next row", K(ret));
    }
  }
  return ret;
}

int ObFTDictTableIter::init(const ObFTDictDesc &desc)
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  const char *dot = desc.table_name_.find('.');

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Inited twice.", K(ret));
  } else if (OB_ISNULL(sql_proxy) || OB_ISNULL(dot)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid dictionary table descriptor", K(ret), K(desc), KP(sql_proxy));
  } else {
    const ObString database_name(static_cast<int32_t>(dot - desc.table_name_.ptr()),
                                 desc.table_name_.ptr());
    const ObString table_name(static_cast<int32_t>(
                                  desc.table_name_.length() - (dot - desc.table_name_.ptr()) - 1),
                              dot + 1);
    SMART_VAR(ObSqlString, sql_string)
    {
      if (desc.need_casedown_) {
        if (OB_FAIL(sql_string.append_fmt("SELECT DISTINCT LOWER(word) AS word FROM `%.*s`.`%.*s`",
                                         database_name.length(), database_name.ptr(),
                                         table_name.length(), table_name.ptr()))) {
          LOG_WARN("Failed to append lowercase dictionary sql", K(ret));
        }
      } else if (OB_FAIL(sql_string.append_fmt("SELECT word FROM `%.*s`.`%.*s`",
                                              database_name.length(), database_name.ptr(),
                                              table_name.length(), table_name.ptr()))) {
        LOG_WARN("Failed to append dictionary sql", K(ret));
      }
      if (OB_FAIL(ret)) {
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
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        iter_end_ = true;
        is_inited_ = true;
      } else {
        LOG_WARN("Failed to get next row", K(ret));
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
  iter_end_ = false;
}

} //  namespace storage
} //  namespace oceanbase
