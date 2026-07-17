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

#include "sql/engine/expr/ob_expr_ai/ob_expr_load_file.h"

#include <limits.h>
#include "lib/file/file_directory_utils.h"
#include "lib/file/ob_file.h"
#include "lib/string/ob_sql_string.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

static const char *LOAD_FILE_FUNC_NAME = "load_file";
static const char *LOCAL_FILE_SCHEME = "file://";
static const int64_t LOCAL_FILE_SCHEME_LEN = 7;

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc,
                       T_FUN_SYS_LOAD_FILE,
                       LOAD_FILE_FUNC_NAME,
                       2,
                       NOT_VALID_FOR_GENERATED_COL,
                       NOT_ROW_DIMENSION)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location_type,
                                      ObExprResType &file_name_type,
                                      ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (!ob_is_string_tc(location_type.get_type()) || !ob_is_string_tc(file_name_type.get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("load_file arguments must be strings", K(ret),
             K(location_type.get_type()), K(file_name_type.get_type()));
  } else {
    location_type.set_calc_type(ObVarcharType);
    location_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    file_name_type.set_calc_type(ObVarcharType);
    file_name_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_blob();
    type.set_length(OB_MAX_BLOB_WIDTH);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

static bool is_safe_relative_file_name(const ObString &file_name)
{
  bool safe = !file_name.empty()
              && '/' != file_name[0]
              && '\\' != file_name[0]
              && NULL == MEMCHR(file_name.ptr(), '\0', file_name.length());
  int64_t component_start = 0;
  for (int64_t i = 0; safe && i <= file_name.length(); ++i) {
    if (i == file_name.length() || '/' == file_name[i] || '\\' == file_name[i]) {
      const int64_t component_len = i - component_start;
      safe = component_len > 0
             && !(1 == component_len && '.' == file_name[component_start])
             && !(2 == component_len
                  && '.' == file_name[component_start]
                  && '.' == file_name[component_start + 1]);
      component_start = i + 1;
    }
  }
  return safe;
}

static int build_checked_local_file_path(const ObString &location_url,
                                         const ObString &file_name,
                                         ObSqlString &checked_path)
{
  int ret = OB_SUCCESS;
  ObSqlString base_path;
  ObSqlString candidate_path;
  char base_real_path[PATH_MAX + 1];
  char file_real_path[PATH_MAX + 1];
  char *base_actual_path = NULL;
  char *file_actual_path = NULL;
  if (location_url.length() <= LOCAL_FILE_SCHEME_LEN
      || 0 != MEMCMP(location_url.ptr(), LOCAL_FILE_SCHEME, LOCAL_FILE_SCHEME_LEN)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("load_file only supports local file locations", K(ret), K(location_url));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "LOAD_FILE with a non-local LOCATION");
  } else if (!is_safe_relative_file_name(file_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid load_file file name", K(ret), K(file_name));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "LOAD_FILE file_name must be a safe relative path");
  } else if (OB_FAIL(base_path.append(ObString(location_url.length() - LOCAL_FILE_SCHEME_LEN,
                                                location_url.ptr() + LOCAL_FILE_SCHEME_LEN)))) {
    LOG_WARN("failed to build location base path", K(ret));
  } else if (OB_FAIL(candidate_path.append(base_path.string()))) {
    LOG_WARN("failed to append location base path", K(ret));
  } else if (!candidate_path.empty()
             && '/' != candidate_path.ptr()[candidate_path.length() - 1]
             && OB_FAIL(candidate_path.append("/"))) {
    LOG_WARN("failed to append path separator", K(ret));
  } else if (OB_FAIL(candidate_path.append(file_name))) {
    LOG_WARN("failed to append file name", K(ret));
  }
  if (OB_SUCC(ret)) {
#ifdef _WIN32
    base_actual_path = _fullpath(base_real_path, base_path.ptr(), PATH_MAX);
    file_actual_path = _fullpath(file_real_path, candidate_path.ptr(), PATH_MAX);
#else
    base_actual_path = realpath(base_path.ptr(), base_real_path);
    file_actual_path = realpath(candidate_path.ptr(), file_real_path);
#endif
    if (OB_ISNULL(base_actual_path) || OB_ISNULL(file_actual_path)) {
      ret = OB_FILE_NOT_EXIST;
      LOG_WARN("load_file path does not exist", K(ret), K(base_path), K(candidate_path));
    } else {
      const int64_t base_len = STRLEN(base_actual_path);
      const int64_t file_len = STRLEN(file_actual_path);
#ifdef _WIN32
      const bool base_is_root = 3 == base_len
                                && ':' == base_actual_path[1]
                                && ('\\' == base_actual_path[2] || '/' == base_actual_path[2]);
      const bool contained = file_len > base_len
                             && 0 == _strnicmp(base_actual_path, file_actual_path, base_len)
                             && (base_is_root
                                 || '\\' == file_actual_path[base_len]
                                 || '/' == file_actual_path[base_len]);
#else
      const bool base_is_root = 1 == base_len && '/' == base_actual_path[0];
      const bool contained = file_len > base_len
                             && 0 == MEMCMP(base_actual_path, file_actual_path, base_len)
                             && (base_is_root || '/' == file_actual_path[base_len]);
#endif
      if (!contained) {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_WARN("load_file path escapes its location", K(ret),
                 KCSTRING(base_actual_path), KCSTRING(file_actual_path));
      } else if (OB_FAIL(checked_path.append(file_actual_path))) {
        LOG_WARN("failed to save checked path", K(ret));
      }
    }
  }
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *file_name_datum = NULL;
  const ObSQLSessionInfo *session_info = NULL;
  const ObLocationSchema *location_schema = NULL;
  ObSchemaGetterGuard schema_guard;
  ObSqlString checked_path;
  int64_t file_size = 0;
  if (OB_UNLIKELY(2 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected load_file argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session())
             || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get session or schema service", K(ret));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_datum))
             || OB_FAIL(expr.args_[1]->eval(ctx, file_name_datum))) {
    LOG_WARN("failed to evaluate load_file arguments", K(ret));
  } else if (location_datum->is_null() || file_name_datum->is_null()) {
    res.set_null();
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_datum->get_string(),
                                                               location_schema))) {
    LOG_WARN("failed to get location schema", K(ret), K(location_datum->get_string()));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_LOCATION_OBJ_NOT_EXIST;
    LOG_WARN("location does not exist", K(ret), K(location_datum->get_string()));
  } else if (OB_FAIL(build_checked_local_file_path(location_schema->get_location_url_str(),
                                                   file_name_datum->get_string(),
                                                   checked_path))) {
    LOG_WARN("failed to resolve local file path", K(ret));
  } else if (OB_FAIL(FileDirectoryUtils::get_file_size(checked_path.ptr(), file_size))) {
    LOG_WARN("failed to get file size", K(ret), K(checked_path));
  } else if (file_size < 0 || file_size > OB_MAX_BLOB_WIDTH) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("load_file result is too large", K(ret), K(file_size));
  } else {
    ObFileReader file_reader;
    ObTextStringDatumResult output(expr.datum_meta_.type_, &expr, &ctx, &res);
    char *buffer = NULL;
    int64_t buffer_size = 0;
    int64_t read_size = 0;
    if (OB_FAIL(output.init(file_size))) {
      LOG_WARN("failed to initialize load_file result", K(ret), K(file_size));
    } else if (file_size > 0 && OB_FAIL(output.get_reserved_buffer(buffer, buffer_size))) {
      LOG_WARN("failed to reserve load_file result buffer", K(ret), K(file_size));
    } else if (OB_FAIL(file_reader.open(checked_path.string(), false))) {
      LOG_WARN("failed to open local file", K(ret), K(checked_path));
    } else if (file_size > 0
               && OB_FAIL(file_reader.pread(buffer, file_size, 0, read_size))) {
      LOG_WARN("failed to read local file", K(ret), K(checked_path), K(file_size));
    } else if (read_size != file_size) {
      ret = OB_IO_ERROR;
      LOG_WARN("local file size changed while reading", K(ret), K(file_size), K(read_size));
    } else if (OB_FAIL(output.lseek(file_size, 0))) {
      LOG_WARN("failed to advance load_file result", K(ret), K(file_size));
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
