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

#define USING_LOG_PREFIX STORAGE
#include "ob_file_system_router.h"
#include "share/ob_io_device_helper.h"
#include "lib/file/file_directory_utils.h"
#include "lib/string/ob_sql_string.h"
#ifdef _WIN32
#include "lib/file/windows_file_path.h"
#include "lib/allocator/ob_allocator.h"
#endif

namespace oceanbase {
using namespace common;
using namespace share;
using namespace blocksstable;
namespace storage {
/**
 * -------------------------------------ObFileSystemRouter-----------------------------------------
 */
ObFileSystemRouter & ObFileSystemRouter::get_instance()
{
  static ObFileSystemRouter instance_;
  return instance_;
}

ObFileSystemRouter::ObFileSystemRouter()
{
  data_dir_.reset();
  slog_dir_.reset();
  clog_dir_.reset();
  sstable_dir_.reset();

  clog_file_spec_.retry_write_policy_ = "normal";
  clog_file_spec_.log_create_policy_ = "normal";
  clog_file_spec_.log_write_policy_ = "truncate";

  slog_file_spec_.retry_write_policy_ = "normal";
  slog_file_spec_.log_create_policy_ = "normal";
  slog_file_spec_.log_write_policy_ = "truncate";

  svr_seq_ = 0;
  is_inited_ = false;
}

void ObFileSystemRouter::reset()
{
#ifdef _WIN32
  instance_root_.reset();
#endif
  data_dir_.reset();
  slog_dir_.reset();
  clog_dir_.reset();
  sstable_dir_.reset();

  clog_file_spec_.retry_write_policy_ = "normal";
  clog_file_spec_.log_create_policy_ = "normal";
  clog_file_spec_.log_write_policy_ = "truncate";

  slog_file_spec_.retry_write_policy_ = "normal";
  slog_file_spec_.log_create_policy_ = "normal";
  slog_file_spec_.log_write_policy_ = "truncate";

  svr_seq_ = 0;
  is_inited_ = false;
}

int ObFileSystemRouter::init(const char *data_dir, const char *redo_dir, const char *instance_root)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_ISNULL(data_dir) || OB_ISNULL(redo_dir) || OB_ISNULL(instance_root)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(init_local_dirs(data_dir, redo_dir, instance_root))) {
  } else {
    clog_file_spec_.retry_write_policy_ = "normal";
    clog_file_spec_.log_create_policy_ = "normal";
    clog_file_spec_.log_write_policy_ = "truncate";

    slog_file_spec_.retry_write_policy_ = "normal";
    slog_file_spec_.log_create_policy_ = "normal";
    slog_file_spec_.log_write_policy_ = "truncate";

    is_inited_ = true;
  }

  if (IS_NOT_INIT) {
    reset();
  }
  return ret;
}

int ObFileSystemRouter::get_server_clog_dir(ObSqlString &server_clog_dir)
{
  return server_clog_dir.assign_fmt("%s/sys", clog_dir_.ptr());
}

