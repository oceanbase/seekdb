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

#ifndef OCEANBASE_STORAGE_OB_TABLET_DUMPED_MEDIUM_INFO
#define OCEANBASE_STORAGE_OB_TABLET_DUMPED_MEDIUM_INFO

#include <stdint.h>
#include "lib/container/ob_se_array.h"
#include "lib/utility/ob_print_utils.h"
#include "storage/compaction/ob_medium_compaction_info.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}

namespace storage
{
namespace mds
{
struct MdsDumpKey;
struct MdsDumpNode;
}

class ObTabletDumpedMediumInfo
{
public:
  typedef common::ObSEArray<compaction::ObMediumCompactionInfo*, 1>::iterator iterator;
public:
  ObTabletDumpedMediumInfo();
  ~ObTabletDumpedMediumInfo();
  ObTabletDumpedMediumInfo(const ObTabletDumpedMediumInfo &) = delete;
  ObTabletDumpedMediumInfo &operator=(const ObTabletDumpedMediumInfo &) = delete;
public:
  int init_for_first_creation(common::ObIAllocator &allocator);
  int init_for_evict_medium_info(
      common::ObIAllocator &allocator,
      const int64_t finish_medium_scn,
      const ObTabletDumpedMediumInfo &other);
  int init_for_mds_table_dump(
      common::ObIAllocator &allocator,
      const int64_t finish_medium_scn,
      const ObTabletDumpedMediumInfo &other1,
      const ObTabletDumpedMediumInfo &other2);
  void reset();
  bool empty() const { return medium_info_list_.empty(); }
  common::ObIArray<compaction::ObMediumCompactionInfo*> &get_array() { return medium_info_list_; }
  void sort_array() { lib::ob_sort(medium_info_list_.begin(), medium_info_list_.end(), compare); }
  // key order in array: big -> small
  int append(
      const mds::MdsDumpKey &key,
      const mds::MdsDumpNode &node);
  int append(const compaction::ObMediumCompactionInfo &medium_info);
  int assign(common::ObIAllocator &allocator, const ObTabletDumpedMediumInfo &other);
  bool is_valid() const;


  int64_t get_max_medium_snapshot() const;

  int is_contain(const compaction::ObMediumCompactionInfo &info, bool &contain) const;

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(common::ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

  int64_t to_string(char* buf, const int64_t buf_len) const;
  int64_t simple_to_string(char* buf, const int64_t buf_len, int64_t &pos) const;
public:
  static bool compare(const compaction::ObMediumCompactionInfo *lhs, const compaction::ObMediumCompactionInfo *rhs);
private:
  int do_append(const compaction::ObMediumCompactionInfo &medium_info);
public:
  bool is_inited_;
  common::ObIAllocator *allocator_;
  common::ObSEArray<compaction::ObMediumCompactionInfo*, 1> medium_info_list_;
};

class ObTabletDumpedMediumInfoIterator
{
public:
  ObTabletDumpedMediumInfoIterator();
  ~ObTabletDumpedMediumInfoIterator();
  ObTabletDumpedMediumInfoIterator(const ObTabletDumpedMediumInfoIterator &) = delete;
  ObTabletDumpedMediumInfoIterator &operator=(const ObTabletDumpedMediumInfoIterator &) = delete;
public:
  int init(
      common::ObIAllocator &allocator,
      const ObTabletDumpedMediumInfo *dumped_medium_info);
  void reset();
  int get_next_key(compaction::ObMediumCompactionInfoKey &key);
private:
  bool is_inited_;
  int64_t idx_;
  common::ObIAllocator *allocator_;
  common::ObSEArray<compaction::ObMediumCompactionInfo*, 1> medium_info_list_;
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_DUMPED_MEDIUM_INFO
