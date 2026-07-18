/*
 * Copyright (c) 2026 OceanBase.
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
#include "lib/utility/utility.h"
#include "lib/oblog/ob_log_module.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

namespace
{
int read_local_file(const char *path, ObIAllocator &allocator, ObString &content)
{
  int ret = OB_SUCCESS;
  struct stat st;
  int fd = -1;
  if (OB_UNLIKELY(OB_ISNULL(path)) || OB_UNLIKELY('\0' == path[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid file path", K(ret));
  } else if (OB_UNLIKELY(0 != stat(path, &st))) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("file not found", K(ret), K(path));
  } else if (OB_UNLIKELY(st.st_size <= 0)) {
    // empty file - return empty content, not an error
    content.reset();
  } else if (OB_UNLIKELY(st.st_size > 1024L * 1024L * 1024L)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("file too large for load_file", K(ret), K(st.st_size));
  } else {
    const int64_t file_size = st.st_size;
    char *buf = static_cast<char *>(allocator.alloc(file_size));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate file buffer", K(ret), K(file_size));
    } else {
      fd = ::open(path, O_RDONLY | O_BINARY);
      if (OB_UNLIKELY(fd < 0)) {
        // stat() confirmed the file exists, so open() failing indicates a permission
        // or IO issue rather than "file not found"
        ret = OB_IO_ERROR;
        LOG_WARN("failed to open file", K(ret), K(path), K(errno));
      } else {
        int64_t total_read = 0;
        while (total_read < file_size) {
          ssize_t r = ::read(fd, buf + total_read, file_size - total_read);
          if (r > 0) {
            total_read += r;
          } else if (0 == r) {
            break;
          } else if (errno != EINTR) {
            ret = OB_IO_ERROR;
            LOG_WARN("failed to read file", K(ret), K(path), K(errno));
            break;
          }
        }
        if (OB_SUCC(ret)) {
          content.assign_ptr(buf, total_read);
        }
      }
    }
  }
  if (fd >= 0) {
    ::close(fd);
  }
  return ret;
}
}

ObExprLoadFile::ObExprLoadFile(common::ObIAllocator &alloc)
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
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  type1.set_calc_type(ObVarcharType);
  type1.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type2.set_calc_type(ObVarcharType);
  type2.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_type(ObLongTextType);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *file_datum = NULL;
  const ObSQLSessionInfo *session_info = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = NULL;
  share::schema::ObSessionPrivInfo session_priv;
  ObString location_name;
  ObString file_name;
  ObString location_url;
  ObString file_content;
  ObString file_path;
  ObArenaAllocator allocator("LoadFile");

  if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret), KP(session_info), KP(GCTX.schema_service_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("failed to evaluate load_file arguments", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else {
    location_name = location_datum->get_string();
    file_name = file_datum->get_string();
    // Validate file name: reject path separators and parent-dir references.
    for (int64_t i = 0; i < file_name.length(); ++i) {
      if ('/' == file_name.ptr()[i] || '\\' == file_name.ptr()[i]) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file file name must not contain path separators");
        break;
      } else if (i + 1 < file_name.length()
                 && '.' == file_name.ptr()[i]
                 && '.' == file_name.ptr()[i + 1]) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file file name must not contain '..'");
        break;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (location_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file location name must not be empty");
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("failed to get schema guard", K(ret));
    } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
      LOG_WARN("failed to get session privilege info", K(ret));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to get location schema", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_LOCATION_OBJ_NOT_EXIST;
      LOG_WARN("location does not exist", K(ret), K(location_name));
      LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST, location_name.length(), location_name.ptr());
    } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                           session_info->get_enable_role_array(),
                                                           location_name,
                                                           false /* is_write */))) {
      LOG_WARN("failed to check location read privilege", K(ret), K(location_name));
    } else {
      location_url = location_schema->get_location_url_str();
      if (!location_url.prefix_match(OB_FILE_PREFIX)) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file with a non-file LOCATION");
      } else {
        const int64_t directory_len = location_url.length() - STRLEN(OB_FILE_PREFIX);
        // Ensure trailing slash between directory path and file name
        bool has_trailing_slash = (directory_len > 0
            && (location_url.ptr()[location_url.length() - 1] == '/'
                || location_url.ptr()[location_url.length() - 1] == '\\'));
        const int64_t file_path_len = directory_len + (has_trailing_slash ? 0 : 1) + file_name.length();
        char *file_path_buf = static_cast<char *>(allocator.alloc(file_path_len + 1));
        if (OB_ISNULL(file_path_buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate file path", K(ret));
        } else {
          int64_t pos = 0;
          MEMCPY(file_path_buf, location_url.ptr() + STRLEN(OB_FILE_PREFIX), directory_len);
          pos += directory_len;
          if (!has_trailing_slash) {
            file_path_buf[pos++] = '/';
          }
          MEMCPY(file_path_buf + pos, file_name.ptr(), file_name.length());
          pos += file_name.length();
          file_path_buf[pos] = '\0';
          file_path.assign_ptr(file_path_buf, pos);
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(read_local_file(file_path.ptr(), allocator, file_content))) {
        LOG_WARN("failed to read location file", K(ret), K(location_name), K(file_name));
      }
      if (OB_SUCC(ret)) {
        ObTextStringDatumResult text_result(expr.datum_meta_.type_, &expr, &ctx, &res);
        if (OB_FAIL(text_result.init(file_content.length()))) {
          LOG_WARN("failed to initialize load_file result", K(ret));
        } else if (OB_FAIL(text_result.append(file_content))) {
          LOG_WARN("failed to append load_file result", K(ret));
        } else {
          text_result.set_result();
        }
      }
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
