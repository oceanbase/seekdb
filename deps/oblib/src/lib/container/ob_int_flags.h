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

#ifndef OCEANBASE_LIB_OB_INT_FLAGS_
#define OCEANBASE_LIB_OB_INT_FLAGS_

#include <stdint.h>
#include "lib/utility/utility.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
struct ObInt64Flags
{
  ObInt64Flags() : flags_(0)
  { }
  ObInt64Flags(int64_t flags): flags_(flags)
  { }
  virtual ~ObInt64Flags()
  { }

  bool empty() const
  { return 0 == flags_; }

  void reset()
  { flags_ = 0; }

  inline bool add_member(int64_t index);
  inline bool del_member(int64_t index);
  inline bool has_member(int64_t index);

  TO_STRING_KV("flags", PHEX(&flags_, sizeof(flags_)));
private:
  int64_t flags_;
};

bool ObInt64Flags::add_member(int64_t index)
{
  bool bret = true;
  if (index >= 0 && index < static_cast<int64_t>(sizeof(flags_))) {
    flags_ |= 0x1 << index;
  } else {
    bret = false;
  }
  return bret;
}

bool ObInt64Flags::del_member(int64_t index)
{
  bool bret = true;
  if (index >= 0 && index < static_cast<int64_t>(sizeof(flags_))) {
    flags_ &= ~(0x1 << index);
  } else {
    bret = false;
  }
  return bret;
}

bool ObInt64Flags::has_member(int64_t index)
{
  bool bret = false;
  if (index >= 0 && index < static_cast<int64_t>(sizeof(flags_))) {
    bret = flags_ & (0x1 << index);
  } else {
    bret = false;
  }
  return bret;
}

}
}
#endif
