/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_load_file.h"

#include <fstream>
#include <iterator>
#include <string>

#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
  : ObStringExprOperator(alloc, T_FUN_SYS_LOAD_FILE, N_LOAD_FILE, 2,
                         NOT_VALID_FOR_GENERATED_COL)
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
  type.set_blob();
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  type1.set_calc_type(ObVarcharType);
  type1.set_calc_collation_type(ObCharset::get_system_collation());
  type2.set_calc_type(ObVarcharType);
  type2.set_calc_collation_type(ObCharset::get_system_collation());
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *location_name = NULL;
  ObDatum *file_name = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = NULL;

  if (OB_UNLIKELY(expr.arg_cnt_ != 2)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid load_file argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_name))
             || OB_FAIL(expr.args_[1]->eval(ctx, file_name))) {
    LOG_WARN("failed to eval load_file arguments", K(ret));
  } else if (location_name->is_null() || file_name->is_null()) {
    expr_datum.set_null();
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name->get_string(),
                                                              location_schema))) {
    LOG_WARN("failed to get location schema", K(ret), K(location_name->get_string()));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("location schema is null", K(ret));
  } else {
    const ObString url = location_schema->get_location_url_str();
    const ObString name = file_name->get_string();
    const char *file_prefix = "file://";
    const int64_t file_prefix_len = 7;
    if (url.length() < file_prefix_len
        || 0 != STRNCASECMP(url.ptr(), file_prefix, file_prefix_len)) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file only supports file:// location");
    } else {
      std::string path(url.ptr() + file_prefix_len, url.length() - file_prefix_len);
      if (!path.empty() && path[path.length() - 1] != '/' && path[path.length() - 1] != '\\') {
        path.append("/");
      }
      path.append(name.ptr(), name.length());

      std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
      if (!input.good()) {
        ret = OB_IO_ERROR;
        LOG_WARN("failed to open load_file path", K(ret), K(path.c_str()));
      } else {
        std::string content((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
        const ObString file_content(static_cast<ObString::obstr_size_t>(content.length()),
                                    content.data());
        if (OB_FAIL(ObExprUtil::set_expr_ascii_result(expr, ctx, expr_datum,
                                                      file_content, true, CS_TYPE_BINARY))) {
          LOG_WARN("failed to build load_file lob result", K(ret), K(content.length()));
        }
      }
    }
  }
  return ret;
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

}
}
