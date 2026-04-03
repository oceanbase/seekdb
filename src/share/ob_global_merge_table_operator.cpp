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

#include "share/ob_global_merge_table_operator.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_zone_merge_info.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/storage/ob_merge_info_table_storage.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

// Static storage instance
ObMergeInfoTableStorage ObGlobalMergeTableOperator::storage_;

int ObGlobalMergeTableOperator::init()
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

int ObGlobalMergeTableOperator::load_global_merge_info(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    ObGlobalMergeInfo &info,
    const bool print_sql)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.get(tenant_id, info);
    if (OB_FAIL(ret) && OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to get global merge info from storage", K(ret), K(tenant_id));
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS; // Return empty info
    }
  }
  return ret;
}

int ObGlobalMergeTableOperator::insert_global_merge_info(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObGlobalMergeInfo &info)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else if (!info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(info));
  } else {
    ret = storage_.insert_or_update(info);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to insert or update global merge info", K(ret), K(tenant_id), K(info));
    }
  }
  return ret;
}

int ObGlobalMergeTableOperator::update_partial_global_merge_info(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObGlobalMergeInfo &info)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Use SQLite storage - partial update is same as full update for SQLite
    ret = storage_.insert_or_update(info);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to insert or update global merge info", K(ret), K(tenant_id), K(info));
    }
  }
  return ret;
}

int ObGlobalMergeTableOperator::check_scn_revert(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObGlobalMergeInfo &info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(info));
  } else {
    HEAP_VAR(ObGlobalMergeInfo, global_merge_info) {
      if (OB_FAIL(ObGlobalMergeTableOperator::load_global_merge_info(sql_client, tenant_id,
                                                                     global_merge_info))) {
        LOG_WARN("fail to load global merge info", KR(ret), K(tenant_id));
      } else {
        const ObMergeInfoItem *it = info.list_.get_first();
        while (OB_SUCC(ret) && (it != info.list_.get_header())) {
          if (NULL == it) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("null item", KR(ret), KP(it), K(tenant_id), K(info));
          } else {
            if (it->need_update_ && it->is_scn_) {
              if (0 == STRCMP(it->name_, "frozen_scn")) {
                if (it->get_scn() < global_merge_info.frozen_scn_.get_scn()) {
                  LOG_WARN("frozen_scn revert", K(tenant_id), "new_frozen_scn", it->get_scn(),
                    "origin_frozen_scn", global_merge_info.frozen_scn_.get_scn());
                }
              } else if (0 == STRCMP(it->name_, "global_broadcast_scn")) {
                if (it->get_scn() < global_merge_info.global_broadcast_scn_.get_scn()) {
                  LOG_WARN("global_broadcast_scn revert", K(tenant_id), "new_global_broadcast_scn",
                    it->get_scn(), "origin_global_broadcast_scn", global_merge_info.global_broadcast_scn_.get_scn());
                }
              } else if (0 == STRCMP(it->name_, "last_merged_scn")) {
                if (it->get_scn() < global_merge_info.last_merged_scn_.get_scn()) {
                  LOG_WARN("last_merged_scn revert", K(tenant_id), "new_last_merged_scn",
                    it->get_scn(), "origin_last_merged_scn", global_merge_info.last_merged_scn_.get_scn());
                }
              }
            }
            it = it->get_next();
          }
        }
      }
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
