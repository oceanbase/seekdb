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
// Task2: 安全读取 LOCATION 下的本地普通文件并返回二进制 LOB。
#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_load_file.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "share/schema/ob_location_schema_struct.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

namespace oceanbase {
namespace sql {
namespace {

const ObString FILE_URL_PREFIX("file://");
const int64_t READ_BUFFER_SIZE = 64 * 1024;

bool is_safe_file_name(const ObString &file_name) {
  bool safe = !file_name.empty() && 0 != file_name.compare(".") &&
              0 != file_name.compare("..");
  for (int64_t i = 0; safe && i < file_name.length(); ++i) {
    const char ch = file_name.ptr()[i];
    safe = '/' != ch && '\\' != ch && '\0' != ch;
  }
  return safe;
}

int copy_as_c_string(ObIAllocator &allocator, const ObString &source,
                     char *&target) {
  int ret = OB_SUCCESS;
  target = static_cast<char *>(allocator.alloc(source.length() + 1));
  if (OB_ISNULL(target)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate path buffer", K(ret), K(source.length()));
  } else {
    MEMCPY(target, source.ptr(), source.length());
    target[source.length()] = '\0';
  }
  return ret;
}

int read_regular_file(const ObExpr &expr, ObEvalCtx &ctx,
                      ObIAllocator &allocator, const ObString &root_path,
                      const ObString &file_name,
                      const int64_t max_allowed_packet, ObDatum &res) {
  int ret = OB_SUCCESS;
  int dir_fd = -1;
  int file_fd = -1;
  char *root_cstr = nullptr;
  char *file_cstr = nullptr;
  struct stat file_stat;

  if (root_path.empty() || '/' != root_path.ptr()[0]) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                   "LOAD_FILE requires an absolute file:// location");
  } else if (!is_safe_file_name(file_name)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                   "LOAD_FILE file_name must name a direct child file");
  } else if (OB_FAIL(copy_as_c_string(allocator, root_path, root_cstr))) {
    LOG_WARN("failed to copy location path", K(ret));
  } else if (OB_FAIL(copy_as_c_string(allocator, file_name, file_cstr))) {
    LOG_WARN("failed to copy file name", K(ret));
  } else if ((dir_fd = ::open(root_cstr, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) <
             0) {
    ret = ENOENT == errno ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
    LOG_WARN("failed to open location directory", K(ret), K(errno));
  } else if ((file_fd = ::openat(dir_fd, file_cstr,
                                 O_RDONLY | O_CLOEXEC | O_NOFOLLOW)) < 0) {
    ret = ENOENT == errno ? OB_FILE_NOT_EXIST : OB_IO_ERROR;
    LOG_WARN("failed to open location file", K(ret), K(errno), K(file_name));
  } else if (0 != ::fstat(file_fd, &file_stat)) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to stat location file", K(ret), K(errno), K(file_name));
  } else if (!S_ISREG(file_stat.st_mode)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                   "LOAD_FILE only supports regular files");
  } else if (file_stat.st_size < 0 || file_stat.st_size > max_allowed_packet) {
    ret = OB_ERR_FUNC_RESULT_TOO_LARGE;
    LOG_USER_ERROR(OB_ERR_FUNC_RESULT_TOO_LARGE, N_LOAD_FILE,
                   static_cast<int>(max_allowed_packet));
  } else {
    ObTextStringDatumResult result(expr.datum_meta_.type_, &expr, &ctx, &res);
    char read_buffer[READ_BUFFER_SIZE];
    int64_t total_read = 0;
    if (OB_FAIL(result.init(file_stat.st_size))) {
      LOG_WARN("failed to initialize LOAD_FILE result", K(ret),
               K(file_stat.st_size));
    }
    while (OB_SUCC(ret) && total_read < file_stat.st_size) {
      const int64_t remaining = file_stat.st_size - total_read;
      const size_t read_size =
          static_cast<size_t>(MIN(remaining, READ_BUFFER_SIZE));
      const ssize_t bytes_read = ::read(file_fd, read_buffer, read_size);
      if (bytes_read < 0 && EINTR == errno) {
        continue;
      } else if (bytes_read < 0) {
        ret = OB_IO_ERROR;
        LOG_WARN("failed to read location file", K(ret), K(errno),
                 K(file_name));
      } else if (0 == bytes_read) {
        break;
      } else if (OB_FAIL(result.append(read_buffer, bytes_read))) {
        LOG_WARN("failed to append LOAD_FILE result", K(ret), K(bytes_read));
      } else {
        total_read += bytes_read;
      }
    }
    if (OB_SUCC(ret)) {
      result.set_result();
    }
  }

  if (file_fd >= 0 && 0 != ::close(file_fd) && OB_SUCC(ret)) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to close location file", K(ret), K(errno));
  }
  if (dir_fd >= 0 && 0 != ::close(dir_fd) && OB_SUCC(ret)) {
    ret = OB_IO_ERROR;
    LOG_WARN("failed to close location directory", K(ret), K(errno));
  }
  return ret;
}

} // namespace

