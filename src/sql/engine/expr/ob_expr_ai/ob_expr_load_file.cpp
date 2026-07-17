/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_ai/ob_expr_load_file.h"

#include "share/io/ob_backup_io_adapter.h"
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
  if ((!ob_is_string_tc(location_type.get_type()) && !location_type.is_null())
      || (!ob_is_string_tc(file_type.get_type()) && !file_type.is_null())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("load_file arguments must be strings", K(ret), K(location_type), K(file_type));
  } else {
    location_type.set_calc_type(ObVarcharType);
    location_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    file_type.set_calc_type(ObVarcharType);
    file_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    type.set_blob();
    type.set_collation_type(CS_TYPE_BINARY);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
    type.set_length(OB_MAX_LONGTEXT_LENGTH);
  }
  return ret;
}

bool ObExprLoadFile::is_safe_relative_file_name(const ObString &file_name)
{
  bool safe = !file_name.empty() && file_name.ptr()[0] != '/' && file_name.ptr()[0] != '\\';
  int64_t component_start = 0;
  for (int64_t i = 0; safe && i <= file_name.length(); ++i) {
    const bool at_end = i == file_name.length();
    const char ch = at_end ? '/' : file_name.ptr()[i];
    if (ch == '\\') {
      safe = false;
    } else if (ch == '/') {
      const int64_t component_len = i - component_start;
      if (component_len == 0
          || (component_len == 2
              && file_name.ptr()[component_start] == '.'
              && file_name.ptr()[component_start + 1] == '.')) {
        safe = false;
      }
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
  const ObLocationSchema *location_schema = nullptr;
  share::schema::ObSessionPrivInfo session_priv;

  if (OB_ISNULL(session) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret));
  } else if (OB_UNLIKELY(expr.arg_cnt_ != 2)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected load_file argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_datum))
             || OB_FAIL(expr.args_[1]->eval(ctx, file_datum))) {
    LOG_WARN("failed to evaluate load_file arguments", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else {
    const ObString location_name = location_datum->get_string();
    const ObString file_name = file_datum->get_string();
    if (location_name.empty() || !is_safe_relative_file_name(file_name)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file requires a non-empty location and a safe relative file name");
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("failed to get tenant schema guard", K(ret));
    } else if (OB_FAIL(session->get_session_priv_info(session_priv))) {
      LOG_WARN("failed to get session privilege info", K(ret));
    } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                          session->get_enable_role_array(),
                                                          location_name,
                                                          false))) {
      LOG_WARN("location read access denied", K(ret), K(location_name));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to get location schema", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_LOCATION_OBJ_NOT_EXIST;
      LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST, location_name.length(), location_name.ptr());
    } else {
      const ObString &location_url = location_schema->get_location_url_str();
      static const ObString FILE_PREFIX("file://");
      if (location_url.length() < FILE_PREFIX.length()
          || 0 != MEMCMP(location_url.ptr(), FILE_PREFIX.ptr(), FILE_PREFIX.length())) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file currently supports file:// locations only");
      } else {
        ObEvalCtx::TempAllocGuard tmp_alloc_guard(ctx);
        ObIAllocator &tmp_alloc = tmp_alloc_guard.get_allocator();
        const bool need_separator = location_url.ptr()[location_url.length() - 1] != '/';
        const int64_t uri_len = location_url.length() + (need_separator ? 1 : 0) + file_name.length();
        char *uri_buf = static_cast<char *>(tmp_alloc.alloc(uri_len));
        if (OB_ISNULL(uri_buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate load_file uri", K(ret), K(uri_len));
        } else {
          int64_t pos = 0;
          MEMCPY(uri_buf + pos, location_url.ptr(), location_url.length());
          pos += location_url.length();
          if (need_separator) {
            uri_buf[pos++] = '/';
          }
          MEMCPY(uri_buf + pos, file_name.ptr(), file_name.length());
          const ObString uri(uri_len, uri_buf);
          ObBackupIoAdapter io_adapter;
          ObObjectStorageInfo file_storage_info;
          int64_t file_length = 0;
          int64_t max_allowed_packet = 0;
          if (OB_FAIL(file_storage_info.set(OB_STORAGE_FILE, ""))) {
            LOG_WARN("failed to initialize local file storage info", K(ret));
          } else if (OB_FAIL(io_adapter.get_file_length(uri, &file_storage_info, file_length))) {
            LOG_WARN("failed to get file length", K(ret), K(uri));
          } else if (file_length < 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("invalid file length", K(ret), K(file_length), K(uri));
          } else if (OB_FAIL(session->get_max_allowed_packet(max_allowed_packet))) {
            LOG_WARN("failed to get max_allowed_packet", K(ret));
          } else if (file_length > max_allowed_packet || file_length > OB_MAX_LONGTEXT_LENGTH) {
            ret = OB_ERR_FUNC_RESULT_TOO_LARGE;
            LOG_USER_ERROR(OB_ERR_FUNC_RESULT_TOO_LARGE,
                           N_LOAD_FILE,
                           static_cast<int>(MIN(max_allowed_packet, static_cast<int64_t>(OB_MAX_LONGTEXT_LENGTH))));
          } else {
            ObTextStringDatumResult output(expr.datum_meta_.type_, &expr, &ctx, &res);
            char *file_buf = nullptr;
            int64_t reserved_size = 0;
            int64_t read_size = 0;
            if (OB_FAIL(output.init(file_length))) {
              LOG_WARN("failed to initialize load_file blob result", K(ret), K(file_length));
            } else if (file_length > 0
                       && OB_FAIL(output.get_reserved_buffer(file_buf, reserved_size))) {
              LOG_WARN("failed to reserve load_file result buffer", K(ret), K(file_length));
            } else if (file_length > 0
                       && OB_FAIL(io_adapter.read_single_file(uri,
                                                             &file_storage_info,
                                                             file_buf,
                                                             reserved_size,
                                                             read_size,
                                                             ObStorageIdMod::get_default_id_mod()))) {
              LOG_WARN("failed to read location file", K(ret), K(uri));
            } else if (file_length > 0 && read_size != file_length) {
              ret = OB_IO_ERROR;
              LOG_WARN("load_file did not read the complete file", K(ret), K(read_size), K(file_length));
            } else if (OB_FAIL(output.lseek(file_length, 0))) {
              LOG_WARN("failed to set load_file result length", K(ret), K(file_length));
            } else {
              output.set_result();
            }
          }
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
