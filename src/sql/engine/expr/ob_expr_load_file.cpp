/*
 * Copyright (c) 2026 OceanBase.
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
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/io/ob_backup_io_adapter.h"
#include "share/io/ob_backup_storage_info.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "observer/ob_server_struct.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

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
  type1.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type2.set_calc_type(ObVarcharType);
  type2.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_type(ObLongTextType);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *loc_datum = NULL;
  ObDatum *file_datum = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, loc_datum, file_datum))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (loc_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    ObIAllocator &tmp_alloc = tmp_alloc_g.get_allocator();
    ObString location_name = loc_datum->get_string();
    ObString file_name = file_datum->get_string();
    ObSchemaGetterGuard schema_guard;
    const ObLocationSchema *location_schema = NULL;
    if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema service is null", K(ret));
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
      LOG_WARN("failed to get tenant schema guard", K(ret));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
      LOG_WARN("failed to get location schema", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_LOCATION_OBJ_NOT_EXIST;
      LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST, location_name.length(), location_name.ptr());
      LOG_WARN("location does not exist", K(ret), K(location_name));
    } else {
      ObString location_url = location_schema->get_location_url_str();
      // strip a trailing '/' from the url and a leading '/' from the file name,
      // then join with a single '/'
      while (location_url.length() > 0 && location_url[location_url.length() - 1] == '/') {
        location_url.assign_ptr(location_url.ptr(), location_url.length() - 1);
      }
      while (file_name.length() > 0 && file_name[0] == '/') {
        file_name.assign_ptr(file_name.ptr() + 1, file_name.length() - 1);
      }
      char *uri_buf = NULL;
      int64_t uri_len = location_url.length() + 1 + file_name.length();
      int64_t file_len = 0;
      if (file_name.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, file name is empty");
        LOG_WARN("file name is empty", K(ret));
      } else if (OB_ISNULL(uri_buf = static_cast<char *>(tmp_alloc.alloc(uri_len + 1)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate uri buf", K(ret), K(uri_len));
      } else {
        int64_t pos = 0;
        MEMCPY(uri_buf + pos, location_url.ptr(), location_url.length());
        pos += location_url.length();
        uri_buf[pos++] = '/';
        MEMCPY(uri_buf + pos, file_name.ptr(), file_name.length());
        pos += file_name.length();
        uri_buf[pos] = '\0';
        ObString uri(pos, uri_buf);
        ObBackupStorageInfo storage_info;
        const char *access_info = location_schema->get_location_access_info();
        if (OB_FAIL(storage_info.set(uri_buf, NULL == access_info ? "" : access_info))) {
          LOG_WARN("failed to set storage info", K(ret), K(uri));
        } else if (OB_FAIL(ObBackupIoAdapter::get_file_length(uri, &storage_info, file_len))) {
          LOG_WARN("failed to get file length", K(ret), K(uri));
        } else {
          char *file_buf = NULL;
          int64_t read_size = 0;
          if (file_len > 0 && OB_ISNULL(file_buf = static_cast<char *>(tmp_alloc.alloc(file_len)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("failed to allocate file buf", K(ret), K(file_len));
          } else if (file_len > 0
                     && OB_FAIL(ObBackupIoAdapter::read_single_file(uri, &storage_info, file_buf,
                                                                    file_len, read_size,
                                                                    ObStorageIdMod::get_default_id_mod()))) {
            LOG_WARN("failed to read file", K(ret), K(uri), K(file_len));
          } else {
            ObString content(file_len, file_buf);
            ObTextStringDatumResult text_result(expr.datum_meta_.type_, &expr, &ctx, &res);
            if (OB_FAIL(text_result.init(content.length()))) {
              LOG_WARN("failed to init text result", K(ret), K(content.length()));
            } else if (content.length() > 0 && OB_FAIL(text_result.append(content))) {
              LOG_WARN("failed to append content", K(ret));
            } else {
              text_result.set_result();
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
