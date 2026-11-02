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
#include "lib/utility/utility.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

static const char *FILE_URL_PREFIX = "file://";

ObExprLoadFile::ObExprLoadFile(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_LOAD_FILE, N_LOAD_FILE, 2,
                        NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprLoadFile::~ObExprLoadFile()
{
}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location_type,
                                      ObExprResType &file_type,
                                      common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  location_type.set_calc_type(ObVarcharType);
  location_type.set_calc_collation_type(ObCharset::get_system_collation());
  file_type.set_calc_type(ObVarcharType);
  file_type.set_calc_collation_type(ObCharset::get_system_collation());
  type.set_blob();
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_length(OB_MAX_BLOB_WIDTH);
  return OB_SUCCESS;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *file_datum = NULL;
  const ObSQLSessionInfo *session_info = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = NULL;

  if (OB_UNLIKELY(2 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected argument count", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, location_datum))) {
    LOG_WARN("failed to evaluate location name", K(ret));
  } else if (OB_FAIL(expr.args_[1]->eval(ctx, file_datum))) {
    LOG_WARN("failed to evaluate file name", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) ||
             OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret), KP(session_info), KP(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else {
    const ObString location_name = location_datum->get_string();
    const ObString file_name = file_datum->get_string();
    if (location_name.empty() || file_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("location name or file name is empty", K(ret), K(location_name), K(file_name));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to get location schema", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_LOCATION_OBJ_NOT_EXIST;
      LOG_WARN("location does not exist", K(ret), K(location_name));
    } else {
      const ObString location_url = location_schema->get_location_url_str();
      const int64_t prefix_len = static_cast<int64_t>(STRLEN(FILE_URL_PREFIX));
      ObSqlString full_path;
      ObEvalCtx::TempAllocGuard tmp_alloc_guard(ctx);
      ObString file_content;

      if (!location_url.prefix_match(FILE_URL_PREFIX)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("only file URL locations are supported", K(ret), K(location_url));
      } else {
        const ObString root_path(location_url.length() - prefix_len,
                                 location_url.ptr() + prefix_len);
        const bool has_separator = !root_path.empty() &&
            ('/' == root_path.ptr()[root_path.length() - 1] ||
             '\\' == root_path.ptr()[root_path.length() - 1]);
        if (OB_FAIL(full_path.append_fmt("%.*s%s%.*s",
                                         root_path.length(), root_path.ptr(),
                                         has_separator ? "" : "/",
                                         file_name.length(), file_name.ptr()))) {
          LOG_WARN("failed to build file path", K(ret));
        } else if (OB_FAIL(load_file_to_string(full_path.ptr(),
                                               tmp_alloc_guard.get_allocator(),
                                               file_content))) {
          LOG_WARN("failed to load file", K(ret), K(full_path));
        } else {
          ObTextStringDatumResult result(expr.datum_meta_.type_, &expr, &ctx, &res);
          if (OB_FAIL(result.init(file_content.length()))) {
            LOG_WARN("failed to initialize load file result", K(ret), K(file_content.length()));
          } else if (OB_FAIL(result.append(file_content))) {
            LOG_WARN("failed to append load file result", K(ret), K(file_content.length()));
          } else {
            result.set_result();
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
