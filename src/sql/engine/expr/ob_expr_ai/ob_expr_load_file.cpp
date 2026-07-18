/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_ai/ob_expr_load_file.h"
#include <cstdio>
#include <cstring>
#include "lib/string/ob_string.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

namespace
{
bool filename_has_parent_dir(const ObString &filename)
{
  bool has_parent_dir = false;
  for (int64_t i = 0; !has_parent_dir && i + 1 < filename.length(); ++i) {
    has_parent_dir = filename.ptr()[i] == '.' && filename.ptr()[i + 1] == '.';
  }
  return has_parent_dir;
}
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
                                      ObExprResType &location,
                                      ObExprResType &filename,
                                      ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  type.set_type(ObLongTextType);
  type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  location.set_calc_type(ObVarcharType);
  location.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  filename.set_calc_type(ObVarcharType);
  filename.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  return ret;
}

int ObExprLoadFile::build_local_path(ObIAllocator &allocator,
                                     const ObString &base_url,
                                     const ObString &filename,
                                     ObString &path)
{
  int ret = OB_SUCCESS;
  static const char FILE_PREFIX[] = "file://";
  static const int64_t FILE_PREFIX_LEN = sizeof(FILE_PREFIX) - 1;
  if (base_url.length() < FILE_PREFIX_LEN
      || 0 != MEMCMP(base_url.ptr(), FILE_PREFIX, FILE_PREFIX_LEN)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("load_file only supports file location", K(ret), K(base_url));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file with non-file location");
  } else if (filename.empty() || filename.ptr()[0] == '/'
             || filename_has_parent_dir(filename)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid load_file filename", K(ret), K(filename));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file filename");
  } else {
    const char *base = base_url.ptr() + FILE_PREFIX_LEN;
    const int64_t base_len = base_url.length() - FILE_PREFIX_LEN;
    const bool need_slash = (base_len > 0 && base[base_len - 1] != '/');
    const int64_t path_len = base_len + (need_slash ? 1 : 0) + filename.length();
    char *buf = static_cast<char *>(allocator.alloc(path_len + 1));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc path", K(ret), K(path_len));
    } else {
      MEMCPY(buf, base, base_len);
      int64_t pos = base_len;
      if (need_slash) {
        buf[pos++] = '/';
      }
      MEMCPY(buf + pos, filename.ptr(), filename.length());
      pos += filename.length();
      buf[pos] = '\0';
      path.assign_ptr(buf, static_cast<int32_t>(pos));
    }
  }
  return ret;
}

int ObExprLoadFile::read_file_to_datum(const ObExpr &expr,
                                       ObEvalCtx &ctx,
                                       const ObString &path,
                                       ObDatum &res)
{
  int ret = OB_SUCCESS;
  FILE *fp = NULL;
  if (OB_ISNULL(fp = fopen(path.ptr(), "rb"))) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to open load_file path", K(ret), K(path));
  } else if (0 != fseek(fp, 0, SEEK_END)) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to seek load_file path", K(ret), K(path));
  } else {
    const long file_size = ftell(fp);
    if (file_size < 0) {
      ret = OB_IO_ERROR;
      LOG_WARN("failed to tell load_file path", K(ret), K(path));
    } else if (0 != fseek(fp, 0, SEEK_SET)) {
      ret = OB_IO_ERROR;
      LOG_WARN("failed to rewind load_file path", K(ret), K(path));
    } else {
      ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &res);
      char *buf = NULL;
      int64_t reserve_len = 0;
      if (OB_FAIL(output_result.init(file_size))) {
        LOG_WARN("failed to init load_file result", K(ret), K(file_size));
      } else if (file_size > 0
                 && OB_FAIL(output_result.get_reserved_buffer(buf, reserve_len))) {
        LOG_WARN("failed to get reserved buffer", K(ret), K(file_size));
      } else if (file_size > 0 && reserve_len != file_size) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("reserved buffer length mismatch", K(ret), K(reserve_len), K(file_size));
      } else {
        const size_t read_size = file_size > 0 ? fread(buf, 1, file_size, fp) : 0;
        if (read_size != static_cast<size_t>(file_size)) {
          ret = OB_IO_ERROR;
          LOG_WARN("failed to read load_file path", K(ret), K(path), K(read_size), K(file_size));
        } else if (file_size > 0 && OB_FAIL(output_result.lseek(file_size, 0))) {
          LOG_WARN("failed to lseek load_file result", K(ret), K(file_size));
        } else {
          output_result.set_result();
        }
      }
    }
  }
  if (NULL != fp) {
    fclose(fp);
  }
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *filename_datum = NULL;
  ObSQLSessionInfo *session_info = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = NULL;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  ObString path;
  if (OB_UNLIKELY(2 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid load_file arg count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, filename_datum))) {
    LOG_WARN("failed to eval load_file args", K(ret));
  } else if (location_datum->is_null() || filename_datum->is_null()) {
    res.set_null();
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session())
             || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get session or schema service", K(ret), KP(session_info), KP(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_datum->get_string(), location_schema))) {
    LOG_WARN("failed to get location schema", K(ret), K(location_datum->get_string()));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("location does not exist", K(ret), K(location_datum->get_string()));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file location");
  } else if (OB_FAIL(build_local_path(tmp_alloc_g.get_allocator(),
                                      location_schema->get_location_url_str(),
                                      filename_datum->get_string(),
                                      path))) {
    LOG_WARN("failed to build local path", K(ret));
  } else if (OB_FAIL(read_file_to_datum(expr, ctx, path, res))) {
    LOG_WARN("failed to read file", K(ret), K(path));
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
