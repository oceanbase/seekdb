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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_lob_result_materializer.h"

#include "lib/allocator/page_arena.h"
#include "query/session/ob_session_access.h"
#include "share/ob_lob_access_utils.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{

int materialize_lob_result(common::ObObj &value,
                           common::ObIAllocator *allocator,
                           const ObSQLSessionInfo &session_info,
                           common::ObILobReadService *lob_read_service)
{
  int ret = OB_SUCCESS;
  if (!(value.is_lob() || value.is_json() || value.is_geometry()) ||
      value.is_null() || value.is_nop_value()) {
    // No external payload to materialize.
  } else if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("LOB result allocator is null", K(ret), K(value));
  } else {
    common::ObTextStringIter text_iter(value);
    common::ObArenaAllocator tmp_allocator(
        "LobRead", common::OB_MALLOC_NORMAL_BLOCK_SIZE);
    if (OB_ISNULL(lob_read_service)) {
      ret = text_iter.init(0, nullptr, allocator, &tmp_allocator);
    } else {
      const common::ObLobReadOptions read_options(
          *lob_read_service,
          query::ObSessionAccess::get_query_timeout_ts(&session_info));
      ret = text_iter.init(0, &read_options, allocator, &tmp_allocator);
    }

    common::ObString data;
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to initialize LOB result iterator", K(ret), K(value));
    } else if (OB_FAIL(text_iter.get_full_data(data))) {
      LOG_WARN("failed to materialize LOB result", K(ret), K(value));
    } else {
      common::ObObjType dst_type = common::ObLongTextType;
      if (value.is_json()) {
        dst_type = common::ObJsonType;
      } else if (value.is_geometry()) {
        dst_type = common::ObGeometryType;
      }
      value.set_lob_value(
          dst_type, data.ptr(), static_cast<int32_t>(data.length()));
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
