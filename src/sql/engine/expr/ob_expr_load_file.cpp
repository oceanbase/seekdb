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
#include "sql/engine/cmd/ob_load_data_file_reader.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
using namespace common;

namespace sql
{
namespace
{
static const char FILE_URL_PREFIX[] = "file://";
static const int64_t FILE_URL_PREFIX_LEN = sizeof(FILE_URL_PREFIX) - 1;
}

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_LOAD_FILE, "load_file", 2,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location_name,
                                      ObExprResType &file_name,
                                      ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  location_name.set_calc_type(ObVarcharType);
  location_name.set_calc_collation_type(ObCharset::get_system_collation());
  file_name.set_calc_type(ObVarcharType);
  file_name.set_calc_collation_type(ObCharset::get_system_collation());
  type.set_type(ObLongTextType);
  type.set_length(OB_MAX_LONGTEXT_LENGTH);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  return ret;
}

bool ObExprLoadFile::is_file_url(const ObString &url)
{
  bool is_match = false;
  if (url.length() >= FILE_URL_PREFIX_LEN) {
    is_match = (0 == MEMCMP(url.ptr(), FILE_URL_PREFIX, FILE_URL_PREFIX_LEN));
  }
  return is_match;
}

bool ObExprLoadFile::is_safe_relative_path(const ObString &path)
{
  bool is_safe = true;
  int64_t segment_start = 0;
  if (0 == path.length() || OB_ISNULL(path.ptr()) || '/' == path.ptr()[0]) {
    is_safe = false;
  }
  for (int64_t i = 0; is_safe && i <= path.length(); ++i) {
    if (i == path.length() || '/' == path.ptr()[i]) {
      const int64_t segment_len = i - segment_start;
      if (2 == segment_len
          && '.' == path.ptr()[segment_start]
          && '.' == path.ptr()[segment_start + 1]) {
        is_safe = false;
      } else {
        segment_start = i + 1;
      }
    } else if ('\0' == path.ptr()[i]) {
      is_safe = false;
    }
  }
  return is_safe;
}

