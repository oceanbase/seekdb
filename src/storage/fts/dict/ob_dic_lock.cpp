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

#define USING_LOG_PREFIX STORAGE_FTS

#include "src/storage/fts/dict/ob_dic_lock.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"

namespace oceanbase
{
namespace storage
{
int ObDicLock::lock_dic_tables_out_trans(const ObTenantDicLoader &dic_loader, 
    const transaction::tablelock::ObTableLockMode lock_mode, 
    const transaction::tablelock::ObTableLockOwnerID &lock_owner)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  const ObArray<ObTenantDicLoader::ObDicTableInfo> &dic_tables_info = dic_loader.get_dic_tables_info();
  if (OB_UNLIKELY(!true || dic_tables_info.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the tenant id or dic loader is invalid", K(ret), K(dic_tables_info));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
     LOG_WARN("failed to start trans", K(ret));
  } else if (OB_FAIL(lock_dic_tables_out_trans(dic_loader, lock_mode, lock_owner, trans))) {
    LOG_WARN("fail to lock dic tables", K(ret));
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_WARN("failed to commit trans", K(ret), K(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  return ret;
}

int ObDicLock::lock_dic_tables_out_trans(
    const common::ObIArray<uint64_t> &dict_table_ids,
    const transaction::tablelock::ObTableLockMode lock_mode,
    const transaction::tablelock::ObTableLockOwnerID &lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(dict_table_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary table ids are empty", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dict_table_ids.count(); ++i) {
      const uint64_t table_id = dict_table_ids.at(i);
      if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid dictionary table id", K(ret), K(table_id));
      } else if (OB_FAIL(do_table_lock(table_id, lock_mode, lock_owner,
                                       DEFAULT_TIMEOUT, true /* is_lock */, trans))) {
        LOG_WARN("failed to lock dictionary table", K(ret), K(table_id), K(lock_mode));
      }
    }
  }
  return ret;
}

int ObDicLock::lock_dic_tables_out_trans(
    const uint64_t tenant_id,
    const transaction::tablelock::ObTableLockMode lock_mode,
    const transaction::tablelock::ObTableLockOwnerID &lock_owner,
    ObMySQLTransaction &trans,
    const common::ObIArray<uint64_t> &dict_table_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", K(ret), K(tenant_id));
  } else {
    ret = lock_dic_tables_out_trans(dict_table_ids, lock_mode, lock_owner, trans);
  }
  return ret;
}

int ObDicLock::lock_dic_tables_out_trans(const ObTenantDicLoader &dic_loader, 
    const transaction::tablelock::ObTableLockMode lock_mode, 
    const transaction::tablelock::ObTableLockOwnerID &lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObArray<ObTenantDicLoader::ObDicTableInfo> &dic_tables_info = dic_loader.get_dic_tables_info();
  if (OB_UNLIKELY(!true || dic_tables_info.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the tenant id or dic loader is invalid", K(ret), K(dic_tables_info));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dic_tables_info.count(); ++i) {
      const uint64_t table_id = dic_tables_info.at(i).table_id_;
      if (OB_FAIL(do_table_lock(table_id, lock_mode, lock_owner, DEFAULT_TIMEOUT, true/*is_lock*/, trans))) {
          LOG_WARN("fail to do lock table", K(ret));
      }
    }
  }
  return ret;
}

int ObDicLock::unlock_dic_tables(const ObTenantDicLoader &dic_loader, 
    const transaction::tablelock::ObTableLockMode lock_mode, 
    const transaction::tablelock::ObTableLockOwnerID lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObArray<ObTenantDicLoader::ObDicTableInfo> &dic_tables_info = dic_loader.get_dic_tables_info();
  if (OB_UNLIKELY(!true || dic_tables_info.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the tenant id or dic loader is invalid", K(ret), K(dic_tables_info));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dic_tables_info.count(); ++i) {
      const uint64_t table_id = dic_tables_info.at(i).table_id_;
      if (OB_FAIL(do_table_lock(table_id, lock_mode, lock_owner, DEFAULT_TIMEOUT, false/*is_lock*/, trans))) {
        LOG_WARN("fail to do unlock table", K(ret));
      }
    }
  }
  return ret;
}

int ObDicLock::unlock_dict_tables(
    const common::ObIArray<uint64_t> &dict_table_ids,
    const transaction::tablelock::ObTableLockMode lock_mode,
    const transaction::tablelock::ObTableLockOwnerID &lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(dict_table_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary table ids are empty", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dict_table_ids.count(); ++i) {
      const uint64_t table_id = dict_table_ids.at(i);
      if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid dictionary table id", K(ret), K(table_id));
      } else if (OB_FAIL(do_table_lock(table_id, lock_mode, lock_owner,
                                       DEFAULT_TIMEOUT, false /* is_lock */, trans))) {
        LOG_WARN("failed to unlock dictionary table", K(ret), K(table_id), K(lock_mode));
      }
    }
  }
  return ret;
}

