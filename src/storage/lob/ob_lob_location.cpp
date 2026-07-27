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

int ObLobLocationUtil::lob_check_tablet_not_exist(ObLobAccessParam &param, uint64_t table_id)
{
  int ret = OB_SUCCESS;
  bool tablet_exist = false;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid schema service", KR(ret), K(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    // Runtime schema may not be ready during startup or shutdown.
    LOG_WARN("get runtime schema guard fail", KR(ret));
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

int ObLobLocationUtil::refresh_local_location(ObLobAccessParam &param,
                                              int last_err,
                                              int retry_cnt)
{
  int ret = OB_SUCCESS;
  ObLobLocatorV2 *lob_locator = param.lob_locator_;
  ObMemLobExternHeader *extern_header = NULL;
  bool has_retry_info = false;
  if (OB_NOT_NULL(lob_locator) && OB_SUCC(lob_locator->get_extern_header(extern_header))) {
    has_retry_info = extern_header->flags_.has_retry_info_;
  }



  if (!has_retry_info) {
    // Local access has no location route to refresh.
  } else if (OB_FAIL(ObDASUtils::wait_das_retry(retry_cnt))) {
    LOG_WARN("wait das retry failed", K(ret), K(last_err), K(retry_cnt));
  } else {
    ObMemLobLocationInfo *location_info = nullptr;
    if (last_err == OB_TABLET_NOT_EXIST && OB_FAIL(ObLobLocationUtil::lob_check_tablet_not_exist(param, extern_header->table_id_))) {
      LOG_WARN("fail to check tablet not exist", K(ret), K(extern_header->table_id_), K(last_err), K(retry_cnt));
    } else if (OB_FAIL(lob_locator->get_location_info(location_info))) {
      LOG_WARN("failed to get location info", K(ret), KPC(lob_locator), K(last_err), K(retry_cnt));
    } else if (location_info->tablet_id_ != param.tablet_id_.id()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet id is changed", K(ret), K(param), KPC(location_info));
    }
  }
  LOG_TRACE("[LOB RETRY] after do fresh location", K(ret), K(last_err), K(retry_cnt), K(has_retry_info), K(param));
  return ret;
}


}  // end namespace storage
}  // end namespace oceanbase
