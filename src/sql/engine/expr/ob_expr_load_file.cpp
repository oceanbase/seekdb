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

#include "sql/engine/expr/ob_expr_load_file.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_location_schema_struct.h"

#include <cerrno>
#include <climits>
#include <cstdio>

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

ObExprLoadFile::ObExprLoadFile(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_LOAD_FILE,
                         N_LOAD_FILE,
                         2,
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location_name,
                                      ObExprResType &file_name,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  location_name.set_calc_type(ObVarcharType);
  location_name.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  file_name.set_calc_type(ObVarcharType);
  file_name.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_blob();
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_length(OB_MAX_MYSQL_VARCHAR_LENGTH);
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_load_file;
  return ret;
}

namespace
{
// copy an ObString into a NUL terminated stack buffer; returns false on overflow
bool to_cstr(const ObString &str, char *buf, int64_t buf_len)
{
  bool bret = false;
  if (OB_LIKELY(!str.empty() && str.length() < buf_len)) {
    MEMCPY(buf, str.ptr(), str.length());
    buf[str.length()] = '\0';
    bret = true;
  }
  return bret;
}

// resolve symlinks and verify that `full` really stays inside `dir`,
// so a file_name like "../etc/passwd" can not escape the LOCATION root
bool path_is_confined(const char *dir, const char *full)
{
  bool bret = false;
  char resolved_dir[PATH_MAX] = {0};
  char resolved_full[PATH_MAX] = {0};
  if (NULL != ::realpath(dir, resolved_dir)
      && NULL != ::realpath(full, resolved_full)) {
    const int64_t dir_len = STRLEN(resolved_dir);
    // strip trailing '/' of the directory (except the root "/")
    while (dir_len > 1 && '/' == resolved_dir[dir_len - 1]) {
      resolved_dir[dir_len - 1] = '\0';
    }
    const int64_t effective_len = STRLEN(resolved_dir);
    bret = 0 == STRNCMP(resolved_full, resolved_dir, effective_len)
        && ('/' == resolved_full[effective_len]
            || '\0' == resolved_full[effective_len]);
  }
  return bret;
}
} // namespace

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(2 != expr.arg_cnt_)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("load_file expects 2 arguments", K(ret), K(expr.arg_cnt_));
  } else {
    ObDatum *location_datum = NULL;
    ObDatum *file_datum = NULL;
    if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
      LOG_WARN("eval load_file arguments failed", K(ret));
    } else if (location_datum->is_null() || file_datum->is_null()) {
      res.set_null();
    } else {
      const ObString location_name = location_datum->get_string();
      const ObString file_name = file_datum->get_string();
      const ObLocationSchema *location_schema = NULL;
      ObSchemaGetterGuard schema_guard;
      const ObSQLSessionInfo *session_info = NULL;
      if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session())
          || OB_ISNULL(GCTX.schema_service_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("session or schema service is null", K(ret));
      } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
        LOG_WARN("failed to get tenant schema guard", K(ret));
      } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))
                 || OB_ISNULL(location_schema)) {
        ret = OB_SUCCESS == ret ? OB_LOCATION_OBJ_NOT_EXIST : ret;
        LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST,
                       static_cast<int>(location_name.length()), location_name.ptr());
        LOG_WARN("location not found", K(ret), K(location_name));
      } else {
        const ObString &url = location_schema->get_location_url_str();
        static const char FILE_SCHEME[] = "file://";
        const int64_t scheme_len = STRLEN(FILE_SCHEME);
        if (OB_UNLIKELY(url.length() <= scheme_len)
            || 0 != STRNCMP(url.ptr(), FILE_SCHEME, scheme_len)) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file only supports file:// locations");
          LOG_WARN("location url is not file://", K(ret), K(url));
        } else {
          // dir part after "file://", keep the leading '/'
          const ObString dir(url.length() - scheme_len, url.ptr() + scheme_len);
          char dir_buf[PATH_MAX] = {0};
          char file_buf[PATH_MAX] = {0};
          char full_buf[PATH_MAX] = {0};
          if (OB_UNLIKELY(!to_cstr(dir, dir_buf, PATH_MAX))
              || OB_UNLIKELY(!to_cstr(file_name, file_buf, PATH_MAX))) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("location url or file name too long", K(ret), K(dir), K(file_name));
          } else {
            // drop a trailing '/' on the dir so we never produce "//name"
            int64_t dir_len = STRLEN(dir_buf);
            while (dir_len > 1 && '/' == dir_buf[dir_len - 1]) {
              dir_buf[--dir_len] = '\0';
            }
            if (OB_UNLIKELY('/' == file_buf[0] || 0 == STRNCMP(file_buf, "..", 2))) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("file name must be relative to the location", K(ret), K(file_name));
            } else if (OB_UNLIKELY(snprintf(full_buf, PATH_MAX, "%s/%s", dir_buf, file_buf) >= PATH_MAX)) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("joined path too long", K(ret));
            } else if (OB_UNLIKELY(!path_is_confined(dir_buf, full_buf))) {
              ret = OB_FILE_NOT_EXIST;
              LOG_USER_ERROR(OB_FILE_NOT_EXIST);
              LOG_WARN("file escapes the location directory or does not exist",
                       K(ret), K(full_buf), K(dir_buf));
            } else {
              FILE *fp = ::fopen(full_buf, "rb");
              if (OB_ISNULL(fp)) {
                ret = OB_FILE_NOT_EXIST;
                LOG_USER_ERROR(OB_FILE_NOT_EXIST);
                LOG_WARN("fopen failed", K(ret), K(full_buf), K(errno));
              } else {
                int64_t file_size = 0;
                if (0 != ::fseek(fp, 0, SEEK_END)) {
                  ret = OB_IO_ERROR;
                  LOG_WARN("fseek end failed", K(ret), K(errno));
                } else if ((file_size = ::ftell(fp)) < 0) {
                  ret = OB_IO_ERROR;
                  LOG_WARN("ftell failed", K(ret), K(errno));
                } else if (OB_UNLIKELY(file_size > MAX_FILE_SIZE)) {
                  ret = OB_SIZE_OVERFLOW;
                  LOG_USER_ERROR(OB_SIZE_OVERFLOW);
                } else if (0 != ::fseek(fp, 0, SEEK_SET)) {
                  ret = OB_IO_ERROR;
                  LOG_WARN("fseek set failed", K(ret), K(errno));
                } else {
                  char *buf = NULL;
                  if (file_size > 0 && OB_ISNULL(buf = expr.get_str_res_mem(ctx, file_size))) {
                    ret = OB_ALLOCATE_MEMORY_FAILED;
                    LOG_WARN("alloc result memory failed", K(ret), K(file_size));
                  } else if (file_size > 0
                             && file_size != static_cast<int64_t>(::fread(buf, 1, file_size, fp))) {
                    ret = OB_IO_ERROR;
                    LOG_WARN("fread failed", K(ret), K(errno));
                  } else {
                    res.set_string(buf, static_cast<uint32_t>(file_size));
                  }
                }
                ::fclose(fp);
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
