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
#include "lib/string/ob_sql_string.h"
#include "share/io/ob_backup_io_adapter.h"
#include "share/io/ob_backup_storage_info.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
namespace
{
static const char *LOAD_FILE_SAFE_PATH_ERROR =
    "LOAD_FILE file_name should be a non-empty safe relative path";

bool is_ascii_alpha(const char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_uri_scheme_char(const char c)
{
  return is_ascii_alpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

int hex_to_int(const char c)
{
  return (c >= '0' && c <= '9') ? c - '0'
      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
      : -1;
}

bool has_uri_scheme(const ObString &path)
{
  bool has_scheme = false;
  const char *ptr = path.ptr();
  const int64_t len = path.length();
  if (len > 1 && OB_NOT_NULL(ptr) && is_ascii_alpha(ptr[0])) {
    bool scheme_candidate = true;
    for (int64_t i = 1; !has_scheme && scheme_candidate && i < len; ++i) {
      if (':' == ptr[i]) {
        has_scheme = true;
      } else if ('/' == ptr[i]) {
        scheme_candidate = false;
      } else if (!is_uri_scheme_char(ptr[i])) {
        scheme_candidate = false;
      }
    }
  }
  return has_scheme;
}

int set_load_file_path_error(const ObString &path, const char *path_state, const char *reason)
{
  const int ret = OB_INVALID_ARGUMENT;
  LOG_WARN("invalid LOAD_FILE file_name", K(ret), K(path), K(path_state), K(reason));
  LOG_USER_ERROR(OB_INVALID_ARGUMENT, LOAD_FILE_SAFE_PATH_ERROR);
  return ret;
}

int check_load_file_path_segments(const ObString &path, const char *path_state)
{
  int ret = OB_SUCCESS;
  const char *ptr = path.ptr();
  const int64_t len = path.length();
  int64_t segment_start = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i <= len; ++i) {
    if (i == len || '/' == ptr[i]) {
      const int64_t segment_len = i - segment_start;
      if (1 == segment_len && '.' == ptr[segment_start]) {
        ret = set_load_file_path_error(path, path_state, "dot path segment");
      } else if (2 == segment_len && '.' == ptr[segment_start] && '.' == ptr[segment_start + 1]) {
        ret = set_load_file_path_error(path, path_state, "dot-dot path segment");
      } else {
        segment_start = i + 1;
      }
    }
  }
  return ret;
}

int check_load_file_relative_path_once(const ObString &path, const char *path_state)
{
  int ret = OB_SUCCESS;
  const char *ptr = path.ptr();
  const int64_t len = path.length();
  if (OB_ISNULL(ptr) || len <= 0) {
    ret = set_load_file_path_error(path, path_state, "empty path");
  } else if ('/' == ptr[0]) {
    ret = set_load_file_path_error(path, path_state, "absolute unix path");
  } else if (is_ascii_alpha(ptr[0]) && len >= 2 && ':' == ptr[1]) {
    ret = set_load_file_path_error(path, path_state, "windows drive path");
  } else if (has_uri_scheme(path)) {
    ret = set_load_file_path_error(path, path_state, "uri scheme");
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < len; ++i) {
      if ('\0' == ptr[i]) {
        ret = set_load_file_path_error(path, path_state, "nul byte");
      } else if ('\\' == ptr[i]) {
        ret = set_load_file_path_error(path, path_state, "backslash");
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_load_file_path_segments(path, path_state))) {
      LOG_WARN("failed to check LOAD_FILE path segments", K(ret), K(path), K(path_state));
    }
  }
  return ret;
}

int percent_decode_for_validation(const ObString &path,
                                  ObIAllocator &allocator,
                                  ObString &decoded_path,
                                  bool &has_decoded)
{
  int ret = OB_SUCCESS;
  const char *ptr = path.ptr();
  const int64_t len = path.length();
  char *buf = NULL;
  has_decoded = false;
  decoded_path.reset();
  for (int64_t i = 0; !has_decoded && i + 2 < len; ++i) {
    if ('%' == ptr[i] && hex_to_int(ptr[i + 1]) >= 0 && hex_to_int(ptr[i + 2]) >= 0) {
      has_decoded = true;
    }
  }
  if (!has_decoded) {
    decoded_path = path;
  } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate LOAD_FILE decoded path buffer", K(ret), K(len));
  } else {
    int64_t pos = 0;
    for (int64_t i = 0; i < len; ++i) {
      const int high = (i + 2 < len && '%' == ptr[i]) ? hex_to_int(ptr[i + 1]) : -1;
      const int low = high >= 0 ? hex_to_int(ptr[i + 2]) : -1;
      if (high >= 0 && low >= 0) {
        buf[pos++] = static_cast<char>((high << 4) + low);
        i += 2;
      } else {
        buf[pos++] = ptr[i];
      }
    }
    decoded_path.assign_ptr(buf, static_cast<int32_t>(pos));
  }
  return ret;
}

