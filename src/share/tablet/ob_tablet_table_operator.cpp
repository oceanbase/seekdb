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
      group_id_ = 0;
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

int ObTabletTableOperator::batch_get_tablet_info(
    common::ObISQLClient *sql_proxy,
    const ObIArray<compaction::ObTabletCheckInfo> &tablet_check_infos,
    const int32_t group_id,
    ObArrayWithMap<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  // Legacy method: ignore sql_proxy and use SQLite storage
  ObTabletMetaTableStorage *storage = nullptr;
  if (OB_FAIL(get_shared_storage(storage))) {
    LOG_WARN("failed to get shared storage", K(ret));
  } else {
    ObSEArray<ObTabletID, 64> tablet_ids;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_check_infos.count(); ++i) {
      const compaction::ObTabletCheckInfo &check_info = tablet_check_infos.at(i);
      if (OB_FAIL(tablet_ids.push_back(check_info.get_tablet_id()))) {
        LOG_WARN("failed to push back tablet id", K(ret), K(check_info));
      }
    }
    if (OB_SUCC(ret)) {
      ObSEArray<ObTabletInfo, 64> infos;
      if (OB_FAIL(storage->batch_get(tablet_ids, infos))) {
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
    const ObIArray<ObTabletID> &tablet_ids,
    ObIArray<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  const int64_t tablet_cnt = tablet_ids.count();
  hash::ObHashMap<ObTabletID, bool> tablet_map;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(tablet_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_cnt));
  }
  // Step 1: check duplicates by hash map
  if (FAILEDx(tablet_map.create(
      hash::cal_next_prime(tablet_cnt * 2),
      ObModIds::OB_HASH_BUCKET))) {
    LOG_WARN("fail to create tablet_map", KR(ret), K(tablet_cnt));
  } else {
    ARRAY_FOREACH_N(tablet_ids, idx, cnt) {
      // if same tablet_id exists, return error
      if (OB_FAIL(tablet_map.set_refactored(tablet_ids.at(idx), false))) {
        if (OB_HASH_EXIST == ret) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("tablet ids have duplicates", KR(ret), K(tablet_ids), K(idx));
        } else {
          LOG_WARN("fail to set refactored", KR(ret), K(tablet_ids), K(idx));
        }
      }
    } // end for
    if (OB_FAIL(ret)) {
    } else if (tablet_map.size() != tablet_cnt) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid tablet_map size", "size", tablet_map.size(), K(tablet_cnt));
    }
  }
  // Step 2: get from SQLite storage
  if (OB_SUCC(ret)) {
    if (OB_FAIL(storage_.batch_get(tablet_ids, tablet_infos))) {
      LOG_WARN("fail to batch get from storage", KR(ret), K(tablet_ids));
    }
  }
  // Step 3: check tablet_infos and push back empty tablet_info for tablets not exist
  if (OB_SUCC(ret) && (tablet_infos.count() < tablet_cnt)) {
    // check tablet infos and set flag in map
    int overwrite_flag = 1;
    ARRAY_FOREACH_N(tablet_infos, idx, cnt) {
      const ObTabletID &tablet_id = tablet_infos.at(idx).get_tablet_id();
      if (OB_FAIL(tablet_map.set_refactored(tablet_id, true, overwrite_flag))) {
        LOG_WARN("fail to set_fefactored", KR(ret), K(tablet_id));
      }
    }
    // push back empty tablet_info
    if (OB_SUCC(ret)) {
      FOREACH_X(iter, tablet_map, OB_SUCC(ret)) {
        if (!iter->second) {
          ObTabletInfo tablet_info(iter->first);
          if (OB_FAIL(tablet_infos.push_back(tablet_info))) {
            LOG_WARN("fail to push back tablet info", KR(ret), K(tablet_info));
          }
          LOG_TRACE("tablet not exist in meta table",
              KR(ret), K(iter->first));
        }
      }
    }
  }
  return ret;
}


int ObTabletTableOperator::batch_update(
    const ObIArray<ObTabletReplica> &replicas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.batch_update(replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to batch update in storage", KR(ret));
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_update(
    ObSQLiteConnection *conn,
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
    ret = storage_.batch_update(conn, replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to batch update", K(ret));
    }
  }
  return ret;
}

int ObTabletTableOperator::range_get(const common::ObTabletID &start_tablet_id,
    const int64_t range_size,
    ObIArray<ObTabletInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(range_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_tablet_id), K(range_size));
  } else {
    ret = storage_.range_get(start_tablet_id, range_size, tablet_infos);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to range get from storage", KR(ret), K(start_tablet_id), K(range_size));
    }
  }
  return ret;
}



int ObTabletTableOperator::batch_remove(
    ObSQLiteConnection *conn,
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
    ret = storage_.batch_remove(conn, replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to batch remove", K(ret));
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_remove(
    const ObIArray<ObTabletReplica> &replicas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.batch_remove(replicas);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to batch remove in storage", KR(ret));
    }
  }
  return ret;
}

int ObTabletTableOperator::remove_residual_tablet(
    ObISQLClient &sql_client,
    const ObAddr &server,
    const int64_t limit,
    int64_t &affected_rows)
{
  // Legacy method: ignore sql_client and use SQLite storage
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!server.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(server));
  } else {
    ret = storage_.remove_residual_tablet(server, limit, affected_rows);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to remove residual tablet in storage", KR(ret), K(server));
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
