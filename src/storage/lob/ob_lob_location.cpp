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

#include "ob_lob_location.h"
#include "observer/ob_server.h"
#include "sql/das/ob_das_utils.h"

namespace oceanbase
{
namespace storage
{

int ObLobLocationUtil::is_remote(ObLobAccessParam& param, bool& is_remote, common::ObAddr& dst_addr)
{
  int ret = OB_SUCCESS;
  ObLobLocatorV2 *lob_locator = param.lob_locator_;
  
  const ObAddr &self_addr = MYADDR;
  if (lob_locator == nullptr) {
    is_remote = false;
  } else if (!lob_locator->is_persist_lob()) {
    is_remote = false;
  } else if (param.from_rpc_ == true) {
    is_remote = false;
  } else {
    bool has_retry_info = false;
    ObMemLobExternHeader *extern_header = nullptr;
    if (OB_SUCC(lob_locator->get_extern_header(extern_header))) {
      has_retry_info = extern_header->flags_.has_retry_info_;
    }
    if (has_retry_info) {
      ObMemLobRetryInfo *retry_info = nullptr;
      if (OB_FAIL(lob_locator->get_retry_info(retry_info))) {
        LOG_WARN("fail to get retry info", K(ret), KPC(lob_locator));
      } else {
        dst_addr = retry_info->addr_;
      }
    } else {
      dst_addr = self_addr;
    }
    if (OB_SUCC(ret)) {
      // lob from other tenant also should read by rpc
      is_remote = (dst_addr != self_addr) || (false);
      if (param.from_rpc_ == true && is_remote) {
        ret = OB_NOT_MASTER;
        LOG_WARN("call from rpc, but remote again", K(ret), K(dst_addr), K(self_addr));
      }
    }
  }
  return ret;
}


int ObLobLocationUtil::lob_check_tablet_not_exist(ObLobAccessParam &param, uint64_t table_id)
{
  int ret = OB_SUCCESS;
  bool tablet_exist = false;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid schema service", KR(ret), K(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    // tenant could be deleted
    LOG_WARN("get tenant schema guard fail", KR(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, table_schema))) {
    LOG_WARN("failed to get table schema", KR(ret));
  } else if (OB_ISNULL(table_schema)) {
    //table could be dropped
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist, fast fail das task", K(table_id));
  } else if (OB_FAIL(table_schema->check_if_tablet_exists(param.tablet_id_, tablet_exist))) {
    LOG_WARN("check if tablet exists failed", K(ret), K(param), K(table_id));
  } else if (!tablet_exist) {
    ret = OB_PARTITION_NOT_EXIST;
    LOG_WARN("partition not exist, maybe dropped by DDL", K(ret), K(param), K(table_id));
  }
  return ret;
}

int ObLobLocationUtil::lob_refresh_location(ObLobAccessParam &param, int last_err, int retry_cnt)
{
  int ret = OB_SUCCESS;
  ObLobLocatorV2 *lob_locator = param.lob_locator_;
  const ObAddr &self_addr = MYADDR;
  ObMemLobExternHeader *extern_header = NULL;
  bool has_retry_info = false;
  if (OB_NOT_NULL(lob_locator) && OB_SUCC(lob_locator->get_extern_header(extern_header))) {
    has_retry_info = extern_header->flags_.has_retry_info_;
  }

  

  if (!has_retry_info) {
    // do check remote
    if (OB_FAIL(ObLobLocationUtil::get_ls_leader(param))) {
      LOG_WARN("fail to do check is remote", K(ret));
    }
  } else if (OB_FAIL(ObDASUtils::wait_das_retry(retry_cnt))) {
    LOG_WARN("wait das retry failed", K(ret), K(last_err), K(retry_cnt));
  } else {
    // do location refresh
    ObArenaAllocator tmp_allocator("LobRefLoc", OB_MALLOC_NORMAL_BLOCK_SIZE);
    sql::ObDASLocationRouter router(tmp_allocator);
    router.set_last_errno(last_err);
    sql::ObDASTableLocMeta loc_meta(tmp_allocator);
    loc_meta.ref_table_id_ = extern_header->table_id_;
    sql::ObDASTabletLoc tablet_loc;
    ObMemLobRetryInfo *retry_info = nullptr;
    ObMemLobLocationInfo *location_info = nullptr;
    if (last_err == OB_TABLET_NOT_EXIST && OB_FAIL(ObLobLocationUtil::lob_check_tablet_not_exist(param, extern_header->table_id_))) {
      LOG_WARN("fail to check tablet not exist", K(ret), K(extern_header->table_id_), K(last_err), K(retry_cnt));
    } else if (OB_FAIL(lob_locator->get_retry_info(retry_info))) {
      LOG_WARN("fail to get retry info", K(ret), KPC(lob_locator), K(last_err), K(retry_cnt));
    } else if (OB_FAIL(lob_locator->get_location_info(location_info))) {
      LOG_WARN("failed to get location info", K(ret), KPC(lob_locator), K(last_err), K(retry_cnt));
    } else if (OB_FALSE_IT(loc_meta.select_leader_ = retry_info->is_select_leader_)) {
       // use main tablet id to get location, for lob meta tablet is same location as main tablet
    } else if (OB_FAIL(router.get_tablet_loc(loc_meta, param.tablet_id_, tablet_loc))) {
      LOG_WARN("fail to refresh location", K(ret), K(last_err), K(retry_cnt));
    } else if (param.tablet_id_ != tablet_loc.tablet_id_ || location_info->tablet_id_ != tablet_loc.tablet_id_.id()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet id is changed", K(ret), K(tablet_loc), K(param), KPC(location_info));
    } else {
      param.addr_ = tablet_loc.server_;
    }
  }
  LOG_TRACE("[LOB RETRY] after do fresh location", K(ret), K(last_err), K(retry_cnt), K(has_retry_info), K(param));
  return ret;
}


int ObLobLocationUtil::get_ls_leader(ObLobAccessParam& param)
{
  param.addr_ = MYADDR;
  return OB_SUCCESS;
}


}  // end namespace storage
}  // end namespace oceanbase