int check_load_file_relative_path(const ObString &path, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObString decoded_path;
  bool has_decoded = false;
  if (OB_FAIL(check_load_file_relative_path_once(path, "raw"))) {
    LOG_WARN("LOAD_FILE raw path is not safe", K(ret), K(path));
  } else if (OB_FAIL(percent_decode_for_validation(path, allocator, decoded_path, has_decoded))) {
    LOG_WARN("failed to percent-decode LOAD_FILE path for validation", K(ret), K(path));
  } else if (has_decoded && OB_FAIL(check_load_file_relative_path_once(decoded_path, "percent decoded"))) {
    LOG_WARN("LOAD_FILE percent decoded path is not safe", K(ret), K(path), K(decoded_path));
  }
  return ret;
}

} // namespace

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

  type.set_type(ObLongTextType);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  type.set_length(OB_MAX_LONGTEXT_LENGTH);
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_collation_type(CS_TYPE_BINARY);

  type1.set_calc_type(ObVarcharType);
  type1.set_calc_collation_type(ObCharset::get_system_collation());
  type2.set_calc_type(ObVarcharType);
  type2.set_calc_collation_type(ObCharset::get_system_collation());
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &op_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  if (OB_UNLIKELY(2 != rt_expr.arg_cnt_)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("LOAD_FILE expects two arguments", K(ret), K(rt_expr.arg_cnt_));
  } else {
    rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  }
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *file_datum = NULL;
  const ObSQLSessionInfo *session_info = NULL;
  share::schema::ObSessionPrivInfo session_priv;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObLocationSchema *location_schema = NULL;
  ObString location_name;
  ObString file_name;

  if (OB_UNLIKELY(2 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("LOAD_FILE expects two arguments", K(ret), K(expr.arg_cnt_));
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), KP(session_info), KP(GCTX.schema_service_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("evaluate LOAD_FILE parameters failed", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    expr_datum.set_null();
  } else if (FALSE_IT(location_name = location_datum->get_string())) {
  } else if (FALSE_IT(file_name = file_datum->get_string())) {
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
    LOG_WARN("failed to get session priv info", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
    LOG_WARN("failed to get location schema by name", K(ret), K(location_name));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_LOCATION_OBJ_NOT_EXIST;
    LOG_WARN("location object does not exist", K(ret), K(location_name));
    LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST,
                   static_cast<int>(location_name.length()),
                   location_name.ptr());
  } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                        session_info->get_enable_role_array(),
                                                        location_name,
                                                        false))) {
    LOG_WARN("failed to check location read access", K(ret), K(location_name));
  } else {
    const ObString &location_url = location_schema->get_location_url_str();
    const ObString &location_access_info = location_schema->get_location_access_info_str();
    const int64_t location_url_len = location_url.length();
    const int64_t location_access_info_len = location_access_info.length();
    share::ObBackupStorageInfo storage_info;
    ObSqlString file_uri_buf;
    ObEvalCtx::TempAllocGuard tmp_alloc_guard(ctx);
    int64_t file_length = -1;
    int64_t read_size = 0;
    const int64_t lob_max_load_file_length =
        OB_MAX_LONGTEXT_LENGTH - ObTextStringResult::MAX_TMP_LOB_HEADER_LEN;
    const int64_t expr_max_length = expr.max_length_ > 0
        ? static_cast<int64_t>(expr.max_length_)
        : lob_max_load_file_length;
    const int64_t max_load_file_length = MIN(lob_max_load_file_length, expr_max_length);
    const bool need_path_sep =
        location_url.length() > 0 && file_name.length() > 0
        && '/' != location_url.ptr()[location_url.length() - 1]
        && '/' != file_name.ptr()[0];

    if (OB_FAIL(check_load_file_relative_path(file_name, tmp_alloc_guard.get_allocator()))) {
      LOG_WARN("failed to check LOAD_FILE safe relative path", K(ret), K(location_name), K(file_name));
    } else if (OB_FAIL(storage_info.set(location_schema->get_location_url(),
                                        location_schema->get_location_access_info()))) {
      LOG_WARN("failed to set LOAD_FILE storage info",
               K(ret), K(location_name), K(location_url_len), K(location_access_info_len));
    } else if (OB_FAIL(file_uri_buf.append(location_url))) {
      LOG_WARN("failed to append LOAD_FILE location url", K(ret), K(location_name), K(location_url_len));
    } else if (need_path_sep && OB_FAIL(file_uri_buf.append("/"))) {
      LOG_WARN("failed to append LOAD_FILE path separator", K(ret), K(location_name), K(location_url_len), K(file_name));
    } else if (OB_FAIL(file_uri_buf.append(file_name))) {
      LOG_WARN("failed to append LOAD_FILE file name", K(ret), K(location_name), K(file_name));
    } else {
      const ObString file_uri = file_uri_buf.string();
      if (OB_FAIL(ObBackupIoAdapter::get_file_length(file_uri, &storage_info, file_length))) {
        LOG_WARN("failed to get LOAD_FILE file length", K(ret), K(location_name), K(file_name));
      } else if (OB_UNLIKELY(file_length < 0)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("LOAD_FILE file length is invalid", K(ret), K(location_name), K(file_name), K(file_length));
      } else if (OB_UNLIKELY(file_length > max_load_file_length)) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("LOAD_FILE file is too large",
                 K(ret), K(location_name), K(file_name), K(file_length), K(max_load_file_length));
      } else if (0 == file_length) {
        expr_datum.set_string(NULL, 0);
      } else {
        char *read_buf = static_cast<char *>(tmp_alloc_guard.get_allocator().alloc(file_length));
        if (OB_ISNULL(read_buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate LOAD_FILE read buffer", K(ret), K(location_name), K(file_name), K(file_length));
        } else if (OB_FAIL(ObBackupIoAdapter::read_single_file(file_uri,
                                                               &storage_info,
                                                               read_buf,
                                                               file_length,
                                                               read_size,
                                                               ObStorageIdMod::get_default_id_mod()))) {
          LOG_WARN("failed to read LOAD_FILE file",
                   K(ret), K(location_name), K(file_name), K(file_length), K(read_size));
        } else if (OB_UNLIKELY(read_size != file_length)) {
          ret = OB_BUF_NOT_ENOUGH;
          LOG_WARN("LOAD_FILE read size does not match file length",
                   K(ret), K(location_name), K(file_name), K(file_length), K(read_size));
        } else {
          ObTextStringDatumResult result(expr.datum_meta_.type_, &expr, &ctx, &expr_datum);
          if (OB_FAIL(result.init(file_length))) {
            LOG_WARN("failed to init LOAD_FILE result", K(ret), K(location_name), K(file_name), K(file_length));
          } else if (OB_FAIL(result.append(read_buf, read_size))) {
            LOG_WARN("failed to append LOAD_FILE result", K(ret), K(location_name), K(file_name), K(read_size));
          } else {
            result.set_result();
          }
        }
      }
    }
  }
  return ret;
}

}
}
