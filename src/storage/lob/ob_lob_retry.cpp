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

#include "ob_lob_retry.h"
#include "share/interrupt/ob_global_interrupt_call.h"
#include "data_plane/access/ob_data_access_retry.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace storage
{

int ObLobRetryUtil::check_need_retry(ObLobAccessParam &param, const int error_code, const int retry_cnt, bool &need_retry)
{
  int ret = OB_SUCCESS;
  need_retry = false;
  if (!is_retryable_error(error_code)) {
  } else if (ObTimeUtility::current_time() > param.timeout_) {
    need_retry = false;
    ret = OB_TIMEOUT;
  } else if (IS_INTERRUPTED()) {
    need_retry = false;
    LOG_INFO("[LOB RETRY] Retry is interrupted by worker interrupt signal", KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry));
  } else {
    need_retry = true;
    if (OB_FAIL(prepare_retry(param, error_code, retry_cnt))) {
      need_retry = false;
    }
    LOG_INFO("retry again", K(ret), KR(ret), K(error_code), KR(error_code), K(retry_cnt), K(need_retry), K(param));
  }
  return ret;
}

int ObLobRetryUtil::check_tablet_exists(ObLobAccessParam &param, uint64_t table_id)
{
  int ret = OB_SUCCESS;
  bool tablet_exist = false;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObTableSchema *table_schema = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_table_schema(table_id, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    //table could be dropped
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table not exist, stop LOB retry", K(table_id));
  } else if (OB_FAIL(table_schema->check_if_tablet_exists(param.tablet_id_, tablet_exist))) {
  } else if (!tablet_exist) {
    ret = OB_PARTITION_NOT_EXIST;
  }
  return ret;
}

int ObLobRetryUtil::prepare_retry(ObLobAccessParam &param,
                                  int last_err,
                                  int retry_cnt)
{
  int ret = OB_SUCCESS;
  ObLobLocatorV2 *lob_locator = param.lob_locator_;
  ObMemLobExternHeader *extern_header = NULL;
  bool has_location_info = false;
  if (OB_NOT_NULL(lob_locator) && OB_SUCC(lob_locator->get_extern_header(extern_header))) {
    has_location_info = extern_header->flags_.has_location_info_;
  }

  if (!has_location_info) {
    // Locators without tablet metadata require no retry preparation.
  } else if (OB_FAIL(data_plane::ObDataAccessRetry::wait(retry_cnt))) {
  } else {
    ObMemLobLocationInfo *location_info = nullptr;
    if (last_err == OB_TABLET_NOT_EXIST && OB_FAIL(check_tablet_exists(param, extern_header->table_id_))) {
    } else if (OB_FAIL(lob_locator->get_location_info(location_info))) {
    } else if (location_info->tablet_id_ != param.tablet_id_.id()) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}


bool ObLobRetryUtil::is_retryable_error(int ret)
{
  return (ret == OB_REPLICA_NOT_READABLE) || 
         (ret == OB_NOT_MASTER) || 
         (ret == OB_NO_READABLE_REPLICA) || 
         (ret == OB_TABLET_NOT_EXIST) || 
         (ret == OB_LS_NOT_EXIST) ||
         (ret == OB_LS_OFFLINE);
}

} // storage
} // oceanbase
