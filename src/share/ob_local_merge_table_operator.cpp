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

#include "share/ob_local_merge_table_operator.h"

#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_local_merge_info.h"
#include "share/storage/ob_local_merge_info_table_storage.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

// Static storage instance
ObLocalMergeInfoTableStorage ObLocalMergeTableOperator::storage_;

int ObLocalMergeTableOperator::init()
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

int ObLocalMergeTableOperator::load_local_merge_info(
    ObISQLClient &sql_client,
    ObLocalMergeInfo &info,
    const bool print_sql)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    ret = storage_.get(info);
    if (OB_FAIL(ret) && OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("failed to get local merge info from storage", K(ret));
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS; // Return empty info
    }
  }
  return ret;
}

int ObLocalMergeTableOperator::insert_local_merge_info(
    ObISQLClient &sql_client,
    const ObLocalMergeInfo &info)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else if (OB_FAIL(storage_.insert_or_update(info))) {
    LOG_WARN("failed to insert local merge info", K(ret));
  }
  return ret;
}

int ObLocalMergeTableOperator::update_partial_local_merge_info(
    ObISQLClient &sql_client,
    const ObLocalMergeInfo &info)
{
  int ret = OB_SUCCESS;
  if (!storage_.is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("storage not initialized", K(ret));
  } else {
    // Use SQLite storage - partial update is same as full update for SQLite
    ret = storage_.insert_or_update(info);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to insert or update local merge info", K(ret), K(info));
    }
  }
  return ret;
}


} // end namespace share
} // end namespace oceanbase
