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

#include "share/tablet/ob_tablet_table_operator.h"
#include "share/resource_manager/ob_cgroup_ctrl.h"
#include "share/ob_server_struct.h"
namespace oceanbase
{
using namespace common;

namespace share
{
// Get shared storage from GCTX for static methods
static int get_shared_storage(ObTabletMetaTableStorage *&storage)
{
  int ret = OB_SUCCESS;
  storage = nullptr;

  // Try to get from GCTX (if available)
  if (GCTX.is_inited() && nullptr != GCTX.meta_db_pool_) {
    // Create a temporary ObTabletMetaTableStorage that uses the shared pool
    // For static methods, we need to create a temporary instance
    static ObTabletMetaTableStorage *g_static_storage = nullptr;
    if (OB_ISNULL(g_static_storage)) {
      void *buf = ob_malloc(sizeof(ObTabletMetaTableStorage), "TabletMetaStor");
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory for static storage", K(ret));
      } else {
        g_static_storage = new(buf) ObTabletMetaTableStorage();
        if (OB_FAIL(g_static_storage->init(GCTX.meta_db_pool_))) {
          LOG_WARN("failed to init static storage", K(ret));
          g_static_storage->~ObTabletMetaTableStorage();
          ob_free(buf);
          g_static_storage = nullptr;
        } else {
          storage = g_static_storage;
        }
      }
    } else {
      storage = g_static_storage;
    }
  } else {
    ret = OB_NOT_INIT;
    LOG_WARN("GCTX not inited or meta_db_storage not available", K(ret));
  }
  return ret;
}
ObTabletTableOperator::ObTabletTableOperator()
    : inited_(false), batch_size_(MAX_BATCH_COUNT), group_id_(0)
{
}

ObTabletTableOperator::~ObTabletTableOperator()
{
  reset();
}

int ObTabletTableOperator::init(share::ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_ISNULL(pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else {
    // Initialize storage with shared instance
    if (OB_FAIL(storage_.init(pool))) {
      LOG_WARN("failed to init storage", K(ret));
    } else {
      batch_size_ = MAX_BATCH_COUNT;
      group_id_ = 0; /*OBCG_DEFAULT*/
      inited_ = true;
      LOG_INFO("tablet table operator init success");
    }
  }
  return ret;
}

void ObTabletTableOperator::reset()
{
  inited_ = false;
  // storage_ is now a pointer to shared storage, don't destroy it
  storage_.~ObTabletMetaTableStorage();
  batch_size_ = 0;
}

int ObTabletTableOperator::get(
    const uint64_t tenant_id,
    const common::ObTabletID &tablet_id,
    const ObLSID &ls_id,
    const common::ObAddr &addr,
    ObTabletReplica &tablet_replica)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.get(tenant_id, tablet_id, ls_id, addr, tablet_replica);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to get tablet replica from storage", K(ret), K(tenant_id), K(tablet_id), K(ls_id), K(addr));
    }
  }
  return ret;
}

