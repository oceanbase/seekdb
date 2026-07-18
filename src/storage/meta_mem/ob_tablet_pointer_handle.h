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

#ifndef OCEANBASE_STORAGE_OB_TABLET_POINTER_HANDLE_H
#define OCEANBASE_STORAGE_OB_TABLET_POINTER_HANDLE_H

#include "storage/ob_resource_map.h"

namespace oceanbase
{
namespace storage
{

class ObTabletPointer;
class ObTabletPointerMap;

class ObTabletPointerHandle : public ObResourceHandle<ObTabletPointer>
{
public:
  ObTabletPointerHandle();
  explicit ObTabletPointerHandle(ObTabletPointerMap &map);
  ObTabletPointerHandle(
      ObResourceValueStore<ObTabletPointer> *ptr,
      ObTabletPointerMap *map);
  virtual ~ObTabletPointerHandle();

public:
  virtual void reset() override;
  bool is_valid() const;
  int assign(const ObTabletPointerHandle &other);

  TO_STRING_KV("ptr", ObResourceHandle<ObTabletPointer>::ptr_, KP_(map));
private:
  int set(
      ObResourceValueStore<ObTabletPointer> *ptr,
      ObTabletPointerMap *map);

private:
  ObTabletPointerMap *map_;

  DISALLOW_COPY_AND_ASSIGN(ObTabletPointerHandle);
};

} // end namespace storage
} // end namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_POINTER_HANDLE_H
