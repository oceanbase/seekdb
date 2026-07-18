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

#ifndef OCEANBASE_STORAGE_OB_TABLET_LEAK_CHECKER
#define OCEANBASE_STORAGE_OB_TABLET_LEAK_CHECKER

#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_qsync_lock.h"

namespace oceanbase
{
namespace storage
{

class ObTenantMetaMemMgr;
class ObTabletLeakChecker;

class ObTabletHandleIndexMap final
{
public:
  static const int32_t LEAK_CHECKER_INITIAL_INDEX = -1;
  static const int64_t REF_ARRAY_SIZE = 1024;
  static const int64_t REF_BUCKET_SIZE = 32;
public:
  struct TabletHandleFootprint final
  {
  public:
    TabletHandleFootprint();
    TabletHandleFootprint(const char *file, const int line, const char *func);
    int assign(const TabletHandleFootprint &other);
    static const int32_t FOOTPRINT_LEN = 512;
    char footprint_[FOOTPRINT_LEN];
  };
  struct TabletHandleFootprintHashFunc final
  {
  public:
    int operator()(const TabletHandleFootprint &key, uint64_t &res) const
    {
      res = murmurhash(key.footprint_, static_cast<int32_t>(strlen(key.footprint_)), 0);
      return OB_SUCCESS;
    }
  };
  struct TabletHandleFootprintEqual final
  {
  public:
    bool operator()(const TabletHandleFootprint &lhs, const TabletHandleFootprint &rhs) const
    {
      return STRCMP(lhs.footprint_, rhs.footprint_) == 0;
    }
  };
  struct PrintToLogTraversal {
  public:
    PrintToLogTraversal() { MEMSET(temporal_ref_map_, 0, sizeof(temporal_ref_map_)); }
    int operator()(common::hash::HashMapTypes<
                   ObTabletHandleIndexMap::TabletHandleFootprint,
                   int32_t>::pair_type &pair);
    void set_data(int32_t index, int32_t ref_cnt) {
      temporal_ref_map_[index] = ref_cnt;
    }
  private:
    int32_t temporal_ref_map_[ObTabletHandleIndexMap::REF_ARRAY_SIZE];
  };
public:
  ObTabletHandleIndexMap();
  int reset();
  int init();
  int register_handle(const char *file, const int line,
                      const char *func, int32_t &index);
  int foreach(PrintToLogTraversal &op);
  static ObTabletHandleIndexMap* get_instance() {
    static ObTabletHandleIndexMap tb_handle_index_map;
    return &tb_handle_index_map;
  }

private:
  common::hash::ObHashMap<
      TabletHandleFootprint, int32_t, common::hash::SpinReadWriteDefendMode,
      TabletHandleFootprintHashFunc, TabletHandleFootprintEqual>
      tb_map_;
  common::ObQSyncLock rw_lock_;
  volatile int32_t max_index_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTabletHandleIndexMap);
};

class ObTabletLeakChecker final
{
public:
  ObTabletLeakChecker();
  int inc_ref(const int32_t index);
  int dec_ref(const int32_t index);
  void dump_pinned_tablet_info();
private:
  int32_t tb_ref_bucket_[ObTabletHandleIndexMap::REF_BUCKET_SIZE *
                         ObTabletHandleIndexMap::REF_ARRAY_SIZE];
private:
  DISALLOW_COPY_AND_ASSIGN(ObTabletLeakChecker);
};

}  // namespace storage
}  // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_LEAK_CHECKER
