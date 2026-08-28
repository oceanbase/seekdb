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

#define USING_LOG_PREFIX COMMON_MYSQLP

#include "common/mysqlclient/ob_mysql_transaction.h"

namespace oceanbase
{
namespace common
{
using namespace sqlclient;

ObMySQLTransaction::ObMySQLTransaction(bool enable_query_stash)
    :ObSingleConnectionProxy(),
     start_time_(0),
     in_trans_(false),
     enable_query_stash_(enable_query_stash)
{
}

ObMySQLTransaction::~ObMySQLTransaction()
{
  int ret = OB_SUCCESS;
  if (in_trans_) {
    if (OB_FAIL(end(OB_SUCCESS == get_errno()))) {
    }
  }
  if (enable_query_stash_) {
    for (auto &it : query_stash_desc_) {
      ob_delete(it.second);
    }
    query_stash_desc_.destroy();
  }
}

int ObMySQLTransaction::start_transaction(
    bool with_snapshot)
{
  int ret = OB_SUCCESS;
  if (NULL == get_connection()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("conn_ is NULL", K(ret));
  } else if (OB_FAIL(get_connection()->start_transaction(with_snapshot))) {
  }
  return ret;
}

int ObMySQLTransaction::start(
    ObISQLClient *sql_client,
    bool with_snapshot/* = false*/,
    const int32_t group_id /* = 0*/)
{
  int ret = OB_SUCCESS;
  start_time_ = ::oceanbase::common::ObTimeUtility::current_time();
  if (OB_FAIL(connect(group_id, sql_client))) {
  } else if (enable_query_stash_ && OB_FAIL(query_stash_desc_.create(1024, "BucketQueryS", "NodeQueryS"))) {
    LOG_WARN("failed to init map", K(ret));
  } else {
    if (OB_FAIL(start_transaction(with_snapshot))) {
      set_errno(ret);
      close();
      LOG_ERROR("failed to start transaction", K(ret), K(with_snapshot));
    } else {
      in_trans_ = true;
    }
  }
  return ret;
}

int ObMySQLTransaction::start(ObISQLClient *proxy,
                              const int64_t &runtime_refreshed_schema_version,
                              bool with_snapshot)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSEDx(proxy, runtime_refreshed_schema_version, with_snapshot);
  return ret;
}

int ObMySQLTransaction::end_transaction(const bool commit)
{
  int ret = OB_SUCCESS;
  if (NULL != get_connection()) {
    if (commit) {
      ret = get_connection()->commit();
    } else {
      ret = get_connection()->rollback();
    }
    if (OB_SUCCESS == get_errno()) {
      set_errno(ret);
    }
  }
  return ret;
}

int ObMySQLTransaction::do_stash_query(int min_batch_cnt)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  for (hash::ObHashMap<const char*, ObSqlTransQueryStashDesc*>::iterator it = query_stash_desc_.begin();
      OB_SUCC(ret) && it != query_stash_desc_.end(); it++) {
    if (it->second->get_row_cnt() < min_batch_cnt) {
      continue;
    }
    const uint64_t start_time = ObTimeUtility::current_time();
    if (OB_FAIL(write(it->second->get_stash_query().ptr(), affected_rows))) {
    } else if (affected_rows != it->second->get_row_cnt()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("query_write", K(ret), K(affected_rows), "row_cnt", it->second->get_row_cnt(), "query", it->second->get_stash_query());
    } else {
      const uint64_t end_time = ObTimeUtility::current_time();
      it->second->reset();
      LOG_INFO("query_write succ", "table", it->first, "rows", affected_rows, "cost", end_time - start_time);
    }
  }
  return ret;
}

int ObMySQLTransaction::handle_trans_in_the_end(const int err_no)
{
  int ret = OB_SUCCESS;
  if (is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(end(OB_SUCCESS == err_no))) {
      LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == err_no, K(tmp_ret));
      ret = OB_SUCCESS == err_no ? tmp_ret : err_no;
    } else {
      ret = err_no;
    }
  } else {
    ret = err_no;
  }
  return ret;
}

int ObMySQLTransaction::get_stash_query(const char *table_name, ObSqlTransQueryStashDesc *&desc)
{
  int ret = OB_SUCCESS;
  ret = query_stash_desc_.get_refactored(table_name, desc);
  if (OB_FAIL(ret) && ret != OB_HASH_NOT_EXIST) {
    LOG_WARN("get_stash_query", K(ret), K(table_name));
  } else if (ret == OB_HASH_NOT_EXIST) {
    ret = OB_SUCCESS;
    void *ptr = ob_malloc(sizeof(ObSqlTransQueryStashDesc), "QueryStash");
    if (OB_ISNULL(ptr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("get_stash_query ob_malloc fail", K(ret));
    } else {
      desc = new(ptr) ObSqlTransQueryStashDesc();
      if (OB_FAIL(query_stash_desc_.set_refactored(table_name, desc))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (desc->get_stash_query().empty()) {
      
    } else {
      
    }
  }
  return ret;
}

int ObMySQLTransaction::end(const bool want_commit)
{
  bool commit = want_commit;
  int ret = OB_SUCCESS;
  if (in_trans_) {
    if (enable_query_stash_ && commit) {
      int tmp_ret = do_stash_query();
      if (tmp_ret != OB_SUCCESS) {
        LOG_WARN("do_stash_query fail", K(tmp_ret));
        commit = false;
      }
    }
    ret = end_transaction(commit);
    if (OB_FAIL(ret)) {
    } else {
    }
    in_trans_ = false;
  }
  close();
  return ret;
}

} // end namespace commmon
} // end namespace oceanbase