int ObFileSystemRouter::init_local_dirs(const char* data_dir, const char* redo_dir, const char *instance_root)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  ObArenaAllocator allocator;
  WindowsFilePath root(allocator), data(allocator), redo(allocator);
  WindowsFilePath slog(allocator), sstable(allocator), server_clog(allocator);
  ObSqlString input;
  auto resolve = [&](const char *path, WindowsFilePath &output) -> int {
    int result = OB_SUCCESS;
    // CLI drive-relative paths were fixed by startup; ordinary relative
    // configuration paths retain their logical instance-root base.
    if (path[0] == '\0') {
      result = OB_INVALID_ARGUMENT;
    } else if (path[0] == '/' || path[0] == '\\'
               || (path[0] != '\0' && path[1] == ':')) {
      result = output.assign(path);
    } else if (OB_SUCCESS != (result = input.assign_fmt("%s/%s", root.utf8(), path))) {
    } else {
      result = output.assign(input.ptr());
    }
    return result;
  };
  auto same = [](const WindowsFilePath &a, const WindowsFilePath &b) {
    return CSTR_EQUAL == CompareStringOrdinal(a.wide(), -1, b.wide(), -1, TRUE);
  };
  if (OB_FAIL(root.assign(instance_root))) {
  } else if (OB_FAIL(resolve(data_dir, data))) {
  } else if (OB_FAIL(resolve(redo_dir, redo))) {
  } else if (same(root, data) || same(root, redo) || same(data, redo)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("storage directories must differ from instance root and each other", K(ret));
  } else if (OB_FAIL(input.assign_fmt("%s/slog", data.utf8()))) {
  } else if (OB_FAIL(slog.assign(input.ptr()))) {
  } else if (OB_FAIL(input.assign_fmt("%s/sstable", data.utf8()))) {
  } else if (OB_FAIL(sstable.assign(input.ptr()))) {
  } else if (OB_FAIL(input.assign_fmt("%s/sys", redo.utf8()))) {
  } else if (OB_FAIL(server_clog.assign(input.ptr()))) {
  } else if (OB_FAIL(instance_root_.assign(root.utf8()))) {
  } else if (OB_FAIL(data_dir_.assign(data.utf8()))) {
  } else if (OB_FAIL(clog_dir_.assign(redo.utf8()))) {
  } else if (OB_FAIL(slog_dir_.assign(slog.utf8()))) {
  } else if (OB_FAIL(sstable_dir_.assign(sstable.utf8()))) {
  } else if (OB_FAIL(data.create_directory(true))) {
  } else if (OB_FAIL(redo.create_directory(true))) {
  } else if (OB_FAIL(slog.create_directory(true))) {
  } else if (OB_FAIL(sstable.create_directory(true))) {
  }
#else
  UNUSED(instance_root);
  char work_directory[MAX_PATH_SIZE] = {0};
  if (nullptr == getcwd(work_directory, MAX_PATH_SIZE)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get work directory failed", K(ret), KCSTRING(strerror(errno)));
  }

  ObSqlString tmp_dir;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(tmp_dir.assign(data_dir))) {
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(tmp_dir.ptr()))) {
    } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(tmp_dir))) {
    } else if (0 == strcmp(work_directory, tmp_dir.ptr())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("data dir is same as work directory", K(ret), K(tmp_dir), KCSTRING(work_directory));
    } else {
      if (OB_FAIL(data_dir_.assign(tmp_dir.ptr()))) {
        LOG_ERROR("construct data dir fail", K(ret), K(tmp_dir));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(slog_dir_.assign_fmt("%s/slog", data_dir_.ptr()))) {
      LOG_ERROR("construct slog path fail", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(slog_dir_.ptr()))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(tmp_dir.assign(redo_dir))) {
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(tmp_dir.ptr()))) {
    } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(tmp_dir))) {
    } else if (0 == strcmp(work_directory, tmp_dir.ptr())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("clog/redo dir is same as work directory", K(ret), K(tmp_dir), KCSTRING(work_directory));
    } else if (0 == strcmp(tmp_dir.ptr(), data_dir_.ptr())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("clog/redo dir is same as data dir", K(ret), K(tmp_dir), KCSTRING(data_dir_.ptr()));
    } else {
      if (OB_FAIL(clog_dir_.assign(tmp_dir.ptr()))) {
        LOG_ERROR("construct clog/redo dir fail", K(ret), K(tmp_dir));
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(sstable_dir_.assign_fmt("%s/sstable", data_dir_.ptr()))) {
      LOG_ERROR("construct sstable path fail", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(sstable_dir_.ptr()))) {
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("succeed to construct local dir",
      KCSTRING(data_dir_.ptr()), KCSTRING(slog_dir_.ptr()), KCSTRING(clog_dir_.ptr()), KCSTRING(sstable_dir_.ptr()));
  }

#endif
  return ret;
}

} // namespace storage
} // namespace oceanbase
