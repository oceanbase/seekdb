/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_load_file.h"

#include "lib/charset/ob_charset.h"
#include "lib/string/ob_sql_string.h"
#include "share/io/ob_backup_io_adapter.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace share;
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
  type.set_blob();
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_length(OB_MAX_LONGTEXT_LENGTH);
  location_type.set_calc_type(ObVarcharType);
  location_type.set_calc_collation_type(ObCharset::get_system_collation());
  file_type.set_calc_type(ObVarcharType);
  file_type.set_calc_collation_type(ObCharset::get_system_collation());
  return OB_SUCCESS;
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

static bool is_safe_relative_file_name(const ObString &file_name)
{
  bool safe = !file_name.empty() && file_name.ptr()[0] != '/';
  for (int64_t i = 0; safe && i < file_name.length(); ++i) {
    if (file_name.ptr()[i] == '\\') {
      safe = false;
    }
  }
  // Reject a parent-directory path component while still allowing names such as a..b.
  for (int64_t i = 0; safe && i + 1 < file_name.length(); ++i) {
    if (file_name.ptr()[i] == '.' && file_name.ptr()[i + 1] == '.'
        && (i == 0 || file_name.ptr()[i - 1] == '/')
        && (i + 2 == file_name.length() || file_name.ptr()[i + 2] == '/')) {
      safe = false;
    }
  }
  return safe;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = nullptr;
  ObDatum *file_datum = nullptr;
  const ObSQLSessionInfo *session = nullptr;
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = nullptr;
  ObSessionPrivInfo session_priv;

  if (OB_UNLIKELY(expr.arg_cnt_ != 2)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid load_file argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_datum))) {
    LOG_WARN("failed to evaluate location name", K(ret));
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, file_datum))) {
    LOG_WARN("failed to evaluate file name", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    result.set_null();
  } else if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session()) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret));
  } else if (!is_safe_relative_file_name(file_datum->get_string())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("load_file requires a safe relative file name", K(ret), K(file_datum->get_string()));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_datum->get_string(), location_schema))) {
    LOG_WARN("failed to get location schema", K(ret), K(location_datum->get_string()));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_LOCATION_OBJ_NOT_EXIST;
    LOG_WARN("location does not exist", K(ret), K(location_datum->get_string()));
  } else if (OB_FAIL(session->get_session_priv_info(session_priv))) {
    LOG_WARN("failed to get session privilege info", K(ret));
  } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                        session->get_enable_role_array(),
                                                        location_datum->get_string(),
                                                        false))) {
    LOG_WARN("location read access denied", K(ret), K(location_datum->get_string()));
  } else {
    const ObString &base = location_schema->get_location_url_str();
    const ObString &file_name = file_datum->get_string();
    ObSqlString uri_builder;
    if (OB_FAIL(uri_builder.append(base))) {
      LOG_WARN("failed to append location url", K(ret));
    } else if (!base.empty() && base.ptr()[base.length() - 1] != '/'
               && OB_FAIL(uri_builder.append("/"))) {
      LOG_WARN("failed to append path separator", K(ret));
    } else if (OB_FAIL(uri_builder.append(file_name))) {
      LOG_WARN("failed to append file name", K(ret));
    } else {
      const ObString uri(uri_builder.length(), uri_builder.ptr());
      int64_t file_length = 0;
      int64_t read_size = 0;
      ObBackupIoAdapter io_adapter;
      ObObjectStorageInfo storage_info;
      if (OB_FAIL(storage_info.set(OB_STORAGE_FILE, ""))) {
        LOG_WARN("failed to initialize local file storage info", K(ret));
      } else if (OB_FAIL(io_adapter.adaptively_get_file_length(uri, &storage_info, file_length))) {
        LOG_WARN("failed to get file length", K(ret), K(uri));
      } else if (OB_UNLIKELY(file_length < 0 || file_length > OB_MAX_LONGTEXT_LENGTH)) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("load_file file is too large", K(ret), K(file_length));
      } else {
        ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &result);
        if (OB_FAIL(output_result.init(file_length))) {
          LOG_WARN("failed to initialize load_file BLOB result", K(ret), K(file_length));
        } else if (file_length == 0) {
          output_result.set_result();
        } else {
          char *buf = nullptr;
          int64_t buf_size = 0;
          if (OB_FAIL(output_result.get_reserved_buffer(buf, buf_size))) {
            LOG_WARN("failed to reserve load_file BLOB buffer", K(ret), K(file_length));
          } else if (OB_UNLIKELY(buf_size < file_length)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("load_file result buffer is too small", K(ret), K(buf_size), K(file_length));
          } else if (OB_FAIL(io_adapter.adaptively_read_single_file(
                         uri, &storage_info, buf, file_length, read_size,
                         ObStorageIdMod::get_default_id_mod()))) {
            LOG_WARN("failed to read file", K(ret), K(uri), K(file_length));
          } else if (OB_UNLIKELY(read_size != file_length)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected file read size", K(ret), K(read_size), K(file_length));
          } else if (OB_FAIL(output_result.lseek(read_size, 0))) {
            LOG_WARN("failed to finalize load_file BLOB length", K(ret), K(read_size));
          } else {
            output_result.set_result();
          }
        }
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
