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
#include "ob_ai_func_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_location_schema_struct.h"
#include "lib/oblog/ob_log_module.h"

#include <stdio.h>
#include <sys/stat.h>

using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprLoadFile::ObExprLoadFile(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_LOAD_FILE,
                         N_LOAD_FILE,
                         2,
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
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
  type.set_type(ObLongTextType);
  type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *location_datum = NULL;
  ObDatum *file_datum = NULL;
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = NULL;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("load_file parameter is null", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, parameter is null");
    expr_datum.set_null();
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_datum->get_string(),
                                                              location_schema))) {
    ret = OB_LOCATION_NOT_EXIST;
    LOG_WARN("failed to get location schema", K(ret), K(location_datum->get_string()));
    LOG_USER_ERROR(OB_LOCATION_NOT_EXIST, location_datum->get_string().length(),
                   location_datum->get_string().ptr());
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null location schema", K(ret));
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    ObIAllocator &tmp_alloc = tmp_alloc_g.get_allocator();
    ObString dir;
    ObString full_path;
    ObString content;
    if (OB_FAIL(get_location_dir(tmp_alloc, location_schema->get_location_url_str(), dir))) {
      LOG_WARN("failed to parse location url", K(ret), K(location_schema->get_location_url_str()));
    } else {
      const int64_t sep_len = (dir.length() > 0
          && (dir.ptr()[dir.length() - 1] == '/' || dir.ptr()[dir.length() - 1] == '\\')) ? 0 : 1;
      const int64_t path_len = dir.length() + sep_len + file_datum->get_string().length();
      char *path_buf = NULL;
      if (OB_UNLIKELY(path_len <= 0) || OB_ISNULL(path_buf = static_cast<char *>(tmp_alloc.alloc(path_len + 1)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("alloc memory failed", K(ret), K(path_len));
      } else {
        int64_t pos = 0;
        MEMCPY(path_buf + pos, dir.ptr(), dir.length());
        pos += dir.length();
        if (sep_len > 0) {
          path_buf[pos++] = '/';
        }
        MEMCPY(path_buf + pos, file_datum->get_string().ptr(), file_datum->get_string().length());
        pos += file_datum->get_string().length();
        path_buf[pos] = '\0';
        full_path.assign_ptr(path_buf, static_cast<int32_t>(pos));
        if (OB_FAIL(read_file_to_string(tmp_alloc, path_buf, content))) {
          LOG_WARN("failed to read file", K(ret), K(full_path));
          LOG_USER_ERROR(OB_FILE_NOT_EXIST, full_path.length(), full_path.ptr());
        } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, expr_datum, content))) {
          LOG_WARN("failed to set string result", K(ret), K(content.length()));
        }
      }
    }
  }
  return ret;
}

int ObExprLoadFile::get_location_dir(common::ObIAllocator &allocator,
                                     const common::ObString &url,
                                     common::ObString &dir)
{
  int ret = OB_SUCCESS;
  const char *PREFIX = "file://";
  const int64_t prefix_len = static_cast<int64_t>(strlen(PREFIX));
  const char *ptr = url.ptr();
  int64_t len = url.length();
  if (len >= prefix_len && 0 == STRNCASECMP(ptr, PREFIX, prefix_len)) {
    ptr += prefix_len;
    len -= prefix_len;
  }
  if (len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid location url", K(ret), K(url));
  } else {
    char *buf = static_cast<char *>(allocator.alloc(len + 1));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc memory failed", K(ret), K(len));
    } else {
      MEMCPY(buf, ptr, len);
      buf[len] = '\0';
      dir.assign_ptr(buf, static_cast<int32_t>(len));
    }
  }
  return ret;
}

int ObExprLoadFile::read_file_to_string(common::ObIAllocator &allocator,
                                        const char *path,
                                        common::ObString &content)
{
  int ret = OB_SUCCESS;
  FILE *fp = NULL;
  if (OB_ISNULL(path)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid path", K(ret));
  } else if (OB_ISNULL(fp = fopen(path, "rb"))) {
    ret = OB_FILE_NOT_EXIST;
    LOG_WARN("open file failed", K(ret), K(path));
  } else {
    int64_t size = 0;
    if (OB_FAIL(fseek(fp, 0, SEEK_END))) {
      ret = OB_IO_ERROR;
      LOG_WARN("fseek failed", K(ret), K(path));
    } else if (OB_UNLIKELY((size = ftell(fp)) < 0)) {
      ret = OB_IO_ERROR;
      LOG_WARN("ftell failed", K(ret), K(path));
    } else if (OB_FAIL(fseek(fp, 0, SEEK_SET))) {
      ret = OB_IO_ERROR;
      LOG_WARN("fseek failed", K(ret), K(path));
    } else if (size > 0) {
      char *buf = static_cast<char *>(allocator.alloc(size + 1));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("alloc memory failed", K(ret), K(size));
      } else if (OB_UNLIKELY(fread(buf, 1, size, fp) != static_cast<size_t>(size))) {
        ret = OB_IO_ERROR;
        LOG_WARN("fread failed", K(ret), K(path));
      } else {
        buf[size] = '\0';
        content.assign_ptr(buf, static_cast<int32_t>(size));
      }
    } else {
      content.reset();
    }
    fclose(fp);
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  return OB_SUCCESS;
}

}/* ns sql*/
}/* ns oceanbase*/
