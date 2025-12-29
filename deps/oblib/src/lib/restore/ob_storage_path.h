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

#include "lib/alloc/alloc_assist.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"

#ifndef OCEANBASE_COMMON_STORAGE_PATH_H_
#define OCEANBASE_COMMON_STORAGE_PATH_H_

namespace oceanbase
{
namespace common
{

static const char *const BACKUP_INFO_TENANT_ID = "tenant_id";
static const char *const BACKUP_BASE_DATA = "base_data";
static const char *const BACKUP_ALL_TENANT_ID_LIST = "all_tenant_id_list";

class ObStoragePath
{
public:
  ObStoragePath();
  virtual ~ObStoragePath();
  int init(const common::ObString &uri);
  int join(const common::ObString &path);
  int join_simple_object_definition(uint64_t object_id);

  int64_t to_string(char *buf, const int64_t buf_len) const;

  const char *get_string() const { return path_; }
  int64_t get_length() const { return cur_pos_; }
  common::ObString get_obstring() const { return common::ObString(cur_pos_, path_); }
  void reset() { cur_pos_ = 0; }
  bool is_valid() const { return cur_pos_ > 0; }

private:
  int trim_right_delim();
  int trim_left_delim(const common::ObString &path, int64_t &delim_pos);
  char path_[common::OB_MAX_URI_LENGTH];
  int64_t cur_pos_;
};

}//common
}//oceanbase

#endif /* OCEANBASE_COMMON_STORAGE_PATH_H_ */