int ObDicLock::unlock_dict_tables(
    const uint64_t tenant_id,
    const common::ObIArray<uint64_t> &dict_table_ids,
    const transaction::tablelock::ObTableLockMode lock_mode,
    const transaction::tablelock::ObTableLockOwnerID &lock_owner,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", K(ret), K(tenant_id));
  } else {
    ret = unlock_dict_tables(dict_table_ids, lock_mode, lock_owner, trans);
  }
  return ret;
}

int ObDicLock::lock_dic_tables_in_trans(
    const ObTenantDicLoader &dic_loader, 
    const transaction::tablelock::ObTableLockMode lock_mode, 
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *conn = NULL;
  const ObArray<ObTenantDicLoader::ObDicTableInfo> &dic_tables_info = dic_loader.get_dic_tables_info();
  if (OB_UNLIKELY(!true || dic_tables_info.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the tenant id or dic loader is invalid", K(ret), K(dic_tables_info));
  } else if (OB_ISNULL(conn = dynamic_cast<observer::ObInnerSQLConnection *>(trans.get_connection()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn_ is NULL", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dic_tables_info.count(); ++i) {
      const uint64_t table_id = dic_tables_info.at(i).table_id_;
      LOG_INFO("lock table", KR(ret), K(table_id), KPC(conn));
      if (OB_FAIL(transaction::tablelock::ObInnerConnectionLockUtil::lock_table(table_id, lock_mode, DEFAULT_TIMEOUT, conn))) {
        LOG_WARN("lock dest table failed", KR(ret), K(table_id));
      }
    }
  }
  return ret;
}

int ObDicLock::lock_dic_tables_in_trans(
    const common::ObIArray<uint64_t> &dict_table_ids,
    const transaction::tablelock::ObTableLockMode lock_mode,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  observer::ObInnerSQLConnection *conn = nullptr;
  if (OB_UNLIKELY(dict_table_ids.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("dictionary table ids are empty", K(ret));
  } else if (OB_ISNULL(conn = dynamic_cast<observer::ObInnerSQLConnection *>(trans.get_connection()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner sql connection is null", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dict_table_ids.count(); ++i) {
      const uint64_t table_id = dict_table_ids.at(i);
      if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid dictionary table id", K(ret), K(table_id));
      } else if (OB_FAIL(transaction::tablelock::ObInnerConnectionLockUtil::lock_table(
                     table_id, lock_mode, DEFAULT_TIMEOUT, conn))) {
        LOG_WARN("failed to lock dictionary table", K(ret), K(table_id), K(lock_mode));
      }
    }
  }
  return ret;
}

int ObDicLock::lock_dic_tables_in_trans(
    const uint64_t tenant_id,
    const common::ObIArray<uint64_t> &dict_table_ids,
    const transaction::tablelock::ObTableLockMode lock_mode,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant id", K(ret), K(tenant_id));
  } else {
    ret = lock_dic_tables_in_trans(dict_table_ids, lock_mode, trans);
  }
  return ret;
}
} // end storage
} // end oceanbase
