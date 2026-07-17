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
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "lib/ob_define.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_LOAD_FILE, N_LOAD_FILE, 2,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &type1,
                                      ObExprResType &type2,
                                      ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  type1.set_calc_type(ObVarcharType);
  type1.set_calc_collation_type(ObCharset::get_system_collation());
  type2.set_calc_type(ObVarcharType);
  type2.set_calc_collation_type(ObCharset::get_system_collation());
  type.set_blob();
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_length(OB_MAX_LONGTEXT_LENGTH);
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *file_datum = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObSQLSessionInfo *session_info = NULL;
  ObSessionPrivInfo session_priv;
  const ObLocationSchema *location_schema = NULL;
  ObString location_name;
  ObString file_name;
  ObString location_url;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  ObIAllocator &allocator = tmp_alloc_g.get_allocator();

  if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (OB_ISNULL(location_datum) || OB_ISNULL(file_datum)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parameters are null", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    expr_datum.set_null();
  } else {
    location_name = location_datum->get_string();
    file_name = file_datum->get_string();
    if (location_name.empty() || file_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, location_name or file_name is empty");
      LOG_WARN("location_name or file_name is empty", K(ret), K(location_name), K(file_name));
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("failed to get tenant schema guard", K(ret));
    } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
      LOG_WARN("failed to get session priv info", K(ret));
    } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                          session_info->get_enable_role_array(),
                                                          location_name,
                                                          false /* is_write */))) {
      LOG_WARN("failed to check location access", K(ret), K(location_name));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to get location schema", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_LOCATION_OBJ_NOT_EXIST;
      LOG_WARN("location schema is null", K(ret), K(location_name));
    } else {
      location_url = location_schema->get_location_url_str();
      if (!location_url.prefix_match(OB_FILE_PREFIX)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file only supports file:// locations");
        LOG_WARN("location url is not file://", K(ret), K(location_url));
      } else {
        char uri_buf[OB_MAX_URI_LENGTH] = {0};
        int64_t pos = 0;
        if (OB_FAIL(databuff_printf(uri_buf, OB_MAX_URI_LENGTH, pos, "%.*s%.*s",
                                    location_url.length(), location_url.ptr(),
                                    file_name.length(), file_name.ptr()))) {
          LOG_WARN("failed to build file uri", K(ret), K(location_url), K(file_name));
        } else {
          const int64_t prefix_len = static_cast<int64_t>(strlen(OB_FILE_PREFIX));
          const char *local_path = uri_buf + prefix_len;
          ObString file_content;
          int fd = -1;
          struct stat st;
          if ((fd = ::open(local_path, O_RDONLY)) < 0) {
            ret = OB_IO_ERROR;
            LOG_WARN("failed to open file", K(ret), K(local_path));
          } else if (0 != ::fstat(fd, &st)) {
            ret = OB_IO_ERROR;
            LOG_WARN("failed to stat file", K(ret), K(local_path));
          } else {
            char *buf = NULL;
            const int64_t file_size = st.st_size;
            ObEvalCtx::TempAllocGuard read_alloc_g(ctx);
            common::ObArenaAllocator &read_alloc = read_alloc_g.get_allocator();
            if (OB_ISNULL(buf = static_cast<char *>(read_alloc.alloc(file_size)))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("failed to alloc file buffer", K(ret), K(file_size));
            } else {
              const ssize_t read_size = ::read(fd, buf, file_size);
              if (read_size < 0 || read_size != file_size) {
                ret = OB_IO_ERROR;
                LOG_WARN("failed to read file", K(ret), K(local_path), K(file_size), K(read_size));
              } else {
                file_content.assign_ptr(buf, static_cast<int32_t>(file_size));
              }
            }
          }
          if (fd >= 0) {
            ::close(fd);
          }
          if (OB_FAIL(ret)) {
          } else {
            ObTextStringDatumResult str_result(expr.datum_meta_.type_, &expr, &ctx, &expr_datum);
            if (OB_FAIL(str_result.init(file_content.length()))) {
              LOG_WARN("init lob result failed", K(ret));
            } else if (OB_FAIL(str_result.append(file_content.ptr(), file_content.length()))) {
              LOG_WARN("append lob result failed", K(ret));
            } else {
              str_result.set_result();
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
