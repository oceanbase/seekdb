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

#ifndef OCEANBASE_STORAGE_OB_TABLET_FULL_MEDIUM_INFO
#define OCEANBASE_STORAGE_OB_TABLET_FULL_MEDIUM_INFO

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "storage/compaction/ob_compaction_util.h"
#include "storage/compaction/ob_extra_medium_info.h"
#include "storage/tablet/ob_tablet_dumped_medium_info.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace storage
{
class ObTabletFullMediumInfo
{
public:
  ObTabletFullMediumInfo();
  ~ObTabletFullMediumInfo() = default;
  ObTabletFullMediumInfo(const ObTabletFullMediumInfo &) = delete;
  ObTabletFullMediumInfo &operator=(const ObTabletFullMediumInfo &) = delete;
public:
  void reset();
  int assign(common::ObIAllocator &allocator, const ObTabletFullMediumInfo &other);

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(common::ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

  TO_STRING_KV(K_(extra_medium_info), K_(medium_info_list));
public:
  compaction::ObExtraMediumInfo extra_medium_info_;
  ObTabletDumpedMediumInfo medium_info_list_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_FULL_MEDIUM_INFO