int ObExprLoadFile::build_full_path(const ObString &location_url,
                                    const ObString &file_name,
                                    ObIAllocator &allocator,
                                    ObString &full_path)
{
  int ret = OB_SUCCESS;
  full_path.reset();
  if (!is_file_url(location_url)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("load_file only supports file url", K(ret), K(location_url));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file only supports file:// location url");
  } else if (!is_safe_relative_path(file_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid load_file file name", K(ret), K(file_name));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file file name must be a safe relative path");
  } else {
    const char *base_ptr = location_url.ptr() + FILE_URL_PREFIX_LEN;
    const int64_t base_len = location_url.length() - FILE_URL_PREFIX_LEN;
    const bool need_slash = base_len > 0 && '/' != base_ptr[base_len - 1];
    const int64_t full_path_len = base_len + (need_slash ? 1 : 0) + file_name.length();
    if (base_len <= 0 || full_path_len > INT32_MAX) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("load_file path size overflow", K(ret), K(base_len), K(file_name), K(full_path_len));
    } else {
      char *buf = static_cast<char *>(allocator.alloc(full_path_len + 1));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret), K(full_path_len));
      } else {
        int64_t pos = 0;
        MEMCPY(buf + pos, base_ptr, base_len);
        pos += base_len;
        if (need_slash) {
          buf[pos++] = '/';
        }
        MEMCPY(buf + pos, file_name.ptr(), file_name.length());
        pos += file_name.length();
        buf[pos] = '\0';
        full_path.assign_ptr(buf, static_cast<ObString::obstr_size_t>(full_path_len));
      }
    }
  }
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *file_datum = NULL;
  const ObSQLSessionInfo *session_info = NULL;
  share::schema::ObSchemaGetterGuard schema_guard;
  share::schema::ObSessionPrivInfo session_priv;
  const share::schema::ObLocationSchema *location_schema = NULL;
  ObFileReader *file_reader = NULL;

  if (expr.arg_cnt_ != 2) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("incorrect parameter count in load_file", K(ret), K(expr.arg_cnt_));
  } else if (OB_ISNULL(expr.args_) || OB_ISNULL(expr.args_[0]) || OB_ISNULL(expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of load_file expr is null", K(ret), K(expr.args_));
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), KP(session_info), KP(GCTX.schema_service_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_datum))) {
    LOG_WARN("eval location name failed", K(ret));
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, file_datum))) {
    LOG_WARN("eval file name failed", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    expr_datum.set_null();
  } else {
    const ObString location_name = location_datum->get_string();
    const ObString file_name = file_datum->get_string();
    ObString full_path;
    int64_t file_size = 0;
    int64_t read_size = 0;

    ObFileReadParam file_read_param;
    file_read_param.file_location_ = ObLoadFileLocation::SERVER_DISK;

    if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("failed to get tenant schema guard", K(ret));
    } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
      LOG_WARN("failed to get session priv info", K(ret));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to get location schema", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("location does not exist", K(ret), K(location_name));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file location does not exist");
    } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                         session_info->get_enable_role_array(),
                                                         location_name,
                                                         false))) {
      LOG_WARN("failed to check location read access", K(ret), K(location_name));
    } else if (OB_FAIL(build_full_path(location_schema->get_location_url_str(),
                                      file_name,
                                      ctx.exec_ctx_.get_allocator(),
                                      full_path))) {
      LOG_WARN("failed to build full path", K(ret), K(location_name), K(file_name));
    } else {
      file_read_param.filename_ = full_path;
      if (OB_FAIL(ObFileReader::open(file_read_param, ctx.exec_ctx_.get_allocator(), file_reader))) {
        LOG_WARN("failed to open load_file target", K(ret), K(full_path));
      } else if (OB_FAIL(file_reader->get_file_size(file_size))) {
        LOG_WARN("failed to get load_file target size", K(ret), K(full_path));
      } else if (file_size < 0 || file_size > INT32_MAX) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("load_file target size overflow", K(ret), K(file_size), K(full_path));
      } else if (0 == file_size) {
        ObString empty_content;
        if (OB_FAIL(ObTextStringHelper::string_to_templob_result(expr, ctx, expr_datum, empty_content))) {
          LOG_WARN("failed to set empty load_file blob result", K(ret));
        }
      } else {
        char *file_buf = static_cast<char *>(ctx.exec_ctx_.get_allocator().alloc(file_size));
        if (OB_ISNULL(file_buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("allocate memory failed", K(ret), K(file_size));
        } else if (OB_FAIL(file_reader->readn(file_buf, file_size, read_size))) {
          LOG_WARN("failed to read load_file target", K(ret), K(full_path), K(file_size), K(read_size));
        } else if (read_size != file_size) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("load_file read size mismatch", K(ret), K(full_path), K(file_size), K(read_size));
        } else {
          ObString file_content(static_cast<ObString::obstr_size_t>(file_size),
                                static_cast<ObString::obstr_size_t>(file_size),
                                file_buf);
          if (OB_FAIL(ObTextStringHelper::string_to_templob_result(expr, ctx, expr_datum, file_content))) {
            LOG_WARN("failed to set load_file blob result", K(ret), K(file_size));
          }
        }
      }
    }
  }

  if (OB_NOT_NULL(file_reader)) {
    ObFileReader::destroy(file_reader);
    file_reader = NULL;
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (rt_expr.arg_cnt_ != 2) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Incorrect parameter count in the call to native function load_file",
             K(ret), K(rt_expr.arg_cnt_));
  } else if (OB_ISNULL(rt_expr.args_) || OB_ISNULL(rt_expr.args_[0]) || OB_ISNULL(rt_expr.args_[1])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of load_file expr is null", K(ret), K(rt_expr.args_));
  } else {
    rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
