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

#define USING_LOG_PREFIX STORAGE

#include "share/tablet/ob_tablet_mapping_operator.h"
#include "storage/ddl/ob_ddl_lock.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "storage/tablelock/ob_lock_utils.h"
#include "lib/string/ob_sql_string.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "common/mysqlclient/ob_mysql_result.h"

using namespace oceanbase::transaction::tablelock;
using oceanbase::share::schema::ObTableSchema;
using oceanbase::common::sqlclient::ObISQLConnection;

namespace oceanbase
{
namespace storage
{

bool ObDDLLock::need_lock(const ObTableSchema &table_schema)
{
  const int64_t table_id = table_schema.get_table_id();
  return table_schema.is_user_table()
      || table_schema.is_tmp_table()
      || ObInnerTableLockUtil::in_inner_table_lock_white_list(table_id);
};

int ObDDLLock::lock_for_add_drop_index_in_trans(
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_schema,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const uint64_t index_table_id = index_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  ObISQLConnection *iconn = nullptr;
  if (data_table_schema.is_user_hidden_table()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lock for rebuild hidden table index", K(ret));
  } else if (!need_lock(data_table_schema)) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
    LOG_WARN("failed to get data tablet ids", K(ret));
  } else if (index_schema.is_storage_local_index_table()) {
    if (OB_FAIL(lock_table_lock_in_trans(data_table_id, data_tablet_ids, ROW_SHARE, timeout_us, trans))) {
      LOG_WARN("failed to lock data table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(data_table_id, ROW_EXCLUSIVE, timeout_us, trans))) {
      LOG_WARN("failed to lock data table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::lock_tablets_in_trans(data_tablet_ids, ROW_EXCLUSIVE, timeout_us, trans))) {
      LOG_WARN("failed to lock data table tablet", K(ret));
    }
  } else {
    if (OB_FAIL(lock_table_lock_in_trans(data_table_id, data_tablet_ids, ROW_SHARE, timeout_us, trans))) {
      LOG_WARN("failed to lock data table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(data_table_id, ROW_EXCLUSIVE, timeout_us, trans))) {
      LOG_WARN("failed to lock data table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(index_table_id, EXCLUSIVE, timeout_us, trans))) {
      LOG_WARN("failed to lock index table", K(ret));
    }
  }
  return ret;
}

int ObDDLLock::lock_for_add_drop_index(
    const ObTableSchema &data_table_schema,
    const ObIArray<ObTabletID> *inc_data_tablet_ids,
    const ObIArray<ObTabletID> *del_data_tablet_ids,
    const ObTableSchema &index_schema,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const uint64_t index_table_id = index_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  ObISQLConnection *iconn = nullptr;
  if (OB_UNLIKELY(data_table_schema.is_user_hidden_table() || data_table_id != index_schema.get_data_table_id())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lock for rebuild hidden table index", K(ret), K(data_table_id), K(index_table_id), K(index_schema.get_data_table_id()));
  } else if (!need_lock(data_table_schema)) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else {
    if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
      LOG_WARN("failed to get data tablet ids", K(ret));
    } else if (nullptr != del_data_tablet_ids) {
      ObArray<ObTabletID> tmp_tablet_ids;
      if (OB_FAIL(get_difference(data_tablet_ids, *del_data_tablet_ids, tmp_tablet_ids))) {
        LOG_WARN("failed to get diff tablet ids", K(ret));
      } else if (OB_FAIL(data_tablet_ids.assign(tmp_tablet_ids))) {
        LOG_WARN("failed to assign data tablet ids", K(ret));
      }
    }
    if (OB_SUCC(ret) && nullptr != inc_data_tablet_ids) {
      if (OB_FAIL(append(data_tablet_ids, *inc_data_tablet_ids))) {
        LOG_WARN("failed to append inc data tablet ids", K(ret));
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObOnlineDDLLock::lock_table(data_table_id, ROW_EXCLUSIVE, lock_owner, timeout_us, trans))) {
      LOG_WARN("failed to lock data table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::lock_tablets(data_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans))) {
      LOG_WARN("failed to lock data table tablet", K(ret));
    } else if (OB_FAIL(do_table_lock(data_table_id, data_tablet_ids, ROW_SHARE, lock_owner, timeout_us, true/*is_lock*/, trans))) {
      LOG_WARN("failed to lock data tablet", K(ret));
    } else if (!index_schema.is_storage_local_index_table()) {
      if (OB_FAIL(ObOnlineDDLLock::lock_table(index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
        LOG_WARN("failed to lock index table", K(ret));
      }
    }
  }
  return ret;
}

int ObDDLLock::unlock_for_add_drop_index(
    const ObTableSchema &data_table_schema,
    const uint64_t index_table_id,
    const bool is_global_index,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  bool some_lock_not_exist = false;
  if (!need_lock(data_table_schema) || data_table_schema.is_user_hidden_table()) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
    LOG_WARN("failed to get data tablet ids", K(ret));
  } else if (OB_FAIL(do_table_lock(data_table_id, data_tablet_ids, ROW_SHARE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
    LOG_WARN("failed to unlock data tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_tablets(data_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock data tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_table(data_table_id, ROW_EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock data table", K(ret));
  } else if (is_global_index) {
    if (OB_FAIL(ObOnlineDDLLock::unlock_table(index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
      LOG_WARN("failed to unlock index table", K(ret));
    }
  }
  return ret;
}

int ObDDLLock::lock_for_rebuild_index(
    const share::schema::ObTableSchema &data_table_schema,
    const uint64_t old_index_table_id,
    const uint64_t new_index_table_id,
    const bool is_global_index,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  ObISQLConnection *iconn = nullptr;
  if (data_table_schema.is_user_hidden_table()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lock for rebuild hidden table index", K(ret));
  } else if (!need_lock(data_table_schema)) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
    LOG_WARN("failed to get data tablet ids", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_table(data_table_id, ROW_EXCLUSIVE, lock_owner, timeout_us, trans))) {
    LOG_WARN("failed to lock data table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_tablets(data_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans))) {
    LOG_WARN("failed to lock data table tablet", K(ret));
  } else if (OB_FAIL(do_table_lock(data_table_id, data_tablet_ids, ROW_SHARE, lock_owner, timeout_us, true/*is_lock*/, trans))) {
    LOG_WARN("failed to lock data tablet", K(ret));
  } else if (is_global_index) {
    if (OB_FAIL(ObOnlineDDLLock::lock_table(old_index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
      LOG_WARN("failed to lock index table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::lock_table(new_index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
      LOG_WARN("failed to lock index table", K(ret));
    }
  }
  return ret;
}

int ObDDLLock::unlock_for_rebuild_index(
    const share::schema::ObTableSchema &data_table_schema,
    const uint64_t old_index_table_id,
    const uint64_t new_index_table_id,
    const bool is_global_index,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  bool some_lock_not_exist = false;
  if (!need_lock(data_table_schema) || data_table_schema.is_user_hidden_table()) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
    LOG_WARN("failed to get data tablet ids", K(ret));
  } else if (OB_FAIL(do_table_lock(data_table_id, data_tablet_ids, ROW_SHARE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
    LOG_WARN("failed to unlock data tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_tablets(data_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock data tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_table(data_table_id, ROW_EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock data table", K(ret));
  } else if (is_global_index) {
    if (OB_FAIL(ObOnlineDDLLock::unlock_table(old_index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
      LOG_WARN("failed to unlock index table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::unlock_table(new_index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
      LOG_WARN("failed to unlock index table", K(ret));
    }
  }
  return ret;
}

int ObDDLLock::lock_for_modify_auto_part_size_in_trans(const uint64_t data_table_id,
    const ObIArray<uint64_t> &global_index_table_ids,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObArray<ObTabletID> no_tablet_ids;
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(data_table_id, EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock data table", K(ret));
  } else if (OB_FAIL(lock_table_lock_in_trans(data_table_id, no_tablet_ids, ROW_SHARE, timeout_us, trans))) {
    LOG_WARN("failed to lock tablet", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < global_index_table_ids.count(); i++) {
      const uint64_t table_id = global_index_table_ids.at(i);
      if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(table_id, EXCLUSIVE, timeout_us, trans))) {
        LOG_WARN("failed to lock data table tablets", K(ret));
      }
    }
  }
  return ret;
}

int ObDDLLock::lock_for_modify_truncate_info_in_trans(const uint64_t global_index_table_id,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(global_index_table_id, EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock online ddl table", K(ret));
  }
  return ret;
}

int ObDDLLock::lock_for_add_lob_in_trans(
    const ObTableSchema &data_table_schema,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  if (!need_lock(data_table_schema)) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else if (OB_FAIL(lock_table_lock_in_trans(data_table_id, data_tablet_ids, ROW_EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(data_table_id, ROW_EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock data table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_tablets_in_trans(data_tablet_ids, ROW_EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock data table tablets", K(ret));
  }
  return ret;
}

int ObDDLLock::lock_for_online_drop_column_in_trans(const ObTableSchema &table_schema, ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> tablet_ids;
  if (!need_lock(table_schema)) {
    LOG_INFO("skip ddl lock for non-user table", K(table_schema.get_table_id()));
  } else if (OB_FAIL(table_schema.get_tablet_ids(tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else if (OB_FAIL(lock_table_lock_in_trans(table_id, tablet_ids, ROW_EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(table_id, EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock ddl table", K(ret));
  }
  ret = share::ObDDLUtil::is_table_lock_retry_ret_code(ret) ? OB_EAGAIN : ret;
  return ret;
}

int ObDDLLock::lock_for_drop_lob(
    const ObTableSchema &data_table_schema,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  if (!need_lock(data_table_schema)) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else if (OB_FAIL(do_table_lock(data_table_id, data_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, true/*is_lock*/, trans))) {
    LOG_WARN("failed to lock data tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_table(data_table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
    LOG_WARN("failed to lock data table", K(ret));
  }
  ret = share::ObDDLUtil::is_table_lock_retry_ret_code(ret) ? OB_EAGAIN : ret;
  return ret;
}

int ObDDLLock::unlock_for_drop_lob(
    const ObTableSchema &data_table_schema,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  bool some_lock_not_exist = false;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  if (!need_lock(data_table_schema)) {
    LOG_INFO("skip ddl lock", K(data_table_id));
  } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else if (OB_FAIL(do_table_lock(data_table_id, data_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
    LOG_WARN("failed to unlock data tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_table(data_table_id, EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock data table", K(ret));
  }
  ret = share::ObDDLUtil::is_table_lock_retry_ret_code(ret) ? OB_EAGAIN : ret;
  return ret;
}

int ObDDLLock::lock_for_add_partition_in_trans(
    const ObTableSchema &table_schema,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const ObArray<ObTabletID> no_tablet_ids;
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  if (table_schema.is_global_index_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not support to add partition to global index", K(ret));
  } else if (need_lock(table_schema)) {
    if (OB_FAIL(lock_table_lock_in_trans(table_id, no_tablet_ids, ROW_EXCLUSIVE, timeout_us, trans))) {
      LOG_WARN("failed to lock data table", K(ret));
    } else if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(table_id, SHARE, timeout_us, trans))) {
      LOG_WARN("failed to lock ddl table", K(ret));
    }
  } else {
    LOG_INFO("skip ddl lock", K(ret), K(table_id));
  }
  return ret;
}

int ObDDLLock::lock_table_and_global_indexes_for_fork(
    share::schema::ObSchemaGetterGuard &schema_guard,
    const share::schema::ObTableSchema &table_schema,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> tablet_ids;
  ObSEArray<const share::schema::ObSimpleTableSchemaV2 *, 4> index_schemas;

  if (!need_lock(table_schema)) {
    LOG_INFO("skip ddl lock", K(table_id));
  } else if (OB_FAIL(table_schema.get_tablet_ids(tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else if (OB_FAIL(do_table_lock(table_id, tablet_ids, ROW_SHARE, lock_owner, timeout_us, true/*is_lock*/, trans))) {
    LOG_WARN("failed to lock data table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_table(table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
    LOG_WARN("failed to lock data table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_tablets(tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans))) {
    LOG_WARN("failed to lock data table tablet", K(ret));
  } else if (OB_FAIL(schema_guard.get_index_schemas_with_data_table_id(table_id, index_schemas))) {
    LOG_WARN("failed to get index schemas", K(ret), K(table_id));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < index_schemas.count(); ++i) {
      const share::schema::ObSimpleTableSchemaV2 *index_schema = index_schemas.at(i);
      if (OB_ISNULL(index_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index schema is null", K(ret), K(i));
      } else if (index_schema->is_global_index_table()) {
        const uint64_t index_table_id = index_schema->get_table_id();
        ObSEArray<ObTabletID, 1> index_tablet_ids;
        if (OB_FAIL(index_schema->get_tablet_ids(index_tablet_ids))) {
          LOG_WARN("failed to get index tablet ids", K(ret), K(index_table_id));
        } else if (OB_FAIL(ObOnlineDDLLock::lock_table(index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
          LOG_WARN("failed to lock index table", K(ret), K(index_table_id));
        } else if (OB_FAIL(ObOnlineDDLLock::lock_tablets(index_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans))) {
          LOG_WARN("failed to lock index table tablet", K(ret), K(index_table_id));
        }
        // Note: For global index tables, we only use OnlineDDL locks, not table locks via do_table_lock,
        // to avoid "lock table not allowed now" errors that may occur during Fork Table operations.
      }
    }
  }
  return ret;
}

int ObDDLLock::unlock_table_and_global_indexes_for_fork(
    share::schema::ObSchemaGetterGuard &schema_guard,
    const share::schema::ObTableSchema &table_schema,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> tablet_ids;
  ObSEArray<const share::schema::ObSimpleTableSchemaV2 *, 4> index_schemas;
  bool some_lock_not_exist = false;

  if (!need_lock(table_schema) || table_schema.is_user_hidden_table()) {
    LOG_INFO("skip ddl unlock", K(table_id));
  } else if (OB_FAIL(table_schema.get_tablet_ids(tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_tablets(tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock tablet", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_table(table_id, EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock table", K(ret));
  } else if (OB_FAIL(do_table_lock(table_id, tablet_ids, ROW_SHARE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
    LOG_WARN("failed to unlock tablet", K(ret));
  } else if (OB_FAIL(schema_guard.get_index_schemas_with_data_table_id(table_id, index_schemas))) {
    LOG_WARN("failed to get index schemas", K(ret), K(table_id));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < index_schemas.count(); ++i) {
      const share::schema::ObSimpleTableSchemaV2 *index_schema = index_schemas.at(i);
      if (OB_ISNULL(index_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index schema is null", K(ret), K(i));
      } else if (index_schema->is_global_index_table()) {
        const uint64_t index_table_id = index_schema->get_table_id();
        ObSEArray<ObTabletID, 1> index_tablet_ids;
        if (OB_FAIL(index_schema->get_tablet_ids(index_tablet_ids))) {
          LOG_WARN("failed to get index tablet ids", K(ret), K(index_table_id));
        } else if (OB_FAIL(ObOnlineDDLLock::unlock_tablets(index_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
          LOG_WARN("failed to unlock index tablet", K(ret), K(index_table_id));
        } else if (OB_FAIL(ObOnlineDDLLock::unlock_table(index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
          LOG_WARN("failed to unlock index table", K(ret), K(index_table_id));
        } else if (OB_FAIL(do_table_lock(OB_INVALID_ID, index_tablet_ids, ROW_SHARE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
          LOG_WARN("failed to unlock index tablet table lock", K(ret), K(index_table_id));
        }
        // Note: For global index tables, we unlock both OnlineDDL locks and table locks.
        // The table lock unlock is needed for dst tables which have table locks via lock_dst_table_and_global_indexes_for_fork.
        // For src tables without table locks, the unlock will succeed gracefully (OB_OBJ_LOCK_NOT_EXIST is handled).
      }
    }
  }
  return ret;
}

int ObDDLLock::lock_dst_table_and_global_indexes_for_fork(
    const ObIArray<share::schema::ObTableSchema> &dst_table_schemas,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> data_tablet_ids;
  ObSEArray<ObTabletID, 4> all_tablet_ids;

  if (dst_table_schemas.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dst_table_schemas is empty", K(ret));
  } else {
    // First element is the data table
    const share::schema::ObTableSchema &data_table_schema = dst_table_schemas.at(0);
    
    const uint64_t data_table_id = data_table_schema.get_table_id();

    if (OB_INVALID_ID == data_table_id) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid data_table_id", K(ret), K(data_table_id));
    } else if (!need_lock(data_table_schema)) {
      LOG_INFO("skip ddl lock", K(data_table_id));
    } else if (OB_FAIL(data_table_schema.get_tablet_ids(data_tablet_ids))) {
      LOG_WARN("failed to get data tablet ids", K(ret));
    } else if (OB_FAIL(append(all_tablet_ids, data_tablet_ids))) {
      LOG_WARN("failed to append data tablet ids", K(ret));
    } else {
      // Lock OnlineDDL table lock for data table
      if (OB_FAIL(ObOnlineDDLLock::lock_table(data_table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
        LOG_WARN("failed to lock data table", K(ret));
      } else {
        // Find and lock global index tables from dst_table_schemas (starting from index 1)
        for (int64_t i = 1; OB_SUCC(ret) && i < dst_table_schemas.count(); ++i) {
          const share::schema::ObTableSchema &table_schema = dst_table_schemas.at(i);
          if (table_schema.is_global_index_table() && table_schema.get_data_table_id() == data_table_id) {
            const uint64_t index_table_id = table_schema.get_table_id();
            ObSEArray<ObTabletID, 1> index_tablet_ids;
            if (OB_FAIL(table_schema.get_tablet_ids(index_tablet_ids))) {
              LOG_WARN("failed to get index tablet ids", K(ret), K(index_table_id));
            } else if (OB_FAIL(ObOnlineDDLLock::lock_table(index_table_id, EXCLUSIVE, lock_owner, timeout_us, trans))) {
              LOG_WARN("failed to lock index table", K(ret), K(index_table_id));
            } else if (OB_FAIL(append(all_tablet_ids, index_tablet_ids))) {
              LOG_WARN("failed to append index tablet ids", K(ret));
            }
          }
        }

        // Lock all tablets (data table + global indexes) OnlineDDL locks in one request
        if (OB_SUCC(ret)) {
          if (OB_FAIL(ObOnlineDDLLock::lock_tablets(all_tablet_ids, ROW_EXCLUSIVE, lock_owner, timeout_us, trans))) {
            LOG_WARN("failed to lock all tablets", K(ret));
          } else {
            // Lock all tablets (data table + global indexes) in one request using ObLockAloneTabletRequest
            ObLockAloneTabletRequest arg;
            arg.lock_mode_ = ROW_SHARE;
            arg.op_type_ = ObTableLockOpType::OUT_TRANS_LOCK;
            arg.owner_id_ = lock_owner;
            arg.timeout_us_ = timeout_us;
            ObISQLConnection *iconn = nullptr;
            if (OB_ISNULL(iconn = trans.get_connection())) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid conn", K(ret));
            } else if (OB_FAIL(append(arg.tablet_ids_, all_tablet_ids))) {
              LOG_WARN("failed to push back", K(ret));
            } else if (OB_FAIL(ObInnerConnectionLockUtil::lock_tablet(arg, iconn))) {
              LOG_WARN("failed to lock tablet", K(ret), K(arg));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLLock::lock_for_fork_table(
    share::schema::ObSchemaGetterGuard &schema_guard,
    const share::schema::ObTableSchema &src_table_schema,
    const ObIArray<share::schema::ObTableSchema> &dst_table_schemas,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  // lock src table and src global indexes
  if (OB_FAIL(lock_table_and_global_indexes_for_fork(schema_guard, src_table_schema, lock_owner, trans))) {
    LOG_WARN("failed to lock src table and global indexes", K(ret));
  } else if (OB_FAIL(lock_dst_table_and_global_indexes_for_fork(dst_table_schemas, lock_owner, trans))) {
    LOG_WARN("failed to lock dst table and global indexes", K(ret));
  }

  return ret;
}

int ObDDLLock::check_has_dependent_task(const int64_t current_task_id,
    const uint64_t table_id,
    ObMySQLTransaction &trans,
    bool &has_dependent_task)
{
  int ret = OB_SUCCESS;
  has_dependent_task = false;
  ObSqlString sql_string;
  ObISQLClient::ReadResult res;
  sqlclient::ObMySQLResult *result = NULL;

  if (OB_FAIL(sql_string.assign_fmt("SELECT EXISTS(SELECT 1 FROM %s WHERE task_id != %ld AND ddl_type = %d "
                                    "AND (object_id = %lu OR target_object_id = %lu)) as has",
      share::OB_ALL_DDL_TASK_STATUS_TNAME, current_task_id, share::ObDDLType::DDL_FORK_TABLE, table_id, table_id))) {
    LOG_WARN("assign sql string failed", K(ret));
  } else {
    ObISQLConnection *iconn = trans.get_connection();
    if (OB_ISNULL(iconn)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid connection", K(ret));
    } else if (OB_FAIL(ObInnerConnectionLockUtil::execute_read_sql(iconn, sql_string, res))) {
      LOG_WARN("query ddl task record failed", K(ret), K(sql_string));
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get sql result", K(ret), KP(result));
    } else if (OB_FAIL(result->next())) {
      LOG_WARN("result next failed", K(ret));
    } else {
      EXTRACT_BOOL_FIELD_MYSQL(*result, "has", has_dependent_task);
    }
  }

  return ret;
}

int ObDDLLock::unlock_for_fork_table(
    share::schema::ObSchemaGetterGuard &schema_guard,
    const share::schema::ObTableSchema &src_table_schema,
    const share::schema::ObTableSchema &dst_table_schema,
    const int64_t task_id,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t src_table_id = src_table_schema.get_table_id();
  const uint64_t dst_table_id = dst_table_schema.get_table_id();

  if (task_id <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid task id", K(ret), K(task_id));
  } else {
    // Check and unlock src table and src global indexes
    bool has_src_dependent_task = false;
    if (OB_FAIL(check_has_dependent_task(task_id, src_table_id, trans, has_src_dependent_task))) {
      LOG_WARN("failed to check src dependent task", K(ret));
    } else if (has_src_dependent_task) {
      LOG_INFO("skip unlock for src table due to dependent tasks", K(task_id), K(src_table_id));
    } else if (OB_FAIL(unlock_table_and_global_indexes_for_fork(schema_guard, src_table_schema, lock_owner, trans))) {
      LOG_WARN("failed to unlock src table and global indexes", K(ret));
    }

    // Check and unlock dst table and dst global indexes
    bool has_dst_dependent_task = false;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(check_has_dependent_task(task_id, dst_table_id, trans, has_dst_dependent_task))) {
      LOG_WARN("failed to check dst dependent task", K(ret));
    } else if (has_dst_dependent_task) {
      LOG_INFO("skip unlock for dst table due to dependent tasks", K(task_id), K(dst_table_id));
    } else if (OB_FAIL(unlock_table_and_global_indexes_for_fork(schema_guard, dst_table_schema, lock_owner, trans))) {
      LOG_WARN("failed to unlock dst table and global indexes", K(ret));
    }
  }
  return ret;
}

int ObDDLLock::lock_for_common_ddl_in_trans(
    const ObTableSchema &table_schema,
    const bool require_strict_binary_format,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObSEArray<ObTabletID, 1> tablet_ids;
  if (!need_lock(table_schema)) {
    LOG_INFO("skip ddl lock for non-user table", K(table_schema.get_table_id()));
  } else if (OB_FAIL(table_schema.get_tablet_ids(tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else if (OB_FAIL(lock_table_lock_in_trans(table_id, tablet_ids, ROW_EXCLUSIVE, timeout_us, trans))) {
    LOG_WARN("failed to lock table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_table_in_trans(table_id, require_strict_binary_format ? SHARE : ROW_SHARE, timeout_us, trans))) {
    LOG_WARN("failed to lock ddl table", K(ret));
  }
  return ret;
}

int ObDDLLock::lock_for_common_ddl(
    const ObTableSchema &table_schema,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  if (!need_lock(table_schema)) {
    LOG_INFO("skip ddl lock for non-user table", K(table_schema.get_table_id()));
  } else if (OB_FAIL(do_table_lock(table_id, ROW_EXCLUSIVE, lock_owner, timeout_us, true/*is_lock*/, trans))) {
    LOG_WARN("failed to lock table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::lock_table(table_id, ROW_SHARE, lock_owner, timeout_us, trans))) {
    LOG_WARN("failed to lock ddl table", K(ret));
  }
  return ret;
}

int ObDDLLock::unlock_for_common_ddl(
    const ObTableSchema &table_schema,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  bool some_lock_not_exist = false;
  if (!need_lock(table_schema)) {
    LOG_INFO("skip ddl lock for non-user table", K(table_schema.get_table_id()));
  } else if (OB_FAIL(do_table_lock(table_id, ROW_EXCLUSIVE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
    LOG_WARN("failed to unlock table", K(ret));
  } else if (OB_FAIL(ObOnlineDDLLock::unlock_table(table_id, ROW_SHARE, lock_owner, timeout_us, trans, some_lock_not_exist))) {
    LOG_WARN("failed to unlock ddl table", K(ret));
  }
  return ret;
}

int ObDDLLock::lock_for_offline_ddl(
    const ObTableSchema &table_schema,
    const ObTableSchema *hidden_table_schema_to_check_bind,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  if (!need_lock(table_schema)) {
    LOG_INFO("skip ddl lock for non-user table", K(table_id));
  } else if (OB_FAIL(do_table_lock(table_id, EXCLUSIVE, lock_owner, timeout_us, true/*is_lock*/, trans))) {
    LOG_WARN("failed to lock table lock", K(ret));
  }
  UNUSED(hidden_table_schema_to_check_bind);
  return ret;
}

int ObDDLLock::unlock_for_offline_ddl(const uint64_t table_id,
    const ObIArray<ObTabletID> *hidden_tablet_ids_alone,
    const ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  if (OB_FAIL(do_table_lock(table_id, EXCLUSIVE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
    LOG_WARN("failed to lock table lock", K(ret));
  } else if (nullptr != hidden_tablet_ids_alone) {
    if (OB_FAIL(do_table_lock(OB_INVALID_ID/*table_id*/, *hidden_tablet_ids_alone, EXCLUSIVE, lock_owner, timeout_us, false/*is_lock*/, trans))) {
      LOG_WARN("failed to unlock tablets", K(ret), KPC(hidden_tablet_ids_alone), K(lock_owner));
    }
  }
  return ret;
}

int ObDDLLock::lock_table_in_trans(
    const ObTableSchema &table_schema,
    const ObTableLockMode lock_mode,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = table_schema.get_table_id();
  const int64_t timeout_us = DEFAULT_TIMEOUT;
  ObISQLConnection *iconn = nullptr;
  if (!need_lock(table_schema)) {
    LOG_INFO("skip ddl lock for non-user table", K(table_id));
  } else if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else if (OB_FAIL(ObInnerConnectionLockUtil::lock_table(table_id, lock_mode, timeout_us, iconn))) {
    LOG_WARN("failed to lock table", K(ret), K(table_id));
  }
  return ret;
}

// TODO(lihongqin.lhq): batch rpc
int ObDDLLock::lock_table_lock_in_trans(const uint64_t table_id,
    const ObIArray<ObTabletID> &tablet_ids,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *iconn = nullptr;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else if (tablet_ids.empty()) {
    if (OB_FAIL(ObInnerConnectionLockUtil::lock_table(table_id, lock_mode, timeout_us, iconn))) {
      LOG_WARN("failed to lock table", K(ret));
    }
  } else {
    if (OB_FAIL(
          ObInnerConnectionLockUtil::lock_tablet(table_id, tablet_ids, lock_mode, timeout_us, iconn))) {
      LOG_WARN("failed to lock tablets", K(ret), K(table_id), K(tablet_ids), K(lock_mode), K(timeout_us));
    }
  }
  return ret;
}

int ObDDLLock::do_table_lock(const uint64_t table_id,
    const ObTableLockMode lock_mode,
    const ObTableLockOwnerID lock_owner,
    const int64_t timeout_us,
    const bool is_lock,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObTableLockOpType op_type = is_lock ? ObTableLockOpType::OUT_TRANS_LOCK : ObTableLockOpType::OUT_TRANS_UNLOCK;
  ObISQLConnection *iconn = nullptr;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else {
    if (is_lock) {
      ObLockTableRequest arg;
      arg.table_id_ = table_id;
      arg.owner_id_ = lock_owner;
      arg.lock_mode_ = lock_mode;
      arg.op_type_ = op_type;
      arg.timeout_us_ = timeout_us;
      if (OB_FAIL(ObInnerConnectionLockUtil::lock_table(arg, iconn))) {
        LOG_WARN("failed to lock table", K(ret));
      }
    } else {
      ObUnLockTableRequest arg;
      arg.table_id_ = table_id;
      arg.owner_id_ = lock_owner;
      arg.lock_mode_ = lock_mode;
      arg.op_type_ = op_type;
      arg.timeout_us_ = timeout_us;
      if (OB_FAIL(ObInnerConnectionLockUtil::unlock_table(arg, iconn))) {
        if (OB_OBJ_LOCK_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          LOG_INFO("table lock already unlocked", K(ret), K(arg));
        } else {
          LOG_WARN("failed to unlock table", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObDDLLock::do_table_lock(const uint64_t table_id,
    const ObIArray<ObTabletID> &tablet_ids,
    const ObTableLockMode lock_mode,
    const ObTableLockOwnerID lock_owner,
    const int64_t timeout_us,
    const bool is_lock,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObTableLockOpType op_type = is_lock ? ObTableLockOpType::OUT_TRANS_LOCK : ObTableLockOpType::OUT_TRANS_UNLOCK;
  ObISQLConnection *iconn = nullptr;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else if (OB_UNLIKELY(tablet_ids.empty()
      || (lock_mode != ROW_SHARE && lock_mode != ROW_EXCLUSIVE && lock_mode != SHARE && lock_mode != EXCLUSIVE))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(table_id), K(tablet_ids.count()), K(lock_mode));
  }

  if (OB_SUCC(ret) && OB_INVALID_ID != table_id) {
    const ObTableLockMode table_level_lock_mode = lock_mode == ROW_SHARE || lock_mode == SHARE ? ROW_SHARE : ROW_EXCLUSIVE;
    if (OB_FAIL(do_table_lock(table_id, table_level_lock_mode, lock_owner, timeout_us, is_lock, trans))) {
      LOG_WARN("failed to lock table", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    ObLockAloneTabletRequest lock_arg;
    ObUnLockAloneTabletRequest unlock_arg;
    ObLockAloneTabletRequest &arg = is_lock ? lock_arg : unlock_arg;
    arg.owner_id_ = lock_owner;
    arg.lock_mode_ = lock_mode;
    arg.op_type_ = op_type;
    arg.timeout_us_ = timeout_us;
    if (OB_FAIL(append(arg.tablet_ids_, tablet_ids))) {
      LOG_WARN("failed to append tablet ids", K(ret));
    } else if (is_lock && OB_FAIL(ObInnerConnectionLockUtil::lock_tablet(lock_arg, iconn))) {
      LOG_WARN("failed to lock tablet", K(ret), K(lock_arg));
    } else if (!is_lock && OB_FAIL(ObInnerConnectionLockUtil::unlock_tablet(unlock_arg, iconn))) {
      if (OB_OBJ_LOCK_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("table lock already unlocked", K(ret), K(unlock_arg));
      } else {
        LOG_WARN("failed to unlock tablet", K(ret));
      }
    }
  }
  return ret;
}


int ObDDLLock::replace_tablet_lock(const uint64_t table_id,
    const ObIArray<ObTabletID> &tablet_ids,
    const transaction::tablelock::ObTableLockMode old_lock_mode,
    const transaction::tablelock::ObTableLockOwnerID old_lock_owner,
    const transaction::tablelock::ObTableLockMode new_lock_mode,
    const transaction::tablelock::ObTableLockOwnerID new_lock_owner,
    const int64_t timeout_us,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObTableLockOpType op_type = ObTableLockOpType::OUT_TRANS_UNLOCK;
  ObISQLConnection *iconn = nullptr;
  ObUnLockAloneTabletRequest unlock_arg;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else if (OB_UNLIKELY(OB_INVALID_ID == table_id || tablet_ids.count() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(table_id), K(tablet_ids.count()));
  } else if (OB_FAIL(get_unlock_alone_tablet_request_arg(old_lock_mode, old_lock_owner, timeout_us, tablet_ids, unlock_arg))) {
    LOG_WARN("fail to get unlock alone tablet request arg", K(ret), K(old_lock_mode), K(old_lock_owner), K(timeout_us), K(tablet_ids));
  } else {
    ObReplaceLockRequest replace_req;
    replace_req.new_lock_mode_ = new_lock_mode;
    replace_req.new_lock_owner_ = new_lock_owner;
    replace_req.unlock_req_ = &unlock_arg;
    if (OB_FAIL(ObInnerConnectionLockUtil::replace_lock(replace_req, iconn))) {
      LOG_WARN("failed to replace lock", K(ret), K(replace_req));
    }
  }
  return ret;
}

int ObDDLLock::get_unlock_alone_tablet_request_arg(const transaction::tablelock::ObTableLockMode lock_mode,
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    const int64_t timeout_us,
    const ObIArray<ObTabletID> &tablet_ids,
    transaction::tablelock::ObUnLockAloneTabletRequest &unlock_arg)
{
  int ret = OB_SUCCESS;
  unlock_arg.reset();
  const ObTableLockOpType op_type = ObTableLockOpType::OUT_TRANS_UNLOCK;
  if (OB_UNLIKELY(tablet_ids.count() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_ids.count()));
  } else {
    unlock_arg.owner_id_ = lock_owner;
    unlock_arg.lock_mode_ = lock_mode;
    unlock_arg.op_type_ = op_type;
    unlock_arg.timeout_us_ = timeout_us;
    if (OB_FAIL(append(unlock_arg.tablet_ids_, tablet_ids))) {
      LOG_WARN("failed to append tablet ids", K(ret));
    }
  }
  return ret;
}




int ObOnlineDDLLock::lock_table_in_trans(
    const uint64_t table_id,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *iconn = nullptr;
  ObLockObjRequest arg;
  arg.obj_type_ = ObLockOBJType::OBJ_TYPE_ONLINE_DDL_TABLE;
  arg.obj_id_ = table_id;
  arg.timeout_us_ = timeout_us;
  arg.op_type_ = ObTableLockOpType::IN_TRANS_COMMON_LOCK;
  arg.lock_mode_ = lock_mode;
  arg.owner_id_.set_default();
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else if (OB_FAIL(ObInnerConnectionLockUtil::lock_obj(arg, iconn))) {
    LOG_WARN("failed to lock online ddl table in trans", K(ret), K(arg));
  }
  return ret;
}

// TODO(lihongqin.lhq): batch rpc
int ObOnlineDDLLock::lock_tablets_in_trans(const ObIArray<ObTabletID> &tablet_ids,
    const ObTableLockMode lock_mode,
    const int64_t timeout_us,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *iconn = nullptr;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else {
    ObLockObjsRequest arg;
    ObLockID lock_id;
    arg.timeout_us_ = timeout_us;
    arg.op_type_ = ObTableLockOpType::IN_TRANS_COMMON_LOCK;
    arg.lock_mode_ = lock_mode;
    arg.owner_id_.set_default();
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); i++) {
      if (OB_FAIL(lock_id.set(ObLockOBJType::OBJ_TYPE_ONLINE_DDL_TABLET, tablet_ids.at(i).id()))) {
        LOG_WARN("set lock id failed", K(ret), K(i), K(tablet_ids));
      } else if (OB_FAIL(arg.objs_.push_back(lock_id))) {
        LOG_WARN("add lock id failed", K(ret), K(lock_id));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObInnerConnectionLockUtil::lock_obj(arg, iconn))) {
      LOG_WARN("failed to lock online ddl tablets in trans", K(ret), K(arg));
    }
  }
  return ret;
}

int ObOnlineDDLLock::lock_table(
    const uint64_t table_id,
    const ObTableLockMode lock_mode,
    const ObTableLockOwnerID lock_owner,
    const int64_t timeout_us,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *iconn = nullptr;
  ObLockObjRequest arg;
  arg.obj_type_ = ObLockOBJType::OBJ_TYPE_ONLINE_DDL_TABLE;
  arg.obj_id_ = table_id;
  arg.timeout_us_ = timeout_us;
  arg.op_type_ = ObTableLockOpType::OUT_TRANS_LOCK;
  arg.lock_mode_ = lock_mode;
  arg.owner_id_ = lock_owner;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else if (OB_FAIL(ObInnerConnectionLockUtil::lock_obj(arg, iconn))) {
    LOG_WARN("failed to lock online ddl table", K(ret));
  }
  return ret;
}

// TODO(lihongqin.lhq): batch rpc
int ObOnlineDDLLock::lock_tablets(
    const ObIArray<ObTabletID> &tablet_ids,
    const ObTableLockMode lock_mode,
    const ObTableLockOwnerID lock_owner,
    const int64_t timeout_us,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *iconn = nullptr;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else {
    ObLockObjsRequest arg;
    ObLockID lock_id;
    arg.timeout_us_ = timeout_us;
    arg.op_type_ = ObTableLockOpType::OUT_TRANS_LOCK;
    arg.lock_mode_ = lock_mode;
    arg.owner_id_ = lock_owner;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); i++) {
      if (OB_FAIL(lock_id.set(ObLockOBJType::OBJ_TYPE_ONLINE_DDL_TABLET, tablet_ids.at(i).id()))) {
        LOG_WARN("set lock id failed", K(ret), K(i), K(tablet_ids));
      } else if (OB_FAIL(arg.objs_.push_back(lock_id))) {
        LOG_WARN("add lock id failed", K(ret), K(lock_id));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObInnerConnectionLockUtil::lock_obj(arg, iconn))) {
      LOG_WARN("failed to lock online ddl tablets", K(ret), K(arg));
    }
  }
  return ret;
}

int ObOnlineDDLLock::unlock_table(
    const uint64_t table_id,
    const ObTableLockMode lock_mode,
    const ObTableLockOwnerID lock_owner,
    const int64_t timeout_us,
    ObMySQLTransaction &trans,
    bool &some_lock_not_exist)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *iconn = nullptr;
  ObUnLockObjRequest arg;
  arg.obj_type_ = ObLockOBJType::OBJ_TYPE_ONLINE_DDL_TABLE;
  arg.obj_id_ = table_id;
  arg.timeout_us_ = timeout_us;
  arg.op_type_ = ObTableLockOpType::OUT_TRANS_UNLOCK;
  arg.lock_mode_ = lock_mode;
  arg.owner_id_ = lock_owner;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else if (OB_FAIL(ObInnerConnectionLockUtil::unlock_obj(arg, iconn))) {
    if (OB_OBJ_LOCK_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      some_lock_not_exist = true;
      LOG_INFO("online ddl table already unlocked", K(ret), K(arg));
    } else {
      LOG_WARN("failed to lock online ddl table", K(ret), K(arg));
    }
  }
  return ret;
}

// TODO(lihongqin.lhq): batch rpc
int ObOnlineDDLLock::unlock_tablets(const ObIArray<ObTabletID> &tablet_ids,
    const ObTableLockMode lock_mode,
    const ObTableLockOwnerID lock_owner,
    const int64_t timeout_us,
    ObMySQLTransaction &trans,
    bool &some_lock_not_exist)
{
  int ret = OB_SUCCESS;
  ObISQLConnection *iconn = nullptr;
  if (OB_ISNULL(iconn = trans.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid conn", K(ret));
  } else {
    ObUnLockObjsRequest arg;
    ObLockID lock_id;
    arg.timeout_us_ = timeout_us;
    arg.op_type_ = ObTableLockOpType::OUT_TRANS_UNLOCK;
    arg.lock_mode_ = lock_mode;
    arg.owner_id_ = lock_owner;

    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); i++) {
      if (OB_FAIL(lock_id.set(ObLockOBJType::OBJ_TYPE_ONLINE_DDL_TABLET, tablet_ids.at(i).id()))) {
        LOG_WARN("set lock id failed", K(ret), K(i), K(tablet_ids));
      } else if (OB_FAIL(arg.objs_.push_back(lock_id))) {
        LOG_WARN("add lock id failed", K(ret), K(lock_id));
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(ObInnerConnectionLockUtil::unlock_obj(arg, iconn))) {
      LOG_WARN("meet fail during unlock online ddl tablets", K(ret), K(arg));
      if (OB_OBJ_LOCK_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        some_lock_not_exist = true;
        LOG_WARN("online ddl tablet already unlocked", K(ret));
      } else {
        LOG_WARN("failed to unlock online ddl tablet", K(ret));
      }
    }
  }

  return ret;
}

}  // namespace storage
}