// will fill empty tablet_info when tablet not exist
int ObTabletTableOperator::get(
    const uint64_t tenant_id,
    const common::ObTabletID &tablet_id,
    const ObLSID &ls_id,
    ObTabletInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  tablet_info.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.get(tenant_id, tablet_id, ls_id, tablet_info);
    if (OB_FAIL(ret) && OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("fail to get tablet info from storage", KR(ret), K(tenant_id), K(tablet_id), K(ls_id));
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      // Return empty tablet_info when not exist
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_get_tablet_info(
    common::ObISQLClient *sql_proxy,
    const uint64_t tenant_id,
    const ObIArray<compaction::ObTabletCheckInfo> &tablet_ls_infos,
    const int32_t group_id,
    ObArrayWithMap<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  // Legacy method: ignore sql_proxy and use SQLite storage
  ObTabletMetaTableStorage *storage = nullptr;
  if (OB_FAIL(get_shared_storage(storage))) {
    LOG_WARN("failed to get shared storage", K(ret));
  } else {
    // Convert ObTabletCheckInfo to ObTabletLSPair
    ObSEArray<ObTabletLSPair, 64> tablet_ls_pairs;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ls_infos.count(); ++i) {
      const compaction::ObTabletCheckInfo &check_info = tablet_ls_infos.at(i);
      if (OB_FAIL(tablet_ls_pairs.push_back(ObTabletLSPair(check_info.get_tablet_id(), check_info.get_ls_id())))) {
        LOG_WARN("failed to push back tablet ls pair", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      ObSEArray<ObTabletInfo, 64> infos;
      if (OB_FAIL(storage->batch_get(tenant_id, tablet_ls_pairs, infos))) {
        LOG_WARN("failed to batch get from storage", K(ret));
      } else {
        // Convert to ObArrayWithMap
        for (int64_t i = 0; OB_SUCC(ret) && i < infos.count(); ++i) {
          if (OB_FAIL(tablet_infos.push_back(infos.at(i)))) {
            LOG_WARN("failed to push back tablet info", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_get(
    const uint64_t tenant_id,
    const ObIArray<ObTabletLSPair> &tablet_ls_pairs,
    ObIArray<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  const int64_t pairs_cnt = tablet_ls_pairs.count();
  hash::ObHashMap<ObTabletLSPair, bool> pairs_map;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(pairs_cnt < 1 || OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(pairs_cnt));
  }
  // Step 1: check duplicates by hash map
  if (FAILEDx(pairs_map.create(
      hash::cal_next_prime(pairs_cnt * 2),
      ObModIds::OB_HASH_BUCKET))) {
    LOG_WARN("fail to create pairs_map", KR(ret), K(pairs_cnt));
  } else {
    ARRAY_FOREACH_N(tablet_ls_pairs, idx, cnt) {
      // if same talet_id exist, return error
      if (OB_FAIL(pairs_map.set_refactored(tablet_ls_pairs.at(idx), false))) {
        if (OB_HASH_EXIST == ret) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("tablet_ls_pairs have duplicates", KR(ret), K(tablet_ls_pairs), K(idx));
        } else {
          LOG_WARN("fail to set refactored", KR(ret), K(tablet_ls_pairs), K(idx));
        }
      }
    } // end for
    if (OB_FAIL(ret)) {
    } else if (pairs_map.size() != pairs_cnt) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid pairs_map size", "size", pairs_map.size(), K(pairs_cnt));
    }
  }
  // Step 2: get from SQLite storage
  if (OB_SUCC(ret)) {
    if (OB_FAIL(storage_.batch_get(tenant_id, tablet_ls_pairs, tablet_infos))) {
      LOG_WARN("fail to batch get from storage", KR(ret), K(tenant_id), K(tablet_ls_pairs));
    }
  }
  // Step 3: check tablet_infos and push back empty tablet_info for tablets not exist
  if (OB_SUCC(ret) && (tablet_infos.count() < pairs_cnt)) {
    // check tablet infos and set flag in map
    int overwrite_flag = 1;
    ARRAY_FOREACH_N(tablet_infos, idx, cnt) {
      const ObTabletID &tablet_id = tablet_infos.at(idx).get_tablet_id();
      const ObLSID &ls_id = tablet_infos.at(idx).get_ls_id();
      if (OB_FAIL(pairs_map.set_refactored(ObTabletLSPair(tablet_id, ls_id), true, overwrite_flag))) {
        LOG_WARN("fail to set_fefactored", KR(ret), K(tablet_id), K(ls_id));
      }
    }
    // push back empty tablet_info
    if (OB_SUCC(ret)) {
      FOREACH_X(iter, pairs_map, OB_SUCC(ret)) {
        if (!iter->second) {
          ObArray<ObTabletReplica> replica; // empty replica
          ObTabletInfo tablet_info(
              tenant_id,
              iter->first.get_tablet_id(),
              iter->first.get_ls_id(),
              replica);
          if (OB_FAIL(tablet_infos.push_back(tablet_info))) {
            LOG_WARN("fail to push back tablet info", KR(ret), K(tablet_info));
          }
          LOG_TRACE("tablet not exist in meta table",
              KR(ret), K(tenant_id), "tablet_id", iter->first);
        }
      }
    }
  }
  return ret;
}

int ObTabletTableOperator::construct_tablet_infos(
    sqlclient::ObMySQLResult &res,
    std::function<int(ObTabletInfo&)> &&push_tablet)
{
  int ret = OB_SUCCESS;
  ObTabletInfo tablet_info;
  ObTabletReplica replica;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(res.next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("get next result failed", KR(ret));
      }
      break;
    } else {
      replica.reset();
      if (OB_FAIL(construct_tablet_replica_(res, replica))) {
        LOG_WARN("fail to construct tablet replica", KR(ret));
      } else if (OB_UNLIKELY(!replica.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("construct invalid replica", KR(ret), K(replica));
      } else if (tablet_info.is_self_replica(replica)) {
        if (OB_FAIL(tablet_info.add_replica(replica))) {
          LOG_WARN("fail to add replica", KR(ret), K(replica));
        }
      } else {
        if (tablet_info.is_valid()) {
          if (OB_FAIL(push_tablet(tablet_info))) {
            LOG_WARN("fail to push back", KR(ret), K(tablet_info));
          }
        }
        tablet_info.reset();
        if (FAILEDx(tablet_info.init_by_replica(replica))) {
          LOG_WARN("fail to init tablet_info by replica", KR(ret), K(replica));
        }
      }
    }
  } // end while
  if (OB_SUCC(ret) && tablet_info.is_valid()) {
    // last tablet info
    if (OB_FAIL(push_tablet(tablet_info))) {
      LOG_WARN("fail to push back", KR(ret), K(tablet_info));
    }
  }
  return ret;
}

int ObTabletTableOperator::construct_tablet_replica_(
    sqlclient::ObMySQLResult &res,
    ObTabletReplica &replica)
{
  int ret = OB_SUCCESS;
  int64_t tenant_id = OB_INVALID_TENANT_ID;
  int64_t tablet_id = ObTabletID::INVALID_TABLET_ID;
  common::ObAddr server;
  ObString ip;
  int64_t port = OB_INVALID_INDEX;
  int64_t ls_id = OB_INVALID_ID;
  uint64_t uint_compaction_scn = 0;
  int64_t compaction_scn = 0;
  int64_t data_size = 0;
  int64_t required_size = 0;
  uint64_t uint_report_scn = 0;
  int64_t status_in_table = 0;
  ObTabletReplica::ScnStatus status = ObTabletReplica::SCN_STATUS_IDLE;
  bool skip_null_error = false;
  bool skip_column_error = true;

  (void) GET_COL_IGNORE_NULL(res.get_int, "tenant_id", tenant_id);
  (void) GET_COL_IGNORE_NULL(res.get_int, "tablet_id", tablet_id);
  (void) GET_COL_IGNORE_NULL(res.get_int, "ls_id", ls_id);
  (void) GET_COL_IGNORE_NULL(res.get_varchar, "svr_ip", ip);
  (void) GET_COL_IGNORE_NULL(res.get_int, "svr_port", port);
  (void) GET_COL_IGNORE_NULL(res.get_uint, "compaction_scn", uint_compaction_scn);
  (void) GET_COL_IGNORE_NULL(res.get_int, "data_size", data_size);
  (void) GET_COL_IGNORE_NULL(res.get_int, "required_size", required_size);

  EXTRACT_UINT_FIELD_MYSQL_WITH_DEFAULT_VALUE(res, "report_scn", uint_report_scn, uint64_t, skip_null_error, skip_column_error, 0);
  EXTRACT_INT_FIELD_MYSQL_WITH_DEFAULT_VALUE(res, "status", status_in_table, int64_t, skip_null_error, skip_column_error, ObTabletReplica::SCN_STATUS_IDLE);

  status = (ObTabletReplica::ScnStatus)status_in_table;
  compaction_scn = static_cast<int64_t>(uint_compaction_scn);
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(!server.set_ip_addr(ip, static_cast<int32_t>(port)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid server address", KR(ret), K(ip), K(port));
  } else if (OB_UNLIKELY(!ObTabletReplica::is_status_valid(status))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid status", K(ret), K(status_in_table));
  } else if (OB_FAIL(
      replica.init(
          tenant_id,
          ObTabletID(tablet_id),
          share::ObLSID(ls_id),
          server,
          compaction_scn,
          data_size,
          required_size,
          (int64_t)uint_report_scn,
          status))) {
    LOG_WARN("fail to init replica", KR(ret),
        K(tenant_id), K(tablet_id), K(server), K(ls_id), K(data_size), K(required_size));
  }
  LOG_TRACE("construct tablet replica", KR(ret), K(replica));
  return ret;
}


int ObTabletTableOperator::batch_update(
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplica> &replicas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.batch_update(tenant_id, replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to batch update in storage", KR(ret), K(tenant_id));
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_update(
    ObSQLiteConnection *conn,
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplica> &replicas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else {
    ret = storage_.batch_update(conn, tenant_id, replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to batch update", K(ret), K(tenant_id));
    }
  }
  return ret;
}

int ObTabletTableOperator::range_get(
    const uint64_t tenant_id,
    const common::ObTabletID &start_tablet_id,
    const int64_t range_size,
    ObIArray<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(
      !is_valid_tenant_id(tenant_id)
      || range_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(start_tablet_id), K(range_size));
  } else {
    ret = storage_.range_get(tenant_id, start_tablet_id, range_size, tablet_infos);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to range get from storage", KR(ret), K(tenant_id), K(start_tablet_id), K(range_size));
    }
  }
  return ret;
}



int ObTabletTableOperator::batch_remove(
    ObSQLiteConnection *conn,
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplica> &replicas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else {
    ret = storage_.batch_remove(conn, tenant_id, replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to batch remove", K(ret), K(tenant_id));
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_remove(
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplica> &replicas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.batch_remove(tenant_id, replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to batch remove in storage", KR(ret), K(tenant_id));
    }
  }
  return ret;
}

int ObTabletTableOperator::remove_residual_tablet(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObAddr &server,
    const int64_t limit,
    int64_t &affected_rows)
{
  // Legacy method: ignore sql_client and use SQLite storage
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(
      !is_valid_tenant_id(tenant_id)
      || is_virtual_tenant_id(tenant_id)
      || !server.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(server));
  } else {
    ret = storage_.remove_residual_tablet(tenant_id, server, limit, affected_rows);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to remove residual tablet in storage", KR(ret), K(tenant_id), K(server));
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
