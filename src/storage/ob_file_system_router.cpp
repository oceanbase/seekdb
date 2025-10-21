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
  pret = snprintf(tenant_clog_dir, MAX_PATH_SIZE, "%s/tenant_%" PRIu64,
                  clog_dir_, tenant_id);
  if (pret < 0 || pret >= MAX_PATH_SIZE) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_ERROR("construct tenant clog path fail", K(ret), K(tenant_id));
  }
  return ret;
}

int ObFileSystemRouter::init_local_dirs(const char* data_dir, const char* redo_dir)
{
  int ret = OB_SUCCESS;
  int pret = 0;

  if (OB_SUCC(ret)) {
    pret = snprintf(data_dir_, MAX_PATH_SIZE, "%s", data_dir);
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("construct data dir fail", K(ret), KCSTRING(data_dir));
    }
  }

  if (OB_SUCC(ret)) {
    pret = snprintf(slog_dir_, MAX_PATH_SIZE, "%s/slog", data_dir);
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("construct slog path fail", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    pret = snprintf(clog_dir_, MAX_PATH_SIZE, "%s", redo_dir);
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("construct clog path fail", K(ret), KCSTRING(redo_dir));
    }
  }

  if (OB_SUCC(ret)) {
    pret = snprintf(sstable_dir_, MAX_PATH_SIZE, "%s/sstable", data_dir);
    if (pret < 0 || pret >= MAX_PATH_SIZE) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_ERROR("construct sstable path fail", K(ret));
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
