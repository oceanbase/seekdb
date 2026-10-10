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

#ifndef OCEANBASE_STORAGE_OB_EXTERNAL_TABLET_CNT_MAP_H_
#define OCEANBASE_STORAGE_OB_EXTERNAL_TABLET_CNT_MAP_H_

#include "lib/hash/ob_hashmap.h"
#include "lib/container/ob_se_array.h"
#include "lib/lock/ob_bucket_lock.h"
#include "storage/meta_mem/ob_tablet_map_key.h"
#include <functional>

namespace oceanbase
{
namespace storage
{
class ObTablet;

class ObExternalTabletCntMap
{
public:
  ObExternalTabletCntMap();
  int init(const int64_t bucket_num);
  int check_exist(const ObDieingTabletMapKey &key, bool &exist);
  int reg_tablet(ObTablet &tablet);
  int unreg_tablet(ObTablet &tablet);
  // Visits borrowed objects while unregister/destruction is
  // excluded. The callback must not register/unregister or load another
  // tablet. This covers external copies only, not the main/retired maps.
  int scan(const std::function<int(const ObTablet &)> &visit);
  int64_t count() const { return ex_tablet_map_.size(); }
  void destroy();
private:
  bool is_inited_;
  common::ObBucketLock bucket_lock_;
  using Tablets = common::ObSEArray<ObTablet *, 1>;
  common::hash::ObHashMap<ObDieingTabletMapKey, Tablets> ex_tablet_map_;
  DISALLOW_COPY_AND_ASSIGN(ObExternalTabletCntMap);
};


}  // end namespace storage
}  // end namespace oceanbase

#endif /* OCEANBASE_STORAGE_OB_EXTERNAL_TABLET_CNT_MAP_H_ */
