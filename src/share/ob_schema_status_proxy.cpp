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

#include "ob_schema_status_proxy.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share;
using namespace share::schema;
namespace share
{
const char *ObSchemaStatusProxy::OB_ALL_SCHEMA_STATUS_TNAME = "__all_schema_status";
const char *ObSchemaStatusProxy::ROW_ID_CNAME = "id";
const char *ObSchemaStatusProxy::SNAPSHOT_TIMESTAMP_CNAME = "snapshot_timestamp";
const char *ObSchemaStatusProxy::READABLE_SCHEMA_VERSION_CNAME = "readable_schema_version";

//created_schema_version is no use in memory, so ignore created_schema_version_ change
int ObSchemaStatusUpdater::operator() (common::hash::HashMapPair<uint64_t, share::schema::ObRefreshSchemaStatus> &entry) {
  int ret = OB_SUCCESS;
  if (OB_INVALID_TIMESTAMP == schema_status_.snapshot_timestamp_) {
    if (schema_status_.snapshot_timestamp_ != entry.second.snapshot_timestamp_) {
      LOG_INFO("[SCHEMA_STATUS], reset schema status", "old_schema_status", entry.second,
               "new_schema_status", schema_status_);
    }
    entry.second = schema_status_;
  } else if (schema_status_.snapshot_timestamp_ >= entry.second.snapshot_timestamp_) {
    if (schema_status_.snapshot_timestamp_ != entry.second.snapshot_timestamp_) {
      LOG_INFO("[SCHEMA_STATUS] update schema status",
               "old_schema_status", entry.second,
               "new_schema_status", schema_status_);
    }
    entry.second = schema_status_;
  } else {
    LOG_INFO("[SCHEMA_STATUS] schema_status less than the old value, just ignore", K(entry), K(schema_status_));
  }
  return ret;
}

int ObSchemaStatusProxy::init()
{
  int ret = OB_SUCCESS;
  ObRefreshSchemaStatus schema_status;
  
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else {
    common::SpinWLockGuard guard(schema_status_cache_lock_);
    schema_status_cache_ = schema_status;
    is_inited_ = true;
  }
  return ret;
}

int ObSchemaStatusProxy::check_inner_stat()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  }
  return ret;
}


int ObSchemaStatusProxy::get_refresh_schema_status(
    ObRefreshSchemaStatus &refresh_schema_status)
{
  int ret = OB_SUCCESS;
  refresh_schema_status.reset();
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", K(ret));
  } else if (false) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else {
    common::SpinRLockGuard guard(schema_status_cache_lock_);
    refresh_schema_status = schema_status_cache_;
  }
  return ret;
}

int ObSchemaStatusProxy::get_refresh_schema_status(
    ObIArray<ObRefreshSchemaStatus> &refresh_schema_status_array)
{
  int ret = OB_SUCCESS;
  refresh_schema_status_array.reset();
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", K(ret));
  } else {
    common::SpinRLockGuard guard(schema_status_cache_lock_);
    if (OB_FAIL(refresh_schema_status_array.push_back(schema_status_cache_))) {
      LOG_WARN("fail to push back refresh schema status", K(ret), "schema_status", schema_status_cache_);
    }
  }
  return ret;
}

