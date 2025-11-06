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

#ifndef OB_PLUGIN_HELPER_H_
#define OB_PLUGIN_HELPER_H_

#include <dlfcn.h>

#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{

constexpr int OB_PLUGIN_NAME_LENGTH = 64;

class ObPluginName final
{
public:
  ObPluginName() { memset(name_, 0x0, OB_PLUGIN_NAME_LENGTH); }
  explicit ObPluginName(const char *name) { OB_ASSERT(common::OB_SUCCESS == set_name(name)); }
  ~ObPluginName() = default;

  int set_name(const char *name);
  int set_name(const ObString &name);

  OB_INLINE bool is_valid() const { return STRLEN(name_) > 0; }
  OB_INLINE int len() const { return STRLEN(name_); }
  OB_INLINE char *str() { return name_; }
  OB_INLINE const char *str() const { return name_; }
  OB_INLINE int hash(uint64_t &value) const
  {
    value = murmurhash(name_, static_cast<int32_t>(STRLEN(name_)), 0);
    return OB_SUCCESS;
  }

  OB_INLINE bool operator ==(const ObPluginName &other) const
  {
    return 0 == STRCMP(name_, other.name_);
  }
  OB_INLINE bool operator !=(const ObPluginName &other) const
  {
    return 0 != STRCMP(name_, other.name_);
  }
  OB_INLINE bool operator <(const ObPluginName &other) const
  {
    return 0 > STRCMP(name_, other.name_);
  }
  TO_STRING_KV(K_(name));
private:
  char name_[OB_PLUGIN_NAME_LENGTH];
};

} // end namespace share
} // end namespace oceanbase

#endif // OB_PLUGIN_HELPER_H_
