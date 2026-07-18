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

#include "ob_expr_load_file.h"
#include "lib/restore/ob_storage.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
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
  int ret = OB_SUCCESS;
  if (!ob_is_string_tc(location_type.get_type()) || !ob_is_string_tc(file_type.get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("load_file arguments must be strings", K(ret),
             K(location_type.get_type()), K(file_type.get_type()));
  } else {
    location_type.set_calc_type(ObVarcharType);
    location_type.set_calc_collation_type(ObCharset::get_system_collation());
    file_type.set_calc_type(ObVarcharType);
    file_type.set_calc_collation_type(ObCharset::get_system_collation());
    type.set_blob();
    type.set_collation_type(CS_TYPE_BINARY);
    type.set_length(OB_MAX_LONGTEXT_LENGTH);
  }
  return ret;
}

bool ObExprLoadFile::is_safe_relative_file_name(const ObString &file_name)
{
  bool safe = !file_name.empty() && '/' != file_name.ptr()[0] && '\\' != file_name.ptr()[0];
  int64_t segment_start = 0;
  for (int64_t i = 0; safe && i <= file_name.length(); ++i) {
    if (i == file_name.length() || '/' == file_name.ptr()[i] || '\\' == file_name.ptr()[i]) {
      const int64_t segment_len = i - segment_start;
      safe = segment_len > 0
          && !(1 == segment_len && '.' == file_name.ptr()[segment_start])
          && !(2 == segment_len && '.' == file_name.ptr()[segment_start]
              && '.' == file_name.ptr()[segment_start + 1]);
      segment_start = i + 1;
    } else if ('\0' == file_name.ptr()[i]) {
      safe = false;
    }
  }
  return safe;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  int close_ret = OB_SUCCESS;
  ObDatum *location_datum = nullptr;
  ObDatum *file_datum = nullptr;
  const ObSQLSessionInfo *session_info = nullptr;
  const ObLocationSchema *location_schema = nullptr;
  ObSchemaGetterGuard schema_guard;
  ObSessionPrivInfo session_priv;
  ObStorageReader reader;
  ObObjectStorageInfo storage_info;
  ObSqlString uri;
  bool reader_opened = false;

  if (OB_UNLIKELY(2 != expr.arg_cnt_)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("invalid load_file argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_datum))
             || OB_FAIL(expr.args_[1]->eval(ctx, file_datum))) {
    LOG_WARN("failed to evaluate load_file arguments", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session())
             || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret));
  } else {
    const ObString location_name = location_datum->get_string();
    const ObString file_name = file_datum->get_string();
    if (!is_safe_relative_file_name(file_name)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("load_file requires a safe relative file name", K(ret), K(file_name));
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "file_name must be a safe relative path");
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("failed to get tenant schema guard", K(ret));
    } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
      LOG_WARN("failed to get session privilege info", K(ret));
    } else if (OB_FAIL(schema_guard.check_location_access(
                 session_priv, session_info->get_enable_role_array(), location_name, false))) {
      LOG_WARN("no read privilege on location", K(ret), K(location_name));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to find location", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("location schema is null", K(ret), K(location_name));
    } else {
      const ObString &base_url = location_schema->get_location_url_str();
      if (!base_url.prefix_match(OB_FILE_PREFIX)) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("load_file currently supports local file locations only", K(ret), K(base_url));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "non-file LOCATION in load_file");
      } else if (OB_FAIL(uri.append(base_url))) {
        LOG_WARN("failed to append location url", K(ret));
      } else if (!base_url.empty() && '/' != base_url.ptr()[base_url.length() - 1]
                 && OB_FAIL(uri.append("/"))) {
        LOG_WARN("failed to append path separator", K(ret));
      } else if (OB_FAIL(uri.append(file_name))) {
        LOG_WARN("failed to append file name", K(ret));
      } else if (OB_FAIL(storage_info.set(uri.ptr(), location_schema->get_location_access_info()))) {
        LOG_WARN("failed to initialize storage info", K(ret), K(uri));
      } else if (OB_FAIL(reader.open(uri.string(), &storage_info))) {
        LOG_WARN("failed to open location file", K(ret), K(uri));
      } else {
        reader_opened = true;
        const int64_t file_length = reader.get_length();
        if (OB_UNLIKELY(file_length < 0 || file_length > OB_MAX_LONGTEXT_LENGTH)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("load_file result is too large", K(ret), K(file_length));
        } else {
          ObEvalCtx::TempAllocGuard alloc_guard(ctx);
          char *buf = nullptr;
          int64_t read_size = 0;
          if (file_length > 0
              && OB_ISNULL(buf = static_cast<char *>(alloc_guard.get_allocator().alloc(file_length)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate load_file buffer", K(ret), K(file_length));
          } else if (file_length > 0
                     && OB_FAIL(reader.pread(buf, file_length, 0, read_size))) {
            LOG_WARN("failed to read location file", K(ret), K(uri), K(file_length));
          } else if (OB_UNLIKELY(read_size != file_length)) {
            ret = OB_IO_ERROR;
            LOG_WARN("short read from location file", K(ret), K(read_size), K(file_length));
          } else {
            ObTextStringDatumResult result(expr.datum_meta_.type_, &expr, &ctx, &res);
            if (OB_FAIL(result.init(file_length))) {
              LOG_WARN("failed to initialize blob result", K(ret), K(file_length));
            } else if (file_length > 0 && OB_FAIL(result.append(buf, file_length))) {
              LOG_WARN("failed to append blob result", K(ret), K(file_length));
            } else {
              result.set_result();
            }
          }
        }
      }
    }
  }

  if (reader_opened && OB_SUCCESS != (close_ret = reader.close())) {
    LOG_WARN("failed to close location file", K(close_ret));
    if (OB_SUCC(ret)) {
      ret = close_ret;
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
  rt_expr.eval_func_ = eval_load_file;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
