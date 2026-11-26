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

#pragma once

#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace logservice
{
class ObLogService;
class ObServerLogBlockMgr
{
public:
  typedef int (*GetLogDiskInfoInConfig)(int64_t &log_disk_size,
                                        int64_t &log_disk_percentage,
                                        int64_t &total_log_disk_size);
  ObServerLogBlockMgr();
  ~ObServerLogBlockMgr();
  static int check_clog_directory_is_empty(const char *clog_dir, bool &result);
  int init(GetLogDiskInfoInConfig get_log_disk_info_in_config);
  void bind_log_service(ObLogService &log_service)
  {
    log_service_ = &log_service;
  }
  void unbind_log_service(ObLogService &log_service)
  {
    if (log_service_ == &log_service) {
      log_service_ = nullptr;
    }
  }
  int start(const int64_t log_disk_size);
  void destroy();
  int get_disk_usage(int64_t &in_use_size_byte);
  int64_t get_log_disk_size();
  int update_log_disk_size(const int64_t old_log_disk_size,
                           const int64_t new_log_disk_size,
                           int64_t &allowed_log_disk_size,
                           ObLogService *log_service);
  TO_STRING_KV(K_(is_inited));

private:
  bool check_space_is_enough_(const int64_t log_disk_size) const;
  int get_runtime_log_disk_size_(int64_t &log_disk_size) const;

private:
  GetLogDiskInfoInConfig get_log_disk_info_in_config_func_;
  ObLogService *log_service_;
  bool is_inited_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObServerLogBlockMgr);
};

} // namespace logservice
} // namespace oceanbase
