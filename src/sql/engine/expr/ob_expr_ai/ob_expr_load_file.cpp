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
#include "lib/string/ob_string_buffer.h"
#include "share/config/ob_tenant_config_mgr.h"
#include "share/io/ob_backup_io_adapter.h"
#include "share/io/ob_backup_storage_info.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_expr_multi_mode_func_helper.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
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
  bool is_null_result = false;
  UNUSED(type_ctx);

  if (type1.is_null() || type2.is_null()) {
    is_null_result = true;
  } else if (!ob_is_string_type(type1.get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid location name type", K(ret), K(type1.get_type()));
  } else if (!ob_is_string_type(type2.get_type())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid file name type", K(ret), K(type2.get_type()));
  } else {
    type1.set_calc_type(ObVarcharType);
    type1.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    type2.set_calc_type(ObVarcharType);
    type2.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  }

  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
    type.set_null();
  } else {
    type.set_blob();
    type.set_length(OB_MAX_LONGTEXT_LENGTH);
  }
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *location_name_datum = nullptr;
  ObDatum *filename_datum = nullptr;

  if (ob_is_null(expr.obj_meta_.get_type())) {
    expr_datum.set_null();
  } else if (OB_FAIL(expr.eval_param_value(ctx, location_name_datum, filename_datum))) {
    LOG_WARN("failed to evaluate load_file parameters", K(ret));
  } else if (location_name_datum->is_null() || filename_datum->is_null()) {
    expr_datum.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_guard(ctx);
    MultimodeAlloctor allocator(tmp_alloc_guard.get_allocator(), expr.type_, ret, N_LOAD_FILE);
    lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_LOAD_FILE));
    ObString location_name;
    ObString filename;
    ObString file_data;

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                         allocator,
                         *location_name_datum,
                         expr.args_[0]->datum_meta_,
                         expr.args_[0]->obj_meta_.has_lob_header(),
                         location_name,
                         &ctx.exec_ctx_))) {
      LOG_WARN("failed to read location name", K(ret));
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                         allocator,
                         *filename_datum,
                         expr.args_[1]->datum_meta_,
                         expr.args_[1]->obj_meta_.has_lob_header(),
                         filename,
                         &ctx.exec_ctx_))) {
      LOG_WARN("failed to read file name", K(ret));
    } else if (OB_FAIL(read_file_from_location(
                         location_name, filename, ctx.exec_ctx_, allocator, file_data))) {
      LOG_WARN("failed to read file from location", K(ret), K(location_name), K(filename));
    } else if (OB_FAIL(ObTextStringHelper::string_to_templob_result(
                         expr, ctx, expr_datum, file_data))) {
      LOG_WARN("failed to build load_file result", K(ret), K(file_data.length()));
    }
  }
  return ret;
}

