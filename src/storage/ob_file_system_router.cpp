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

namespace oceanbase {
using namespace common;
using namespace share;
using namespace rootserver;
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
  data_dir_[0] = '\0';
  slog_dir_[0] = '\0';
  clog_dir_[0] = '\0';
  sstable_dir_[0] = '\0';

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
  data_dir_[0] = '\0';
  slog_dir_[0] = '\0';
  clog_dir_[0] = '\0';
  sstable_dir_[0] = '\0';

  clog_file_spec_.retry_write_policy_ = "normal";
  clog_file_spec_.log_create_policy_ = "normal";
  clog_file_spec_.log_write_policy_ = "truncate";

  slog_file_spec_.retry_write_policy_ = "normal";
  slog_file_spec_.log_create_policy_ = "normal";
  slog_file_spec_.log_write_policy_ = "truncate";

  svr_seq_ = 0;
  is_inited_ = false;
}

int ObFileSystemRouter::init(const char *data_dir, const char *redo_dir)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_ISNULL(data_dir)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret));
  } else if (OB_FAIL(init_local_dirs(data_dir, redo_dir))) {
    LOG_WARN("init local dir fail", K(ret), KCSTRING(data_dir), KCSTRING(redo_dir));
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

int ObFileSystemRouter::get_tenant_clog_dir(
    const uint64_t tenant_id,
    char (&tenant_clog_dir)[common::MAX_PATH_SIZE])
{
  int ret = OB_SUCCESS;
  int pret = 0;
  if (OB_SYS_TENANT_ID != tenant_id) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("not support tenant clog dir of system tenant", K(ret), K(tenant_id));
  } else {
    pret = snprintf(tenant_clog_dir, MAX_PATH_SIZE, "%s/sys", clog_dir_);
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("construct tenant clog path fail", K(ret), K(tenant_id));
    }
  }
  return ret;
}

int ObFileSystemRouter::init_local_dirs(const char* data_dir, const char* redo_dir)
{
  int ret = OB_SUCCESS;
  int pret = 0;

  char work_directory[MAX_PATH_SIZE] = {0};
  if (nullptr == getcwd(work_directory, MAX_PATH_SIZE)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get work directory failed", K(ret), KCSTRING(strerror(errno)));
  }

  ObSqlString tmp_dir;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(tmp_dir.assign(data_dir))) {
      LOG_ERROR("assign data dir failed", K(ret), KCSTRING(data_dir));
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(tmp_dir.ptr()))) {
      LOG_ERROR("create full path failed", K(ret), K(tmp_dir));
    } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(tmp_dir))) {
      LOG_ERROR("convert data dir to absolute path failed", K(ret), K(tmp_dir));
    } else if (0 == strcmp(work_directory, tmp_dir.ptr())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("data dir is same as work directory", K(ret), K(tmp_dir), KCSTRING(work_directory));
    } else {
      pret = snprintf(data_dir_, MAX_PATH_SIZE, "%s", tmp_dir.ptr());
      if (pret < 0 || pret >= MAX_PATH_SIZE) {
        ret = OB_BUF_NOT_ENOUGH;
        LOG_ERROR("construct data dir fail", K(ret), K(tmp_dir));
      }
    }
  }

  if (OB_SUCC(ret)) {
    pret = snprintf(slog_dir_, MAX_PATH_SIZE, "%s/slog", data_dir_);
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("construct slog path fail", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(slog_dir_))) {
      LOG_ERROR("create full path failed", K(ret), KCSTRING(slog_dir_));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(tmp_dir.assign(redo_dir))) {
      LOG_ERROR("assign clog/redo dir failed", K(ret), KCSTRING(redo_dir));
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(tmp_dir.ptr()))) {
      LOG_ERROR("create full path failed", K(ret), K(tmp_dir));
    } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(tmp_dir))) {
      LOG_ERROR("convert clog/redo dir to absolute path failed", K(ret), K(tmp_dir));
    } else if (0 == strcmp(work_directory, tmp_dir.ptr())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("clog/redo dir is same as work directory", K(ret), K(tmp_dir), KCSTRING(work_directory));
    } else if (0 == strcmp(tmp_dir.ptr(), data_dir_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("clog/redo dir is same as data dir", K(ret), K(tmp_dir), KCSTRING(data_dir_));
    } else {
      pret = snprintf(clog_dir_, MAX_PATH_SIZE, "%s", tmp_dir.ptr());
      if (pret < 0 || pret >= MAX_PATH_SIZE) {
        ret = OB_BUF_NOT_ENOUGH;
        LOG_ERROR("construct clog/redo dir fail", K(ret), K(tmp_dir));
      }
    }
  }

  if (OB_SUCC(ret)) {
    pret = snprintf(sstable_dir_, MAX_PATH_SIZE, "%s/sstable", data_dir_);
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("construct sstable path fail", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(FileDirectoryUtils::create_full_path(sstable_dir_))) {
      LOG_ERROR("create full path failed", K(ret), KCSTRING(sstable_dir_));
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("succeed to construct local dir",
      KCSTRING(data_dir_), KCSTRING(slog_dir_), KCSTRING(clog_dir_), KCSTRING(sstable_dir_));
  }

  return ret;
}

} // namespace storage
} // namespace oceanbase