ObExprLoadFile::ObExprLoadFile(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_LOAD_FILE, N_LOAD_FILE, 2,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}

int ObExprLoadFile::calc_result_type2(ObExprResType &type,
                                      ObExprResType &location_type,
                                      ObExprResType &file_type,
                                      ObExprTypeCtx &type_ctx) const {
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  location_type.set_calc_type(ObVarcharType);
  location_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  file_type.set_calc_type(ObVarcharType);
  file_type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  type.set_type(ObLongTextType);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]);
  return ret;
}

int ObExprLoadFile::eval_load_file(const ObExpr &expr, ObEvalCtx &ctx,
                                   ObDatum &res) {
  int ret = OB_SUCCESS;
  ObDatum *location_datum = nullptr;
  ObDatum *file_datum = nullptr;
  ObSQLSessionInfo *session_info = nullptr;
  ObSchemaGetterGuard schema_guard;
  ObSessionPrivInfo session_priv;
  const ObLocationSchema *location_schema = nullptr;
  int64_t max_allowed_packet = 0;

  if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("failed to evaluate LOAD_FILE parameters", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    res.set_null();
  } else if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session()) ||
             OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get LOAD_FILE execution context", K(ret));
  } else {
    ObEvalCtx::TempAllocGuard temp_alloc_guard(ctx);
    ObIAllocator &allocator = temp_alloc_guard.get_allocator();
    ObString location_name = location_datum->get_string();
    ObString file_name = file_datum->get_string();
    ObString location_url;

    if (OB_FAIL(ObTextStringHelper::read_real_string_data(
            allocator, *location_datum, expr.args_[0]->datum_meta_,
            expr.args_[0]->obj_meta_.has_lob_header(), location_name))) {
      LOG_WARN("failed to read location name", K(ret));
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                   allocator, *file_datum, expr.args_[1]->datum_meta_,
                   expr.args_[1]->obj_meta_.has_lob_header(), file_name))) {
      LOG_WARN("failed to read file name", K(ret));
    } else if (location_name.empty() || file_name.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                     "LOAD_FILE parameters must not be empty");
    } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(
                   schema_guard))) {
      LOG_WARN("failed to get tenant schema guard", K(ret));
    } else if (OB_FAIL(session_info->get_session_priv_info(session_priv))) {
      LOG_WARN("failed to get session privilege info", K(ret));
    } else if (OB_FAIL(schema_guard.check_location_access(
                   session_priv, session_info->get_enable_role_array(),
                   location_name, false))) {
      LOG_WARN("location read access denied", K(ret), K(location_name));
    } else if (OB_FAIL(schema_guard.get_location_schema_by_name(
                   location_name, location_schema))) {
      LOG_WARN("failed to find location", K(ret), K(location_name));
    } else if (OB_ISNULL(location_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("location schema is null", K(ret), K(location_name));
    } else if (FALSE_IT(location_url =
                            location_schema->get_location_url_str())) {
    } else if (!location_url.prefix_match(FILE_URL_PREFIX)) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED,
                     "LOAD_FILE only supports file:// locations");
    } else if (OB_FAIL(
                   session_info->get_max_allowed_packet(max_allowed_packet))) {
      LOG_WARN("failed to get max_allowed_packet", K(ret));
    } else {
      ObString root_path(location_url.length() - FILE_URL_PREFIX.length(),
                         location_url.ptr() + FILE_URL_PREFIX.length());
      if (OB_FAIL(read_regular_file(expr, ctx, allocator, root_path, file_name,
                                    max_allowed_packet, res))) {
        LOG_WARN("failed to load local file", K(ret), K(location_name),
                 K(file_name));
      }
    }
  }
  return ret;
}

int ObExprLoadFile::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const {
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprLoadFile::eval_load_file;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