int ObExprLoadFile::eval_load_file_vector(const ObExpr &expr,
                                          ObEvalCtx &ctx,
                                          const ObBitVector &skip,
                                          const EvalBound &bound)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(expr.eval_vector_param_value(ctx, skip, bound))) {
    LOG_WARN("failed to evaluate load_file vector parameters", K(ret));
  } else {
    ObIVector *location_name_vec = expr.args_[0]->get_vector(ctx);
    ObIVector *filename_vec = expr.args_[1]->get_vector(ctx);
    ObIVector *result_vec = expr.get_vector(ctx);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);

    for (int64_t idx = bound.start(); OB_SUCC(ret) && idx < bound.end(); ++idx) {
      if (skip.at(idx) || eval_flags.at(idx)) {
        continue;
      }
      eval_flags.set(idx);

      if (ob_is_null(expr.obj_meta_.get_type())
          || location_name_vec->is_null(idx)
          || filename_vec->is_null(idx)) {
        result_vec->set_null(idx);
      } else {
        ObEvalCtx::TempAllocGuard tmp_alloc_guard(ctx);
        MultimodeAlloctor allocator(tmp_alloc_guard.get_allocator(), expr.type_, ret, N_LOAD_FILE);
        lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(N_LOAD_FILE));
        ObString location_name;
        ObString filename;
        ObString file_data;

        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                             allocator,
                             location_name_vec,
                             expr.args_[0]->datum_meta_,
                             expr.args_[0]->obj_meta_.has_lob_header(),
                             location_name,
                             idx,
                             &ctx.exec_ctx_))) {
          LOG_WARN("failed to read vector location name", K(ret), K(idx));
        } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                             allocator,
                             filename_vec,
                             expr.args_[1]->datum_meta_,
                             expr.args_[1]->obj_meta_.has_lob_header(),
                             filename,
                             idx,
                             &ctx.exec_ctx_))) {
          LOG_WARN("failed to read vector file name", K(ret), K(idx));
        } else if (OB_FAIL(read_file_from_location(
                             location_name, filename, ctx.exec_ctx_, allocator, file_data))) {
          LOG_WARN("failed to read file from location", K(ret), K(location_name), K(filename), K(idx));
        } else {
          ObTextStringVectorResult<ObIVector> blob_result(
              expr.datum_meta_.type_, &expr, &ctx, result_vec, idx);
          if (OB_FAIL(blob_result.init_with_batch_idx(file_data.length(), idx))) {
            LOG_WARN("failed to initialize vector BLOB result", K(ret), K(idx), K(file_data.length()));
          } else if (OB_FAIL(blob_result.append(file_data))) {
            LOG_WARN("failed to append vector BLOB result", K(ret), K(idx), K(file_data.length()));
          } else {
            blob_result.set_result();
          }
        }
      }
    }
  }
  return ret;
}

int ObExprLoadFile::read_file_from_location(const ObString &location_name,
                                            const ObString &filename,
                                            ObExecContext &exec_ctx,
                                            ObIAllocator &allocator,
                                            ObString &file_data)
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session_info = exec_ctx.get_my_session();
  ObSchemaGetterGuard schema_guard;
  const ObLocationSchema *location_schema = nullptr;
  share::schema::ObSessionPrivInfo session_priv;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF());
  const int64_t max_file_size = tenant_config.is_valid()
      ? static_cast<int64_t>(tenant_config->document_ai_file_max_size)
      : DEFAULT_DOCUMENT_AI_FILE_MAX_SIZE;

  if (location_name.empty() || filename.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("location name or file name is empty", K(ret));
  } else if (OB_ISNULL(session_info) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session or schema service is null", K(ret), KP(session_info), KP(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("failed to get tenant schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_location_schema_by_name(location_name, location_schema))) {
    LOG_WARN("failed to get location schema", K(ret), K(location_name));
  } else if (OB_ISNULL(location_schema)) {
    ret = OB_LOCATION_OBJ_NOT_EXIST;
    LOG_WARN("location object does not exist", K(ret), K(location_name));
  } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
    LOG_WARN("failed to get session privilege information", K(ret));
  } else if (OB_FAIL(schema_guard.check_location_access(
                       session_priv,
                       session_info->get_enable_role_array(),
                       location_name,
                       false /* read only */))) {
    LOG_WARN("location read access denied", K(ret), K(location_name));
  } else {
    const ObString &location_url = location_schema->get_location_url_str();
    const ObString &access_info = location_schema->get_location_access_info_str();
    ObString file_url;
    ObString file_url_cstr;
    ObString access_info_cstr;
    share::ObBackupStorageInfo storage_info;
    ObBackupIoAdapter io_adapter;
    int64_t file_size = -1;
    int64_t read_size = 0;
    char *buffer = nullptr;

    if (OB_FAIL(build_file_path(location_url, filename, allocator, file_url))) {
      LOG_WARN("failed to build file path", K(ret), K(location_url), K(filename));
    } else if (OB_FAIL(ob_write_string(allocator, file_url, file_url_cstr, true))) {
      LOG_WARN("failed to build C-style file URL", K(ret), K(file_url));
    } else if (OB_FAIL(ob_write_string(allocator, access_info, access_info_cstr, true))) {
      LOG_WARN("failed to build C-style location access information", K(ret));
    } else if (OB_FAIL(storage_info.set(file_url_cstr.ptr(), access_info_cstr.ptr()))) {
      LOG_WARN("failed to initialize storage information", K(ret), K(file_url));
    } else if (OB_FAIL(io_adapter.get_file_length(file_url, &storage_info, file_size))) {
      LOG_WARN("failed to get file size", K(ret), K(file_url));
    } else if (file_size < 0) {
      ret = OB_OBJECT_NAME_NOT_EXIST;
      LOG_WARN("file does not exist", K(ret), K(file_url), K(file_size));
    } else if (0 == file_size) {
      ret = OB_INVALID_DATA;
      LOG_WARN("empty files are not supported", K(ret), K(file_url));
      FORWARD_USER_ERROR(ret, "invalid file size (empty file)");
    } else if (file_size > max_file_size) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("file exceeds document AI size limit", K(ret), K(file_size), K(max_file_size));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "file size exceeds document ai file max size is");
    } else if (OB_ISNULL(buffer = static_cast<char *>(allocator.alloc(file_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate file buffer", K(ret), K(file_size));
    } else if (OB_FAIL(io_adapter.read_single_file(
                         file_url,
                         &storage_info,
                         buffer,
                         file_size,
                         read_size,
                         ObStorageIdMod::get_default_id_mod()))) {
      LOG_WARN("failed to read file", K(ret), K(file_url), K(file_size));
    } else if (read_size != file_size) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("file read size mismatch", K(ret), K(file_url), K(file_size), K(read_size));
    } else {
      file_data.assign_ptr(buffer, static_cast<int32_t>(read_size));
    }
  }
  return ret;
}

