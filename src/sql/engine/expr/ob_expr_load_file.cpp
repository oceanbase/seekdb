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
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "observer/ob_server_struct.h"
#include "share/io/ob_backup_io_adapter.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
  : ObStringExprOperator(alloc, T_FUN_SYS_LOAD_FILE, N_LOAD_FILE, 2,
                         NOT_VALID_FOR_GENERATED_COL)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location,
                                      ObExprResType &file_name,
                                      ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  type.set_blob();
  type.set_length(OB_MAX_BLOB_WIDTH);
  location.set_calc_type(ObVarcharType);
  location.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  file_name.set_calc_type(ObVarcharType);
  file_name.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  return ret;
}

static bool is_safe_relative_file_name(const ObString &file_name)
{
  bool safe = !file_name.empty() && file_name[0] != '/' && file_name[0] != '\\';
  int64_t component_start = 0;
  for (int64_t i = 0; safe && i <= file_name.length(); ++i) {
    if (i < file_name.length() && file_name[i] == '\0') {
      safe = false;
    } else if (i == file_name.length() || file_name[i] == '/' || file_name[i] == '\\') {
      const int64_t component_len = i - component_start;
      safe = component_len > 0
          && !(component_len == 2
               && file_name[component_start] == '.'
               && file_name[component_start + 1] == '.');
      component_start = i + 1;
    }
  }
  return safe;
}

static int build_file_uri(ObIAllocator &allocator,
                          const ObString &location_url,
                          const ObString &file_name,
                          ObString &file_uri)
{
  int ret = OB_SUCCESS;
  const ObString file_prefix("file://");
  const bool append_separator = location_url.empty()
      || (location_url[location_url.length() - 1] != '/'
          && location_url[location_url.length() - 1] != '\\');
  const int64_t uri_length = location_url.length() + (append_separator ? 1 : 0) + file_name.length();
  char *uri_buffer = nullptr;
  if (!location_url.prefix_match(file_prefix)) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "LOAD_FILE only supports file:// locations");
  } else if (!is_safe_relative_file_name(file_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "LOAD_FILE file_name must be a safe relative path");
  } else if (OB_ISNULL(uri_buffer = static_cast<char *>(allocator.alloc(uri_length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate file uri failed", K(ret), K(uri_length));
  } else {
    int64_t pos = 0;
    MEMCPY(uri_buffer + pos, location_url.ptr(), location_url.length());
    pos += location_url.length();
    if (append_separator) {
      uri_buffer[pos++] = '/';
    }
    MEMCPY(uri_buffer + pos, file_name.ptr(), file_name.length());
    file_uri.assign_ptr(uri_buffer, static_cast<int32_t>(uri_length));
  }
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = nullptr;
  ObDatum *file_datum = nullptr;
  ObSQLSessionInfo *session_info = nullptr;
  ObSchemaGetterGuard schema_guard;
  ObSessionPrivInfo session_priv;
  const ObLocationSchema *location_schema = nullptr;
  bool allow_access = false;
  if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("evaluate LOAD_FILE parameters failed", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session())
             || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("get tenant schema guard failed", K(ret));
  } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
    LOG_WARN("get session privilege info failed", K(ret));
  } else if (OB_FAIL(schema_guard.check_location_show(session_priv,
                                                       session_info->get_enable_role_array(),
                                                       location_datum->get_string(),
                                                       allow_access))) {
    LOG_WARN("check location access failed", K(ret));
  } else if (!allow_access) {
    ret = OB_ERR_LOCATION_ACCESS_DENIED;
    LOG_WARN("location access denied", K(ret), K(location_datum->get_string()));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_datum->get_string(),
                                                               location_schema))) {
    LOG_WARN("get location schema failed", K(ret), K(location_datum->get_string()));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("location schema is null", K(ret));
  } else {
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);
    ObString file_uri;
    int64_t file_length = 0;
    int64_t read_size = 0;
    ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &res);
    if (OB_FAIL(build_file_uri(alloc_guard.get_allocator(),
                               location_schema->get_location_url_str(),
                               file_datum->get_string(),
                               file_uri))) {
      LOG_WARN("build file uri failed", K(ret));
    } else if (OB_FAIL(ObBackupIoAdapter::adaptively_get_file_length(file_uri, nullptr, file_length))) {
      LOG_WARN("get file length failed", K(ret), K(file_uri));
    } else if (file_length < 0 || file_length > OB_MAX_BLOB_WIDTH) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("file is too large for BLOB", K(ret), K(file_length));
    } else if (file_length == 0) {
      res.set_string(nullptr, 0);
    } else if (OB_FAIL(output_result.init(file_length))) {
      LOG_WARN("initialize BLOB result failed", K(ret), K(file_length));
    } else {
      char *output_buffer = nullptr;
      int64_t buffer_size = 0;
      if (OB_FAIL(output_result.get_reserved_buffer(output_buffer, buffer_size))) {
        LOG_WARN("get BLOB result buffer failed", K(ret));
      } else if (OB_FAIL(ObBackupIoAdapter::adaptively_read_single_file(
                           file_uri, nullptr, output_buffer, buffer_size, read_size,
                           ObStorageIdMod::get_default_id_mod()))) {
        LOG_WARN("read location file failed", K(ret), K(file_uri));
      } else if (read_size != file_length) {
        ret = OB_IO_ERROR;
        LOG_WARN("file size changed while reading", K(ret), K(file_length), K(read_size));
      } else if (OB_FAIL(output_result.lseek(read_size, 0))) {
        LOG_WARN("set BLOB result length failed", K(ret), K(read_size));
      } else {
        output_result.set_result();
      }
    }
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &op_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
