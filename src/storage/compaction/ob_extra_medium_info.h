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

#ifndef OCEANBASE_COMPACTION_OB_EXTRA_MEDIUM_INFO
#define OCEANBASE_COMPACTION_OB_EXTRA_MEDIUM_INFO

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace compaction
{
class ObExtraMediumInfo
{
public:
  ObExtraMediumInfo();
  ~ObExtraMediumInfo() = default;
  ObExtraMediumInfo(const ObExtraMediumInfo &other);
  ObExtraMediumInfo &operator=(const ObExtraMediumInfo &other);
public:
  void reset();

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

  TO_STRING_KV(K_(info), K_(compat), K_(last_compaction_type),
               K_(wait_check_flag), K_(last_medium_scn));

public:
  static constexpr int64_t MEDIUM_LIST_VERSION_V1 = 1;
  static constexpr int32_t MEDIUM_LIST_INFO_RESERVED_BITS = 51;
public:
  union
  {
    uint64_t info_;
    struct
    {
      uint64_t compat_                  : 8;
      uint64_t last_compaction_type_    : 4; // check inner_table when last_compaction is major
      uint64_t wait_check_flag_         : 1; // true: need check finish, false: no need check
      uint64_t reserved_                : MEDIUM_LIST_INFO_RESERVED_BITS;
    };
  };
  int64_t last_medium_scn_;
};
} // namespace compaction
} // namespace oceanbase

#endif // OCEANBASE_COMPACTION_OB_EXTRA_MEDIUM_INFO
