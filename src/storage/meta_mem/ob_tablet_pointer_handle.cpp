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

#include "ob_tablet_pointer_handle.h"
#include "storage/meta_mem/ob_tablet_pointer_map.h"

namespace oceanbase
{
namespace storage
{

ObTabletPointerHandle::ObTabletPointerHandle()
  : ObResourceHandle<ObTabletPointer>::ObResourceHandle(),
    map_(nullptr)
{
}

ObTabletPointerHandle::ObTabletPointerHandle(ObTabletPointerMap &map)
    : ObResourceHandle<ObTabletPointer>::ObResourceHandle(),
      map_(&map)
{
}

ObTabletPointerHandle::ObTabletPointerHandle(
    ObResourceValueStore<ObTabletPointer> *ptr,
    ObTabletPointerMap *map)
    : ObResourceHandle<ObTabletPointer>::ObResourceHandle(),
      map_(map)
{
  abort_unless(common::OB_SUCCESS == set(ptr, map));
}

ObTabletPointerHandle::~ObTabletPointerHandle()
{
  reset();
}

void ObTabletPointerHandle::reset()
{
  int ret = common::OB_SUCCESS;
  if (nullptr != ObResourceHandle<ObTabletPointer>::ptr_) {
    if (nullptr == map_) {
      STORAGE_LOG(ERROR, "map is null", K(ret), KP_(map));
    } else if (OB_FAIL(map_->dec_handle_ref(ObResourceHandle<ObTabletPointer>::ptr_))) {
      STORAGE_LOG(WARN, "fail to decrease handle reference count", K(ret));
    } else {
      ObResourceHandle<ObTabletPointer>::ptr_ = nullptr;
    }
  }
}

bool ObTabletPointerHandle::is_valid() const
{
  return nullptr != ObResourceHandle<ObTabletPointer>::ptr_
      && nullptr != ObResourceHandle<ObTabletPointer>::ptr_->get_value_ptr()
      && nullptr != map_;
}

int ObTabletPointerHandle::assign(const ObTabletPointerHandle &other)
{
  int ret = common::OB_SUCCESS;
  if (this != &other) {
    if (OB_FAIL(set(other.ptr_, other.map_))) {
      STORAGE_LOG(WARN, "failed to set member", K(ret), K(other));
    }
  }
  return ret;
}

int ObTabletPointerHandle::set(
    ObResourceValueStore<ObTabletPointer> *ptr,
    ObTabletPointerMap *map)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(ptr) || OB_ISNULL(map)) {
    ret = common::OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), KP(ptr), KP(map));
  } else {
    reset();
    if (OB_FAIL(map->inc_handle_ref(ptr))) {
      STORAGE_LOG(WARN, "fail to inc tablet poiner", K(ret), KP(ptr), KP(map));
    } else {
      ObResourceHandle<ObTabletPointer>::ptr_ = ptr;
      map_ = map;
    }
  }
  return ret;
}


} // end namespace storage
} // end namespace oceanbase;
