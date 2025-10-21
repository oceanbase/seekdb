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

#ifndef OCEANBASE_SHARE_OB_ARCHIVE_COMPATIBLE_H_
#define OCEANBASE_SHARE_OB_ARCHIVE_COMPATIBLE_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{


// archive compatibility version
struct ObArchiveCompatible
{
  OB_UNIS_VERSION(1);

public:
  enum class Compatible : int64_t
  {
    NONE = 0,
    COMPATIBLE_VERSION_1,
    MAX_COMPATIBLE
  };

  Compatible version_;

  ObArchiveCompatible()
  {
    version_ = ObArchiveCompatible::get_current_compatible_version();
  }

  ObArchiveCompatible(const ObArchiveCompatible &other) : version_(other.version_) {}
  bool operator==(const ObArchiveCompatible &other) const
  {
    return version_ == other.version_;
  }

  bool operator!=(const ObArchiveCompatible &other) const
  {
    return !(*this == other);
  }

  void operator=(const ObArchiveCompatible &other)
  {
    version_ = other.version_;
  }

  bool is_valid() const;
  int set_version(int64_t compatible);
  static bool is_valid(int64_t compatible);
  static ObArchiveCompatible::Compatible get_current_compatible_version()
  {
    return ObArchiveCompatible::Compatible::COMPATIBLE_VERSION_1;
  }

  TO_STRING_KV(K_(version));
};



}
}

#endif