int ObExprLoadFile::build_file_path(const ObString &location_url,
                                    const ObString &filename,
                                    ObIAllocator &allocator,
                                    ObString &full_path)
{
  int ret = OB_SUCCESS;
  ObStringBuffer path_buffer(&allocator);
  bool has_parent_component = false;

  for (int64_t component_start = 0, pos = 0;
       !has_parent_component && pos <= filename.length(); ++pos) {
    if (pos == filename.length() || '/' == filename.ptr()[pos]) {
      has_parent_component = 2 == pos - component_start
                             && '.' == filename.ptr()[component_start]
                             && '.' == filename.ptr()[component_start + 1];
      component_start = pos + 1;
    }
  }

  if (location_url.empty() || filename.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("location URL or file name is empty", K(ret));
  } else if (has_parent_component) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("file name cannot contain a parent directory component", K(ret), K(filename));
  } else if (OB_FAIL(path_buffer.reserve(location_url.length() + filename.length() + 1))) {
    LOG_WARN("failed to reserve file path buffer", K(ret), K(location_url.length()), K(filename.length()));
  } else if (OB_FAIL(path_buffer.append(location_url))) {
    LOG_WARN("failed to append location URL", K(ret), K(location_url));
  } else {
    const bool url_has_separator = '/' == location_url.ptr()[location_url.length() - 1];
    const bool filename_has_separator = '/' == filename.ptr()[0];
    ObString normalized_filename = filename;

    if (url_has_separator && filename_has_separator) {
      normalized_filename.assign_ptr(filename.ptr() + 1, filename.length() - 1);
    } else if (!url_has_separator && !filename_has_separator
               && OB_FAIL(path_buffer.append("/"))) {
      LOG_WARN("failed to append file path separator", K(ret));
    }

    if (OB_FAIL(ret)) {
    } else if (normalized_filename.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("file name is empty after path normalization", K(ret), K(filename));
    } else if (OB_FAIL(path_buffer.append(normalized_filename))) {
      LOG_WARN("failed to append file name", K(ret), K(normalized_filename));
    } else if (OB_FAIL(path_buffer.get_result_string(full_path))) {
      LOG_WARN("failed to take ownership of file path buffer", K(ret));
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
  rt_expr.eval_func_ = eval_load_file;
  rt_expr.eval_vector_func_ = eval_load_file_vector;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
