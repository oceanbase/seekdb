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

#ifndef OCEANBASE_STORAGE_OB_FLYING_POINTER_MAP_H_
#define OCEANBASE_STORAGE_OB_FLYING_POINTER_MAP_H_

#include "storage/meta_mem/ob_meta_obj_struct.h"
#include "storage/meta_mem/ob_tablet_map_key.h"
#include "storage/meta_mem/ob_tablet_pointer.h"
#include "storage/meta_mem/ob_tablet_pointer_handle.h"
#include "storage/ob_resource_map.h"

namespace oceanbase
{
namespace storage
{
class ObFlyingTabletPointerMap final
{
  friend class ObTenantMetaMemMgr;
  typedef ObTabletPointerHandle* ObInnerTPHandlePtr;
public:
  ObFlyingTabletPointerMap(const int64_t capacity);
  int init(const uint64_t tenant_id);
  int set(const ObDieingTabletMapKey &key, ObTabletPointerHandle &handle);
  int check_exist(const ObDieingTabletMapKey &key, bool &is_exist);
  int erase(const ObDieingTabletMapKey &key);
  int64_t count() const { return map_.size(); }
  void destroy();
private:
  int inner_erase_(const ObDieingTabletMapKey &key);
private:
  bool is_inited_;
  int64_t capacity_;
  common::ObBucketLock bucket_lock_;
  common::hash::ObHashMap<ObDieingTabletMapKey, ObTabletPointerHandle> map_;
  DISALLOW_COPY_AND_ASSIGN(ObFlyingTabletPointerMap);
};

}  // end namespace storage
}  // end namespace oceanbase

#endif /* OCEANBASE_STORAGE_OB_FLYING_POINTER_MAP_H_ */
