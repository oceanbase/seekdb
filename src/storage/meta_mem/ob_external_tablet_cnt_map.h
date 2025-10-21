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
#include "lib/lock/ob_bucket_lock.h"
#include "storage/meta_mem/ob_tablet_map_key.h"

namespace oceanbase
{
namespace storage
{

class ObExternalTabletCntMap
{
public:
  ObExternalTabletCntMap();
  int init(const int64_t bucket_num, const uint64_t tenant_id);
  int check_exist(const ObDieingTabletMapKey &key, bool &exist);
  int reg_tablet(const ObDieingTabletMapKey &key);
  int unreg_tablet(const ObDieingTabletMapKey &key);
  int64_t count() const { return ex_tablet_map_.size(); }
  void destroy();
private:
  bool is_inited_;
  common::ObBucketLock bucket_lock_;
  common::hash::ObHashMap<ObDieingTabletMapKey, int64_t> ex_tablet_map_;
  DISALLOW_COPY_AND_ASSIGN(ObExternalTabletCntMap);
};


}  // end namespace storage
}  // end namespace oceanbase

#endif /* OCEANBASE_STORAGE_OB_EXTERNAL_TABLET_CNT_MAP_H_ */
