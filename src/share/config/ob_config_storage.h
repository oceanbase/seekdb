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

#ifndef OCEANBASE_SHARE_CONFIG_OB_CONFIG_STORAGE_H_
#define OCEANBASE_SHARE_CONFIG_OB_CONFIG_STORAGE_H_

#include "lib/string/ob_string.h"
#include "lib/allocator/ob_allocator.h"
#include "auto_config.h"

namespace oceanbase
{
namespace common
{
inline constexpr char AUTO_CONFIG_PATH[] = "./etc/seekdb.auto.conf";

class ObConfigStorage
{
public:
  ObConfigStorage() : inited_(false) {}
  ~ObConfigStorage() = default;

  int init();
  int load_active_checked(bool startup, AutoConfigEntryCallback callback, void *context);
  int save_config(const char *name, const char *value, bool *after_replace = nullptr);
  int reset_config(const char *name, bool *after_replace = nullptr);
  int update_checked(const char *name, const char *value, bool reset,
                     AutoConfigCheckCallback callback, void *context,
                     bool *after_replace);

  bool is_inited() const { return inited_; }

private:
  bool inited_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigStorage);
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_STORAGE_H_
