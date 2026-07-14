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

#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#else
#include <unistd.h>
#endif

#include "share/ob_server_struct.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/ob_exec_context.h"
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
const char FILE_URL_PREFIX[] = "file://";
#ifdef _WIN32
const int FILE_BINARY_FLAG = _O_BINARY;
#else
const int FILE_BINARY_FLAG = 0;
#endif
}

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_LOAD_FILE, N_LOAD_FILE, 2,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location_type,
                                      ObExprResType &file_type,
                                      ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  location_type.set_calc_type(ObVarcharType);
  location_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  file_type.set_calc_type(ObVarcharType);
  file_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_blob();
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_length(OB_MAX_BLOB_WIDTH);
  return OB_SUCCESS;
}

bool ObExprLoadFile::is_safe_relative_path(const ObString &file_name)
{
  bool safe = !file_name.empty() && file_name.ptr()[0] != '/' && file_name.ptr()[0] != '\\';
  int64_t component_start = 0;
  for (int64_t i = 0; safe && i <= file_name.length(); ++i) {
    const bool at_end = i == file_name.length();
    const bool separator = !at_end && (file_name.ptr()[i] == '/' || file_name.ptr()[i] == '\\');
    if (separator || at_end) {
      const int64_t component_len = i - component_start;
      safe = component_len > 0
          && !(component_len == 2
               && file_name.ptr()[component_start] == '.'
               && file_name.ptr()[component_start + 1] == '.');
      component_start = i + 1;
    }
  }
  return safe;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = nullptr;
  ObDatum *file_datum = nullptr;
  ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
  ObSchemaGetterGuard schema_guard;
  ObSessionPrivInfo session_priv;
  const ObLocationSchema *location_schema = nullptr;
  int fd = -1;

  if (OB_ISNULL(session) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret), KP(session), KP(GCTX.schema_service_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("failed to evaluate load_file arguments", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else {
    const ObString location_name = location_datum->get_string();
    const ObString file_name = file_datum->get_string();
    if (location_name.empty() || !is_safe_relative_path(file_name)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file requires a location name and a safe relative file name");
      LOG_WARN("invalid load_file argument", K(ret), K(location_name), K(file_name));
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("failed to get tenant schema guard", K(ret));
    } else if (OB_FAIL(session->get_session_priv_info(session_priv))) {
      LOG_WARN("failed to get session privilege info", K(ret));
    } else if (OB_FAIL(schema_guard.check_location_access(
                   session_priv, session->get_enable_role_array(), location_name, false))) {
      LOG_WARN("failed to check location read privilege", K(ret), K(location_name));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to get location schema", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_LOCATION_OBJ_NOT_EXIST;
      LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST, location_name.length(), location_name.ptr());
      LOG_WARN("location does not exist", K(ret), K(location_name));
    } else {
      const ObString location_url = location_schema->get_location_url_str();
      const ObString prefix(FILE_URL_PREFIX);
      ObSqlString full_path;
      if (!location_url.prefix_match(prefix)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file only supports file:// locations");
        LOG_WARN("unsupported location url", K(ret), K(location_url));
      } else {
        ObString root_path(location_url.length() - prefix.length(),
                           location_url.ptr() + prefix.length());
#ifdef _WIN32
        if (root_path.length() >= 3
            && root_path.ptr()[0] == '/'
            && root_path.ptr()[2] == ':') {
          root_path.assign_ptr(root_path.ptr() + 1, root_path.length() - 1);
        }
#endif
        if (root_path.empty()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("location root path is empty", K(ret), K(location_url));
        } else if (OB_FAIL(full_path.append(root_path))) {
          LOG_WARN("failed to append location root path", K(ret));
        } else if (root_path.ptr()[root_path.length() - 1] != '/'
                   && OB_FAIL(full_path.append("/"))) {
          LOG_WARN("failed to append path separator", K(ret));
        } else if (OB_FAIL(full_path.append(file_name))) {
          LOG_WARN("failed to append file name", K(ret));
        }
      }

      struct stat st;
      if (OB_FAIL(ret)) {
      } else if ((fd = ::open(full_path.ptr(), O_RDONLY | O_CLOEXEC | FILE_BINARY_FLAG)) < 0) {
        ret = OB_IO_ERROR;
        LOG_WARN("failed to open load_file target", K(ret), K(full_path), K(errno));
      } else if (::fstat(fd, &st) != 0) {
        ret = OB_IO_ERROR;
        LOG_WARN("failed to stat load_file target", K(ret), K(full_path), K(errno));
      } else if (!S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > OB_MAX_BLOB_WIDTH) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file target must be a regular file within the BLOB size limit");
        LOG_WARN("invalid load_file target", K(ret), K(full_path), K(st.st_mode), K(st.st_size));
      } else {
        ObTextStringDatumResult output(expr.datum_meta_.type_, &expr, &ctx, &res);
        char *buf = nullptr;
        int64_t buf_size = 0;
        if (OB_FAIL(output.init(st.st_size))) {
          LOG_WARN("failed to initialize load_file result", K(ret), K(st.st_size));
        } else if (st.st_size > 0 && OB_FAIL(output.get_reserved_buffer(buf, buf_size))) {
          LOG_WARN("failed to get load_file result buffer", K(ret), K(st.st_size));
        } else {
          int64_t pos = 0;
          while (OB_SUCC(ret) && pos < st.st_size) {
            const ssize_t read_size = ::read(fd, buf + pos, st.st_size - pos);
            if (read_size < 0 && errno == EINTR) {
              continue;
            } else if (read_size <= 0) {
              ret = OB_IO_ERROR;
              LOG_WARN("failed to read complete file", K(ret), K(full_path), K(pos), K(st.st_size), K(errno));
            } else {
              pos += read_size;
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(output.lseek(pos, 0))) {
              LOG_WARN("failed to set load_file result length", K(ret), K(pos));
            } else {
              output.set_result();
            }
          }
        }
      }
    }
  }

  if (fd >= 0 && ::close(fd) != 0 && OB_SUCC(ret)) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to close load_file target", K(ret), K(errno));
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
