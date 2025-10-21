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

#define USING_LOG_PREFIX SERVER		
#include "ob_table_connection_mgr.h"
#include "share/rc/ob_tenant_base.h"

using namespace oceanbase::table;
using namespace oceanbase::common;
using namespace oceanbase::lib;

int ObTableConnection::init(const common::ObAddr &addr, int64_t tenant_id, int64_t database_id, int64_t user_id)
{
  int ret = OB_SUCCESS;
  if (!addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table connection stat argument", K(ret), K(addr), K(tenant_id), K(database_id), K(user_id));
  } else {
    client_addr_ = addr;
    tenant_id_ = tenant_id;
    database_id_ = database_id;
    user_id_ = user_id;
    first_active_time_ = last_active_time_ = ObTimeUtility::fast_current_time();
  }
  return ret;
}

int64_t ObTableConnectionMgr::once_ = 0;
ObTableConnectionMgr *ObTableConnectionMgr::instance_ = NULL;

ObTableConnectionMgr::ObTableConnectionMgr()
    : connection_map_()
{};

ObTableConnectionMgr &ObTableConnectionMgr::get_instance()
{
  ObTableConnectionMgr *instance = NULL;
  while (OB_UNLIKELY(once_ < 2)) {
    if (ATOMIC_BCAS(&once_, 0, 1)) {
      instance = OB_NEW(ObTableConnectionMgr, ObModIds::TABLE_PROC);
      if (OB_LIKELY(OB_NOT_NULL(instance))) {
        if (common::OB_SUCCESS != instance->init()) {
          LOG_WARN_RET(OB_ERROR, "fail to init ObTableConnectionMgr instance");
          OB_DELETE(ObTableConnectionMgr, ObModIds::TABLE_PROC, instance);
          instance = NULL;
          ATOMIC_BCAS(&once_, 1, 0);
        } else {
          instance_ = instance;
          (void)ATOMIC_BCAS(&once_, 1, 2);
        }
      } else {
        (void)ATOMIC_BCAS(&once_, 1, 0);
      }
    }
  }
  return *(ObTableConnectionMgr *)instance_;
}

int ObTableConnectionMgr::init()
{
  int ret = OB_SUCCESS;
  ObMemAttr bucket_attr(OB_SERVER_TENANT_ID, "TableConnBucket");
  ObMemAttr node_attr(OB_SERVER_TENANT_ID, "TableConnNode");
  if (OB_FAIL(connection_map_.create(CONN_INFO_MAP_BUCKET_SIZE, bucket_attr, node_attr))) {
    LOG_WARN("fail to create table connection map", K(ret));
  } else {
    const ObMemAttr attr(OB_SERVER_TENANT_ID, "TbleConnMgr");
    if (OB_FAIL(allocator_.init(ObMallocAllocator::get_instance(), OB_MALLOC_MIDDLE_BLOCK_SIZE, attr))) {
      LOG_WARN("fail to init allocator", K(ret));
    }
  }
  return ret;
}

// update active time if connection exists
// or insert new connnection into connection mgr if not exists
int ObTableConnectionMgr::update_table_connection(const common::ObAddr &client_addr, int64_t tenant_id, int64_t database_id, int64_t user_id)
{
  int ret = OB_SUCCESS;

  if (!client_addr.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected invalid client address", K(ret), K(client_addr));
  } else {
    ObTableConnection *conn = nullptr;
    if (OB_FAIL(connection_map_.get_refactored(client_addr, conn))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("fail to get connection", K(ret), K(client_addr));
      } else { // not eixst, create
        conn = static_cast<ObTableConnection*>(allocator_.alloc(sizeof(ObTableConnection)));
        if (OB_ISNULL(conn)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc memory", K(ret), K(sizeof(ObTableConnection)));
        } else if (OB_FAIL(conn->init(client_addr, tenant_id, database_id, user_id))) {
          LOG_WARN("fail to init connection", K(ret), K(client_addr), K(tenant_id), K(database_id), K(user_id));
        } else if (OB_FAIL(connection_map_.set_refactored(client_addr, conn))) {
          if (OB_HASH_EXIST != ret) {
            LOG_WARN("fail to set_refactored", K(ret), K(client_addr));
          } else { // already set by other thread
            allocator_.free(conn);
            conn = nullptr;
          }
        }

        if (OB_FAIL(ret) && OB_NOT_NULL(conn)) {
          allocator_.free(conn);
          conn = nullptr;
        }
      }
    } else { // already exist, update it
      conn->set_last_active_time(ObTimeUtility::fast_current_time());
      conn->update_all_ids(tenant_id, database_id, user_id);
    }
  }

  LOG_DEBUG("update table connection", K(ret), K(client_addr), K(tenant_id), K(database_id),
            K(user_id), K(connection_map_.size()));
  return ret;
}

int ObTableConnectionMgr::update_table_connection(const rpc::ObRequest *req, int64_t tenant_id, int64_t database_id, int64_t user_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null request", K(ret));
  } else if (!common::is_valid_tenant_id(tenant_id)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected invalid tenant id", K(ret), K(tenant_id));
  } else {
    const ObAddr &client_addr = RPC_REQ_OP.get_peer(req);
    if (OB_FAIL(update_table_connection(client_addr, tenant_id, database_id, user_id))) {
      LOG_WARN("fail to update table connection", K(ret));
    } else {/* do nothing */}
  }
  return ret;
}


