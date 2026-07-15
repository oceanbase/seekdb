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

#include "sql/engine/expr/ob_expr_load_file.h"

#include "observer/ob_server_struct.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/cmd/ob_load_data_file_reader.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

namespace
{
const ObString FILE_URL_PREFIX("file://");

bool is_safe_relative_file_name(const ObString &file_name)
{
  bool is_safe = !file_name.empty() && '/' != file_name.ptr()[0];
  for (int64_t pos = 0; is_safe && pos < file_name.length();) {
    const int64_t segment_begin = pos;
    while (pos < file_name.length() && '/' != file_name.ptr()[pos]) {
      is_safe = '\0' != file_name.ptr()[pos];
      ++pos;
    }
    const int64_t segment_length = pos - segment_begin;
    is_safe = is_safe && !(2 == segment_length
                           && '.' == file_name.ptr()[segment_begin]
                           && '.' == file_name.ptr()[segment_begin + 1]);
    ++pos;
  }
  return is_safe;
}

int build_local_file_path(ObIAllocator &allocator,
                          const ObString &location_url,
                          const ObString &file_name,
                          ObString &file_path)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(location_url.length() < FILE_URL_PREFIX.length()
                  || 0 != ObString(FILE_URL_PREFIX.length(), location_url.ptr()).case_compare(FILE_URL_PREFIX))) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("location is not a local file location", K(ret), K(location_url));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "LOAD_FILE only supports file:// locations");
  } else if (OB_UNLIKELY(!is_safe_relative_file_name(file_name))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid relative file name", K(ret), K(file_name));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "file_name must be a relative path without parent traversal");
  } else {
    const ObString base_path(location_url.length() - FILE_URL_PREFIX.length(),
                             location_url.ptr() + FILE_URL_PREFIX.length());
    const bool need_separator = !base_path.empty() && '/' != base_path.ptr()[base_path.length() - 1];
    const int64_t path_length = base_path.length() + (need_separator ? 1 : 0) + file_name.length();
    char *path_buffer = static_cast<char *>(allocator.alloc(path_length + 1));
    if (OB_ISNULL(path_buffer)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate local file path", K(ret), K(path_length));
    } else {
      int64_t pos = 0;
      MEMCPY(path_buffer + pos, base_path.ptr(), base_path.length());
      pos += base_path.length();
      if (need_separator) {
        path_buffer[pos++] = '/';
      }
      MEMCPY(path_buffer + pos, file_name.ptr(), file_name.length());
      pos += file_name.length();
      path_buffer[pos] = '\0';
      file_path.assign_ptr(path_buffer, static_cast<int32_t>(path_length));
    }
  }
  return ret;
}
} // namespace

ObExprLoadFile::ObExprLoadFile(ObIAllocator &allocator)
    : ObFuncExprOperator(allocator,
                         T_FUN_SYS_LOAD_FILE,
                         N_LOAD_FILE,
                         2,
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location_type,
                                      ObExprResType &file_type,
                                      ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  location_type.set_calc_type(ObVarcharType);
  location_type.set_calc_collation_type(ObCharset::get_system_collation());
  file_type.set_calc_type(ObVarcharType);
  file_type.set_calc_collation_type(ObCharset::get_system_collation());
  type.set_type(ObLongTextType);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  return OB_SUCCESS;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = nullptr;
  ObDatum *file_datum = nullptr;
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  ObSchemaGetterGuard schema_guard;
  ObSessionPrivInfo session_priv;
  const ObLocationSchema *location_schema = nullptr;
  ObEvalCtx::TempAllocGuard temp_alloc_guard(ctx);
  ObIAllocator &allocator = temp_alloc_guard.get_allocator();
  ObString file_path;
  int64_t file_size = 0;
  int64_t max_allowed_packet = 0;
  ObRandomFileReader reader(allocator);

  if (OB_ISNULL(session) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret), KP(session), KP(GCTX.schema_service_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("failed to evaluate LOAD_FILE arguments", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    result.set_null();
  } else if (location_datum->get_string().empty() || file_datum->get_string().empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("LOAD_FILE arguments must not be empty", K(ret),
             "location_name", location_datum->get_string(), "file_name", file_datum->get_string());
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "LOAD_FILE arguments must not be empty");
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(session->get_session_priv_info(session_priv))) {
    LOG_WARN("failed to get session privilege info", K(ret));
  } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                        session->get_enable_role_array(),
                                                        location_datum->get_string(),
                                                        false))) {
    LOG_WARN("location read access denied", K(ret), "location_name", location_datum->get_string());
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_datum->get_string(),
                                                              location_schema))) {
    LOG_WARN("failed to get location schema", K(ret), "location_name", location_datum->get_string());
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("location schema is null", K(ret), "location_name", location_datum->get_string());
  } else if (OB_FAIL(build_local_file_path(allocator,
                                           location_schema->get_location_url_str(),
                                           file_datum->get_string(),
                                           file_path))) {
    LOG_WARN("failed to build local file path", K(ret),
             "location_url", location_schema->get_location_url_str(), "file_name", file_datum->get_string());
  } else if (OB_FAIL(reader.open(file_path))) {
    LOG_WARN("failed to open local file", K(ret), K(file_path));
  } else if (OB_FAIL(reader.get_file_size(file_size))) {
    LOG_WARN("failed to get local file size", K(ret), K(file_path));
  } else if (OB_UNLIKELY(file_size < 0)) {
    ret = OB_IO_ERROR;
    LOG_WARN("invalid local file size", K(ret), K(file_path), K(file_size));
  } else if (OB_FAIL(session->get_max_allowed_packet(max_allowed_packet))) {
    LOG_WARN("failed to get max_allowed_packet", K(ret));
  } else if (OB_UNLIKELY(file_size > max_allowed_packet)) {
    ret = OB_ERR_FUNC_RESULT_TOO_LARGE;
    LOG_WARN("LOAD_FILE result exceeds max_allowed_packet", K(ret), K(file_path), K(file_size), K(max_allowed_packet));
    LOG_USER_ERROR(OB_ERR_FUNC_RESULT_TOO_LARGE, N_LOAD_FILE, static_cast<int>(max_allowed_packet));
  } else {
    ObTextStringDatumResult output(expr.datum_meta_.type_, &expr, &ctx, &result);
    char *buffer = nullptr;
    int64_t buffer_size = 0;
    int64_t read_size = 0;
    if (OB_FAIL(output.init(file_size))) {
      LOG_WARN("failed to initialize LOAD_FILE result", K(ret), K(file_size));
    } else if (file_size > 0 && OB_FAIL(output.get_reserved_buffer(buffer, buffer_size))) {
      LOG_WARN("failed to reserve LOAD_FILE result buffer", K(ret), K(file_size));
    } else if (file_size > 0 && OB_FAIL(reader.readn(buffer, file_size, read_size))) {
      LOG_WARN("failed to read local file", K(ret), K(file_path), K(file_size), K(read_size));
    } else if (OB_UNLIKELY(read_size != file_size)) {
      ret = OB_IO_ERROR;
      LOG_WARN("local file was not read completely", K(ret), K(file_path), K(file_size), K(read_size));
    } else if (file_size > 0 && OB_FAIL(output.lseek(file_size, 0))) {
      LOG_WARN("failed to advance LOAD_FILE result", K(ret), K(file_size));
    } else {
      output.set_result();
    }
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
