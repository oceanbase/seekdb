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

#include "share/ob_zone_merge_table_operator.h"

#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_zone_merge_info.h"
#include "share/storage/ob_zone_merge_info_table_storage.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/string/ob_sql_string.h"
#include "observer/ob_server_struct.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

// Static storage instance
ObZoneMergeInfoTableStorage ObZoneMergeTableOperator::storage_;

int ObZoneMergeTableOperator::init()
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

int ObZoneMergeTableOperator::load_zone_merge_info(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    ObZoneMergeInfo &info,
    const bool print_sql)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.get(tenant_id, info);
    if (OB_FAIL(ret) && OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to get zone merge info from storage", K(ret), K(tenant_id));
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS; // Return empty info
    }
  }
  return ret;
}

int ObZoneMergeTableOperator::load_zone_merge_infos(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    ObIArray<ObZoneMergeInfo> &infos,
    const bool print_sql)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.get_all(tenant_id, infos);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to get all zone merge infos from storage", K(ret), K(tenant_id));
    }
  }
  return ret;
}


int ObZoneMergeTableOperator::insert_zone_merge_infos(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObIArray<ObZoneMergeInfo> &infos)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < infos.count(); ++i) {
      if (OB_FAIL(storage_.insert_or_update(infos.at(i)))) {
        LOG_WARN("failed to insert or update zone merge info", K(ret), K(i));
      }
    }
  }
  return ret;
}

int ObZoneMergeTableOperator::update_partial_zone_merge_info(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObZoneMergeInfo &info)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Use SQLite storage - partial update is same as full update for SQLite
    ret = storage_.insert_or_update(info);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to insert or update zone merge info", K(ret), K(tenant_id), K(info));
    }
  }
  return ret;
}


int ObZoneMergeTableOperator::update_zone_merge_infos(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObIArray<ObZoneMergeInfo> &infos)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < infos.count(); ++i) {
      if (OB_FAIL(storage_.insert_or_update(infos.at(i)))) {
        LOG_WARN("failed to insert or update zone merge info", K(ret), K(i));
      }
    }
  }
  return ret;
}

int ObZoneMergeTableOperator::get_zone_list(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    ObIArray<ObZone> &zone_list)
{
  int ret = OB_SUCCESS;
  zone_list.reset();
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else {
    ObArray<ObZoneMergeInfo> infos;
    if (OB_FAIL(storage_.get_all(tenant_id, infos))) {
      LOG_WARN("failed to get all zone merge infos", K(ret), K(tenant_id));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < infos.count(); ++i) {
        if (OB_FAIL(zone_list.push_back(GCTX.config_->zone.str()))) {
          LOG_WARN("failed to push back zone", K(ret));
        }
      }
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