int ObSchemaStatusProxy::load_refresh_schema_status()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", K(ret));
  } else {
    ObCoreTableProxy core_table(OB_ALL_SCHEMA_STATUS_TNAME, sql_proxy_);
    if (OB_FAIL(core_table.load())) {
      LOG_WARN("fail to load core table", K(ret));
    } else {
      uint64_t row_id = OB_INVALID_TENANT_ID;
      int64_t snapshot_timestamp = OB_INVALID_TIMESTAMP;
      int64_t readable_schema_version = OB_INVALID_VERSION;
      while(OB_SUCC(ret)) {
        if (OB_FAIL(core_table.next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else {
            LOG_WARN("fail to next", K(ret));
          }
        } else if (OB_FAIL(core_table.get_uint(ROW_ID_CNAME, row_id))) {
          LOG_WARN("fail to get int", K(ret));
        } else if (OB_FAIL(core_table.get_int(SNAPSHOT_TIMESTAMP_CNAME, snapshot_timestamp))) {
          LOG_WARN("fail to get int", K(ret));
        } else if (OB_FAIL(core_table.get_int(READABLE_SCHEMA_VERSION_CNAME, readable_schema_version))) {
          LOG_WARN("fail to get int", K(ret));
        }
        if (OB_FAIL(ret)) {
        } else {
          ObRefreshSchemaStatus schema_status;
          
          schema_status.snapshot_timestamp_ = snapshot_timestamp;
          schema_status.readable_schema_version_ = readable_schema_version;
          // single-tenant: __all_schema_status has only the sys tenant row
          (void)(row_id);
          ObSchemaStatusUpdater updater(schema_status);
          common::SpinWLockGuard guard(schema_status_cache_lock_);
          common::hash::HashMapPair<uint64_t, ObRefreshSchemaStatus> entry;
          entry.first = 1UL;
          entry.second = schema_status_cache_;
          if (OB_FAIL(updater(entry))) {
            LOG_WARN("fail to update schema_status", K(ret), K(schema_status));
          } else {
            schema_status_cache_ = entry.second;
          }
        }
      }
    }
  }
  
  LOG_INFO("[SCHEMA_STATUS] load refreshed schema status", K(ret));
  return ret;
}

int ObSchemaStatusProxy::set_tenant_schema_status(
    const ObRefreshSchemaStatus &refresh_schema_status)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObDMLSqlSplicer dml;
  ObArray<ObCoreTableProxy::UpdateCell> cells;
  ObMySQLTransaction trans;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("check inner stat failed", K(ret));
  } else if (!refresh_schema_status.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("refresh schema status is invalid", K(ret), K(refresh_schema_status));
  } else if (OB_UNLIKELY(OB_INVALID_TIMESTAMP != refresh_schema_status.snapshot_timestamp_
                         && 0 != refresh_schema_status.snapshot_timestamp_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("snapshot timestamp must invalid or zero", KR(ret), K(refresh_schema_status));
  } else if (OB_FAIL(trans.start(&sql_proxy_))) {
    LOG_WARN("fail to start", K(ret));
  } else {
    ObCoreTableProxy kv(OB_ALL_SCHEMA_STATUS_TNAME, trans);
    if (OB_FAIL(dml.add_pk_column(ROW_ID_CNAME, static_cast<uint64_t>(1)))
        || OB_FAIL(dml.add_column(SNAPSHOT_TIMESTAMP_CNAME, refresh_schema_status.snapshot_timestamp_))
        || OB_FAIL(dml.add_column(READABLE_SCHEMA_VERSION_CNAME, refresh_schema_status.readable_schema_version_))) {
      LOG_WARN("fail to add column", KR(ret), K(refresh_schema_status));
    } else if (OB_FAIL(kv.load_for_update())) {
      LOG_WARN("fail to load for update", K(ret));
    } else if (OB_FAIL(dml.splice_core_cells(kv, cells))) {
      LOG_WARN("fail to splice core cells", K(ret));
    } else if (OB_FAIL(kv.replace_row(cells, affected_rows))) {
      LOG_WARN("fail to replace row", K(ret), K(refresh_schema_status), K(refresh_schema_status));
    } else if (affected_rows > 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("should update/insert 0 or 1 row", K(ret), K(affected_rows));
    }
  }
  if (trans.is_started()) {
    bool is_commit = (OB_SUCCESS == ret);
    int tmp_ret = trans.end(is_commit);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("fail to commit transaction", K(tmp_ret), K(ret), K(is_commit));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
      }
    }
  }
  
  ObSchemaStatusUpdater updater(refresh_schema_status);
  if (OB_FAIL(ret)) {
  } else {
    common::SpinWLockGuard guard(schema_status_cache_lock_);
    common::hash::HashMapPair<uint64_t, ObRefreshSchemaStatus> entry;
    entry.first = 1UL;
    entry.second = schema_status_cache_;
    if (OB_FAIL(updater(entry))) {
      LOG_WARN("fail to set schema_status", K(ret), K(refresh_schema_status));
    } else {
      schema_status_cache_ = entry.second;
      LOG_INFO("[SCHEMA_STATUS] set create status", K(refresh_schema_status));
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
