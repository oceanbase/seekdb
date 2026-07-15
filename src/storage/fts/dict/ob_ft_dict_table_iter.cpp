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
#include "share/inner_table/ob_inner_table_schema_constants.h"

#define USING_LOG_PREFIX STORAGE_FTS

namespace oceanbase
{
namespace storage
{
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

int ObFTDictTableIter::init(const ObFTDictDesc &dict_desc)
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  ObString table_name = dict_desc.name_;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Inited twice.", K(ret));
  } else {
    if (dict_desc.is_builtin()) {
      // 内置词典名称是逻辑名称，需映射到对应的 oceanbase inner table。
      switch (dict_desc.type_) {
        case ObFTDictType::DICT_IK_MAIN:
          table_name = ObString(share::OB_FT_DICT_IK_UTF8_TNAME);
          break;
        case ObFTDictType::DICT_IK_QUAN:
          table_name = ObString(share::OB_FT_QUANTIFIER_IK_UTF8_TNAME);
          break;
        case ObFTDictType::DICT_IK_STOP:
          table_name = ObString(share::OB_FT_STOPWORD_IK_UTF8_TNAME);
          break;
        default:
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("unsupported builtin dictionary type", K(ret), K(dict_desc.type_));
          break;
      }
    }
    SMART_VAR(ObSqlString, sql_string)
    {
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(sql_string.append("SELECT word FROM "))) {
        LOG_WARN("Failed to append sql", K(ret));
      } else if (dict_desc.is_builtin() && OB_FAIL(sql_string.append("oceanbase."))) {
        LOG_WARN("Failed to append builtin dictionary database", K(ret));
      } else if (OB_FAIL(sql_string.append(table_name))) {
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
