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
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_location_schema_struct.h"
#include "share/io/ob_backup_io_adapter.h"
#include "share/io/ob_backup_storage_info.h"
#include "lib/restore/ob_storage_info.h"

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
  type1.set_calc_collation_type(ObCharset::get_system_collation());
  type2.set_calc_type(ObVarcharType);
  type2.set_calc_collation_type(ObCharset::get_system_collation());
  type.set_blob();
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_length(OB_MAX_LONGTEXT_LENGTH);
  return ret;
}

int ObExprLoadFile::check_file_name_valid(const ObString &file_name)
{
  int ret = OB_SUCCESS;
  bool invalid = false;
  if (file_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file name is empty", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, file name is empty");
  } else if (file_name.prefix_match("/") || file_name.prefix_match("\\")) {
    invalid = true;
  } else {
    // Reject path traversal and scheme injection; LOCATION is the trusted root.
    for (int64_t i = 0; !invalid && i < file_name.length(); ++i) {
      if ('.' == file_name[i] && i + 1 < file_name.length() && '.' == file_name[i + 1]) {
        invalid = true;
      } else if (':' == file_name[i] && i + 2 < file_name.length()
                 && '/' == file_name[i + 1] && '/' == file_name[i + 2]) {
        invalid = true;
      }
    }
  }
  if (OB_SUCC(ret) && invalid) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid file name", K(ret), K(file_name));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, invalid file name");
  }
  return ret;
}

int ObExprLoadFile::build_file_uri(const ObString &location_url,
                                   const ObString &file_name,
                                   ObIAllocator &allocator,
                                   ObString &file_uri)
{
  int ret = OB_SUCCESS;
  ObString url = location_url;
  while (url.length() > 0 && (' ' == url[url.length() - 1] || '\t' == url[url.length() - 1])) {
    url.assign_ptr(url.ptr(), url.length() - 1);
  }
  const bool need_slash = (url.empty() || '/' != url[url.length() - 1]);
  const int64_t uri_len = url.length() + (need_slash ? 1 : 0) + file_name.length();
  char *buf = static_cast<char *>(allocator.alloc(uri_len + 1));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate uri buffer", K(ret), K(uri_len));
  } else {
    int64_t pos = 0;
    if (OB_FAIL(databuff_printf(buf, uri_len + 1, pos, "%.*s%s%.*s",
                                url.length(), url.ptr(),
                                need_slash ? "/" : "",
                                file_name.length(), file_name.ptr()))) {
      LOG_WARN("failed to build file uri", K(ret), K(url), K(file_name));
    } else {
      file_uri.assign_ptr(buf, static_cast<int32_t>(pos));
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
  ObSessionPrivInfo session_priv;
  const ObLocationSchema *location_schema = NULL;
  ObString location_name;
  ObString file_name;
  ObString file_uri;
  ObBackupStorageInfo storage_info;
  int64_t file_length = 0;
  int64_t read_size = 0;
  char *file_buf = NULL;

  if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), KP(session_info), KP(GCTX.schema_service_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (OB_ISNULL(location_datum) || OB_ISNULL(file_datum)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null datum", K(ret), KP(location_datum), KP(file_datum));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    expr_datum.set_null();
  } else if (FALSE_IT(location_name = location_datum->get_string())
             || FALSE_IT(file_name = file_datum->get_string())) {
  } else if (location_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("location name is empty", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, location name is empty");
  } else if (OB_FAIL(check_file_name_valid(file_name))) {
    LOG_WARN("invalid file name", K(ret), K(file_name));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
    LOG_WARN("failed to get session priv info", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
    LOG_WARN("failed to get location schema by name", K(ret), K(location_name));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_LOCATION_OBJ_NOT_EXIST;
    LOG_WARN("location object does not exist", K(ret), K(location_name));
    LOG_USER_ERROR(OB_LOCATION_OBJ_NOT_EXIST,
                   location_name.length(), location_name.ptr());
  } else if (OB_FAIL(schema_guard.check_location_access(session_priv,
                                                        session_info->get_enable_role_array(),
                                                        location_name,
                                                        false /*is_write*/))) {
    LOG_WARN("failed to check location access", K(ret), K(location_name));
  } else {
    ObEvalCtx::TempAllocGuard alloc_guard(ctx);
    ObIAllocator &allocator = alloc_guard.get_allocator();
    const ObString &location_url = location_schema->get_location_url_str();
    if (!location_url.prefix_match(OB_FILE_PREFIX)) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("load_file only supports file:// location in seekdb", K(ret), K(location_url));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file on non-file:// LOCATION");
    } else if (OB_FAIL(build_file_uri(location_url, file_name, allocator, file_uri))) {
      LOG_WARN("failed to build file uri", K(ret), K(location_url), K(file_name));
    } else if (OB_FAIL(storage_info.set(file_uri.ptr(),
                                        location_schema->get_location_access_info()))) {
      LOG_WARN("failed to set storage info", K(ret), K(file_uri));
    } else if (OB_STORAGE_FILE != storage_info.get_type()) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("load_file only supports local file storage", K(ret),
               "storage_type", storage_info.get_type());
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "load_file on non-local LOCATION");
    } else if (OB_FAIL(ObBackupIoAdapter::get_file_length(file_uri, &storage_info, file_length))) {
      LOG_WARN("failed to get file length", K(ret), K(file_uri));
    } else if (file_length < 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected file length", K(ret), K(file_length), K(file_uri));
    } else if (file_length > OB_MAX_LONGTEXT_LENGTH) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("file too large", K(ret), K(file_length));
    } else if (0 == file_length) {
      ObTextStringDatumResult str_result(expr.datum_meta_.type_, &expr, &ctx, &expr_datum);
      if (OB_FAIL(str_result.init(0))) {
        LOG_WARN("init empty blob result failed", K(ret));
      } else {
        str_result.set_result();
      }
    } else if (OB_ISNULL(file_buf = static_cast<char *>(allocator.alloc(file_length)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate file buffer", K(ret), K(file_length));
    } else if (OB_FAIL(ObBackupIoAdapter::read_single_file(file_uri,
                                                           &storage_info,
                                                           file_buf,
                                                           file_length,
                                                           read_size,
                                                           ObStorageIdMod::get_default_id_mod()))) {
      LOG_WARN("failed to read file", K(ret), K(file_uri), K(file_length));
    } else {
      ObTextStringDatumResult str_result(expr.datum_meta_.type_, &expr, &ctx, &expr_datum);
      if (OB_FAIL(str_result.init(read_size))) {
        LOG_WARN("init blob result failed", K(ret), K(read_size));
      } else if (OB_FAIL(str_result.append(file_buf, read_size))) {
        LOG_WARN("append blob result failed", K(ret), K(read_size));
      } else {
        str_result.set_result();
      }
    }
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

} // namespace sql
} // namespace oceanbase
