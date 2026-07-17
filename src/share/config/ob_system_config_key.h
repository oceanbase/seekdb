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

#ifndef OCEANBASE_SHARE_CONFIG_OB_SYSTEM_CONFIG_KEY_H_
#define OCEANBASE_SHARE_CONFIG_OB_SYSTEM_CONFIG_KEY_H_

#include "share/ob_define.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/string/ob_string.h"
#include "lib/utility/utility.h"
#include "common/ob_zone.h"

namespace oceanbase
{
namespace common
{
class ObSystemConfigKey
{
public:
  ObSystemConfigKey();
  virtual ~ObSystemConfigKey() {}
  // with exactly equal comparision
  bool operator == (const ObSystemConfigKey &other) const;
  uint64_t hash() const;
  inline int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  // check if key's information can fit this. i.e. can use the `key' to get config accordingly
  bool match(const ObSystemConfigKey &key) const;
  // set config name
  void set_name(const ObString &name);
  void set_name(const char *name);
  const char *name() const { return name_; }

private:
  static const char *DEFAULT_VALUE;
  char name_[OB_MAX_CONFIG_NAME_LEN];
  // ObSystemConfig uses the object's copy constructor in ObHashMap, cannot be prohibited
  //DISALLOW_COPY_AND_ASSIGN(ObSystemConfigKey);
};

inline ObSystemConfigKey::ObSystemConfigKey()
{
  MEMSET(name_, 0, OB_MAX_CONFIG_NAME_LEN);
}

inline bool ObSystemConfigKey::operator == (const ObSystemConfigKey &other) const
{
  return (this == &other
          || (0 == STRCMP(name_, other.name_)));
}

inline bool ObSystemConfigKey::match(const ObSystemConfigKey &other) const
{
  bool ret = false;
  ret = (this == &other
         || (0 == STRCMP(name_, other.name_)));
  return ret;
}

inline void ObSystemConfigKey::set_name(const ObString &name)
{
  int64_t name_length = name.length();
  if (name_length >= OB_MAX_CONFIG_NAME_LEN) {
    name_length = OB_MAX_CONFIG_NAME_LEN;
  }
  int64_t pos = 0;
  (void) databuff_printf(name_, OB_MAX_CONFIG_NAME_LEN, pos, "%.*s",
                         static_cast<int>(name_length), name.ptr());
}

inline void ObSystemConfigKey::set_name(const char *name)
{
  set_name(ObString::make_string(name));
}

inline uint64_t ObSystemConfigKey::hash() const
{
  uint64_t hash = murmurhash(this, (int32_t) sizeof(*this), 0);
  return hash;
}
} // end of namespace common
} // end of namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_SYSTEM_CONFIG_KEY_H_
