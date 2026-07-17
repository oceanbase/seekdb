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
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "share/ob_io_device_helper.h"
#include "share/schema/ob_location_schema_struct.h"
#include "lib/oblog/ob_log_module.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

static const char *LOAD_FILE_URL_PREFIX = "file://";
static const int64_t LOAD_FILE_URL_PREFIX_LEN = 7;

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
  type1.set_calc_type(ObVarcharType);
  type1.set_calc_collation_type(ObCharset::get_system_collation());
  type2.set_calc_type(ObVarcharType);
  type2.set_calc_collation_type(ObCharset::get_system_collation());
  type.set_blob();
  type.set_collation_level(CS_LEVEL_COERCIBLE);
  type.set_length(OB_MAX_LONGTEXT_LENGTH);
  return ret;
}

int ObExprLoadFile::build_file_path(const ObString &location_url,
                                    const ObString &file_name,
                                    ObSqlString &file_path)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(file_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file name is empty", K(ret));
  } else if (OB_UNLIKELY(location_url.length() <= LOAD_FILE_URL_PREFIX_LEN)
             || 0 != MEMCMP(location_url.ptr(), LOAD_FILE_URL_PREFIX, LOAD_FILE_URL_PREFIX_LEN)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only local file location is supported", K(ret), K(location_url));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file on non-local location");
  } else if (OB_UNLIKELY(NULL != file_name.find('/'))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file name should not contain path separator", K(ret), K(file_name));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "file name in load_file");
  } else {
    const ObString dir(location_url.length() - LOAD_FILE_URL_PREFIX_LEN,
                       location_url.ptr() + LOAD_FILE_URL_PREFIX_LEN);
    if (OB_UNLIKELY(dir.empty())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("location url has no path", K(ret), K(location_url));
    } else if (OB_FAIL(file_path.append(dir))) {
      LOG_WARN("fail to append dir", K(ret), K(dir));
    } else if ('/' != dir.ptr()[dir.length() - 1] && OB_FAIL(file_path.append("/"))) {
      LOG_WARN("fail to append separator", K(ret));
    } else if (OB_FAIL(file_path.append(file_name))) {
      LOG_WARN("fail to append file name", K(ret), K(file_name));
    }
  }
  return ret;
}

int ObExprLoadFile::read_whole_file(const char *file_path,
                                    const ObExpr &expr,
                                    ObEvalCtx &ctx,
                                    ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObIOFd fd;
  bool is_opened = false;
  int64_t file_size = 0;
  int64_t offset = 0;

  if (OB_ISNULL(file_path)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file path is null", K(ret));
  } else if (OB_FAIL(ObIODeviceLocalFileOp::open(file_path, O_RDONLY, S_IRUSR, fd))) {
    LOG_WARN("fail to open file", K(ret), K(file_path));
  } else if (FALSE_IT(is_opened = true)) {
  } else if (OB_FAIL(ObIODeviceLocalFileOp::lseek(fd, 0, SEEK_END, file_size))) {
    LOG_WARN("fail to seek file end", K(ret), K(file_path));
  } else if (OB_FAIL(ObIODeviceLocalFileOp::lseek(fd, 0, SEEK_SET, offset))) {
    LOG_WARN("fail to seek file begin", K(ret), K(file_path));
  } else {
    ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &expr_datum);
    char *buf = NULL;
    int64_t buf_size = 0;
    int64_t read_size = 0;
    if (OB_FAIL(output_result.init(file_size))) {
      LOG_WARN("fail to init string text result", K(ret), K(file_size));
    } else if (file_size <= 0) {
      output_result.set_result();
    } else if (OB_FAIL(output_result.get_reserved_buffer(buf, buf_size))) {
      LOG_WARN("fail to get reserved buffer", K(ret), K(file_size));
    } else if (OB_UNLIKELY(buf_size < file_size)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("reserved buffer is too small", K(ret), K(buf_size), K(file_size));
    } else if (OB_FAIL(ObIODeviceLocalFileOp::read(fd, buf, file_size, read_size))) {
      LOG_WARN("fail to read file", K(ret), K(file_path), K(file_size));
    } else if (OB_FAIL(output_result.lseek(read_size, 0))) {
      LOG_WARN("fail to lseek result", K(ret), K(read_size));
    } else {
      output_result.set_result();
    }
  }

  if (is_opened) {
    int tmp_ret = ObIODeviceLocalFileOp::close(fd);
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("fail to close file", K(tmp_ret), K(file_path));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
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
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = NULL;

  if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    expr_datum.set_null();
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_datum->get_string(),
                                                              location_schema))) {
    LOG_WARN("failed to get location schema", K(ret), K(location_datum->get_string()));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("location does not exist", K(ret), K(location_datum->get_string()));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "location name in load_file");
  } else {
    SMART_VAR(ObSqlString, file_path)
    {
      if (OB_FAIL(build_file_path(location_schema->get_location_url_str(),
                                  file_datum->get_string(),
                                  file_path))) {
        LOG_WARN("fail to build file path", K(ret));
      } else if (OB_FAIL(read_whole_file(file_path.ptr(), expr, ctx, expr_datum))) {
        LOG_WARN("fail to read file", K(ret), K(file_path));
      }
    }
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  return OB_SUCCESS;
}

}/* ns sql*/
}/* ns oceanbase */
