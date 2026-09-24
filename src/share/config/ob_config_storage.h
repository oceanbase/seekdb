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

#include <string>
#include <vector>

#include "lib/string/ob_string.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace common
{

class ObConfigStorage
{
public:
  struct Entry
  {
    std::string name;
    std::string value;
    uint32_t line;
  };

  ObConfigStorage() : inited_(false) {}
  ~ObConfigStorage() = default;

  int init();
  int load_all_configs(std::vector<Entry> &entries);
  int get_config_value(const char *name, ObString &value, common::ObIAllocator &allocator);
  int save_config(const char *name, const char *value);
  int reset_config(const char *name);

  bool is_inited() const { return inited_; }

private:
  bool inited_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigStorage);
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_STORAGE_H_
