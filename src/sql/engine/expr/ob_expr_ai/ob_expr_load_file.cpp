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
#include "lib/oblog/ob_log_module.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

namespace
{
// Upper bound of a single file loaded into memory: it protects the evaluator
// from allocating an arbitrary amount of memory based on a file size, and keeps
// the length far away from the int32 ObString range.
const int64_t MAX_LOAD_FILE_SIZE = 16L * 1024 * 1024;
const int64_t MAX_LOAD_FILE_PATH_LENGTH = 4096;

// _fullpath() takes the output buffer, the path and its capacity; the POSIX
// realpath() takes the path and the output buffer. Wrap the Windows one so both
// call sites below share a single (path, out) signature.
#ifdef _WIN32
static inline char *seekdb_path_realpath(const char *path, char *resolved_path)
{
  return _fullpath(resolved_path, path, MAX_LOAD_FILE_PATH_LENGTH);
}
#define SEEKDB_PATH_REALPATH(path, out) seekdb_path_realpath((path), (out))
#else
#define SEEKDB_PATH_REALPATH(path, out) realpath((path), (out))
#endif

// The file name is resolved *relative* to the location directory. Absolute
// paths, drive letters, window alternate data streams and any "." or ".."
// component are rejected, so a location can never escape its own directory.
int check_relative_file_name(const common::ObString &file_name)
{
  int ret = OB_SUCCESS;
  const char *ptr = file_name.ptr();
  const int64_t len = file_name.length();
  if (OB_ISNULL(ptr) || OB_UNLIKELY(len <= 0 || len > MAX_LOAD_FILE_PATH_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY('/' == ptr[0] || '\\' == ptr[0] || ':' == ptr[0])) {
    ret = OB_INVALID_ARGUMENT; // absolute path or drive-relative path
  } else {
    int64_t seg_start = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i <= len; ++i) {
      const bool is_sep = (i == len) || '/' == ptr[i] || '\\' == ptr[i];
      if (is_sep) {
        const int64_t seg_len = i - seg_start;
        if (OB_UNLIKELY(seg_len <= 0)) {
          ret = OB_INVALID_ARGUMENT; // empty component
        } else if (OB_UNLIKELY(('.' == ptr[seg_start])
                               && (1 == seg_len || (2 == seg_len && '.' == ptr[seg_start + 1])))) {
          // check seg_len before touching ptr[seg_start + 1]: the last component
          // ends at file_name.length(), which is not guaranteed to be '\0'
          // terminated, so peeking one byte ahead there would be an out of bound read.
          ret = OB_INVALID_ARGUMENT; // "." or ".."
        } else if (OB_UNLIKELY(nullptr != memchr(ptr + seg_start, ':', seg_len))) {
          ret = OB_INVALID_ARGUMENT; // drive letter or alternate data stream
        } else {
          seg_start = i + 1;
        }
      }
    }
  }
  return ret;
}

// Compares the canonical (symlink-free, ".."-free) form of the target with the
// canonical form of the location directory. This catches everything the lexical
// checks above cannot see, such as symbolic links pointing outside the location.
int check_path_within_dir(const common::ObString &dir, const char *path)
{
  int ret = OB_SUCCESS;
  char dir_buf[MAX_LOAD_FILE_PATH_LENGTH];
  char resolved_dir[MAX_LOAD_FILE_PATH_LENGTH];
  char resolved_path[MAX_LOAD_FILE_PATH_LENGTH];
  if (OB_ISNULL(path) || OB_UNLIKELY(dir.length() <= 0 || dir.length() >= MAX_LOAD_FILE_PATH_LENGTH)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    MEMCPY(dir_buf, dir.ptr(), dir.length());
    dir_buf[dir.length()] = '\0';
    if (OB_ISNULL(SEEKDB_PATH_REALPATH(dir_buf, resolved_dir))
        || OB_ISNULL(SEEKDB_PATH_REALPATH(path, resolved_path))) {
      ret = OB_FILE_NOT_EXIST;
    } else {
      const int64_t dir_len = static_cast<int64_t>(strlen(resolved_dir));
      const char dir_tail = (dir_len > 0) ? resolved_dir[dir_len - 1] : '\0';
      const bool dir_has_sep = ('/' == dir_tail || '\\' == dir_tail);
      const char path_tail = resolved_path[dir_len];
      if (OB_UNLIKELY(0 != strncmp(resolved_path, resolved_dir, dir_len))
          || OB_UNLIKELY(!dir_has_sep && '\0' != path_tail && '/' != path_tail && '\\' != path_tail)) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
  }
  return ret;
}
} // anonymous namespace

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
  if (OB_FAIL(expr.eval_param_value(ctx, location_datum, file_datum))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (location_datum->is_null() || file_datum->is_null()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("load_file parameter is null", K(ret));
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, parameter is null");
    expr_datum.set_null();
  } else {
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    ObIAllocator &tmp_alloc = tmp_alloc_g.get_allocator();
    ObString dir;
    ObString full_path;
    ObString content;
    // the first argument is the base directory (a plain path or a file:// url);
    // an empty string means the second argument is already a full path.
    if (OB_FAIL(get_location_dir(tmp_alloc, location_datum->get_string(), dir))) {
      LOG_WARN("failed to parse base dir", K(ret), K(location_datum->get_string()));
    } else if (dir.empty()) {
      full_path = file_datum->get_string();
      if (OB_FAIL(read_file_to_string(tmp_alloc, full_path.ptr(), content))) {
        LOG_WARN("failed to read file", K(ret), K(full_path));
        LOG_USER_ERROR(OB_FILE_NOT_EXIST, full_path.length(), full_path.ptr());
      } else if (OB_FAIL(ObAIFuncUtils::set_string_result(expr, ctx, expr_datum, content))) {
        LOG_WARN("failed to set string result", K(ret), K(content.length()));
      }
    } else {
      const ObString file_name = file_datum->get_string();
      const int64_t sep_len = (dir.length() > 0
          && (dir.ptr()[dir.length() - 1] == '/' || dir.ptr()[dir.length() - 1] == '\\')) ? 0 : 1;
      const int64_t path_len = dir.length() + sep_len + file_name.length();
      char *path_buf = NULL;
      if (OB_FAIL(check_relative_file_name(file_name))) {
        LOG_WARN("illegal file name for load_file", K(ret), K(file_name));
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, illegal file name");
      } else if (OB_UNLIKELY(path_len <= 0 || path_len > MAX_LOAD_FILE_PATH_LENGTH)
                 || OB_ISNULL(path_buf = static_cast<char *>(tmp_alloc.alloc(path_len + 1)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("alloc memory failed", K(ret), K(path_len));
      } else {
        int64_t pos = 0;
        MEMCPY(path_buf + pos, dir.ptr(), dir.length());
        pos += dir.length();
        if (sep_len > 0) {
          path_buf[pos++] = '/';
        }
        MEMCPY(path_buf + pos, file_name.ptr(), file_name.length());
        pos += file_name.length();
        path_buf[pos] = '\0';
        full_path.assign_ptr(path_buf, static_cast<int32_t>(pos));
        if (OB_FAIL(check_path_within_dir(dir, path_buf))) {
          LOG_WARN("file is out of the location directory", K(ret), K(full_path));
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "load_file, file is out of the location directory");
        } else if (OB_FAIL(read_file_to_string(tmp_alloc, path_buf, content))) {
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
    dir.reset();
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
    } else if (OB_UNLIKELY(size > MAX_LOAD_FILE_SIZE)) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("file is too large to be loaded", K(ret), K(size), K(MAX_LOAD_FILE_SIZE), K(path));
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
