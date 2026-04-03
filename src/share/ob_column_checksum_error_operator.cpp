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

#define USING_LOG_PREFIX SHARE

#include "share/ob_column_checksum_error_operator.h"
#include "share/ob_server_struct.h"
#include "share/storage/ob_column_checksum_error_info_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "share/storage/ob_sqlite_connection_pool.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

// Static storage instance
ObColumnChecksumErrorInfoTableStorage ObColumnChecksumErrorOperator::storage_;

int ObColumnChecksumErrorOperator::init()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ not initialized", K(ret));
  } else if (OB_FAIL(storage_.init(GCTX.meta_db_pool_))) {
    LOG_WARN("failed to init storage", K(ret));
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////

bool ObColumnChecksumErrorInfo::is_valid() const
{
  return (tenant_id_ != OB_INVALID_TENANT_ID) && (frozen_scn_.is_valid())
         && (data_table_id_ != OB_INVALID_ID) && (index_table_id_ != OB_INVALID_ID);
}


///////////////////////////////////////////////////////////////////////////////

int ObColumnChecksumErrorOperator::insert_column_checksum_err_info(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObColumnChecksumErrorInfo &info)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.insert(info);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to insert column checksum error info", K(ret), K(tenant_id), K(info));
    }
  }
  return ret;
}

int ObColumnChecksumErrorOperator::delete_column_checksum_err_info(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const SCN &min_frozen_scn)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else if (OB_UNLIKELY((!is_valid_tenant_id(tenant_id))) || (!min_frozen_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(min_frozen_scn));
  } else {
    ret = storage_.delete_expired(tenant_id, min_frozen_scn, INT64_MAX);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to delete expired column checksum error info", K(ret), K(tenant_id), K(min_frozen_scn));
    }
  }
  return ret;
}

int ObColumnChecksumErrorOperator::delete_column_checksum_err_info_by_scn(
    common::ObISQLClient &sql_client, 
    const uint64_t tenant_id,
    const int64_t compaction_scn)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else if (OB_UNLIKELY((!is_valid_tenant_id(tenant_id))) || compaction_scn <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(compaction_scn));
  } else {
    // Use SQLite storage - delete by exact frozen_scn
    const char *delete_sql =
      "DELETE FROM __all_column_checksum_error_info "
      "WHERE frozen_scn = ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(compaction_scn);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(delete_sql, binder))) {
      LOG_WARN("failed to execute delete", K(ret), K(tenant_id), K(compaction_scn));
    }
  }
  return ret;
}

int ObColumnChecksumErrorOperator::check_exist_ckm_error_table(const uint64_t tenant_id, const int64_t compaction_scn, bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  if (OB_UNLIKELY(0 == tenant_id || compaction_scn <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(compaction_scn));
  } else if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    const char *select_sql =
      "SELECT COUNT(*) as cnt FROM __all_column_checksum_error_info "
      "WHERE frozen_scn = ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(compaction_scn);
      return OB_SUCCESS;
    };

    int64_t count = 0;
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      count = reader.get_int64();
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      LOG_WARN("failed to query", K(ret), K(tenant_id), K(compaction_scn));
    } else if (count > 0) {
      exist = true;
      LOG_INFO("exist ckm error info", K(count), K(tenant_id), K(compaction_scn));
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
