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

#define USING_LOG_PREFIX PL

#include "sql/pl/ob_pl_server_cursor.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace sql;

namespace pl
{

int ObPLServerCursorInfo::close(ObSQLSessionInfo &session, bool is_reuse)
{
  int ret = OB_SUCCESS;
  OZ (ObPLCursorInfo::close(session, is_reuse));
  exec_params_.reset();
  fields_.reset();
  ps_sql_.reset();
  stmt_type_ = stmt::T_NONE;
  if (nullptr != sql_entity_) {
    DESTROY_CONTEXT(sql_entity_);
    sql_entity_ = nullptr;
  }
  return ret;
}

void ObPLServerCursorInfo::reset()
{
  exec_params_.reset();
  fields_.reset();
  ps_sql_.reset();
  stmt_type_ = stmt::T_NONE;
  if (nullptr != sql_entity_) {
    DESTROY_CONTEXT(sql_entity_);
    sql_entity_ = nullptr;
  }
  ObPLCursorInfo::reset();
}

int ObPLServerCursorInfo::prepare_entity(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObPLCursorInfo::prepare_entity(session, sql_entity_))) {
    LOG_WARN("prepare server cursor SQL entity failed", K(ret), K(get_id()));
  } else if (OB_FAIL(ObPLCursorInfo::prepare_entity(session, get_cursor_entity()))) {
    LOG_WARN("prepare server cursor entity failed", K(ret), K(get_id()));
  }
  return ret;
}

int ObPLServerCursorInfo::init_params(int64_t param_count)
{
  int ret = OB_SUCCESS;
  ObIAllocator *alloc = nullptr;
  OV (OB_NOT_NULL(sql_entity_), OB_NOT_INIT, ps_sql_, param_count);
  OX (alloc = &sql_entity_->get_arena_allocator());
  CK (OB_NOT_NULL(alloc));
  if (OB_SUCC(ret)) {
    exec_params_.~Ob2DArray();
    new (&exec_params_) ParamStore(ObWrapperAllocator(alloc));
  }
  return ret;
}

int ObPLServerCursorInfo::deep_copy_field_columns(
    ObIAllocator &allocator,
    const ColumnsFieldIArray *src_fields,
    ColumnsFieldArray &dst_fields)
{
  int ret = OB_SUCCESS;
  dst_fields.reset();
  dst_fields.set_allocator(&allocator);
  if (OB_ISNULL(src_fields)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot copy null cursor fields", K(ret));
  } else if (src_fields->count() < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid cursor field count", K(ret), K(src_fields->count()));
  } else if (OB_FAIL(dst_fields.reserve(src_fields->count()))) {
    LOG_WARN("reserve cursor fields failed", K(ret), K(src_fields->count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < src_fields->count(); ++i) {
      ObField field;
      if (OB_FAIL(field.deep_copy(src_fields->at(i), &allocator))) {
        LOG_WARN("deep copy cursor field failed", K(ret), K(i));
      } else if (OB_FAIL(dst_fields.push_back(field))) {
        LOG_WARN("append cursor field failed", K(ret), K(i));
      }
    }
  }
  return ret;
}

} // namespace pl
} // namespace oceanbase
