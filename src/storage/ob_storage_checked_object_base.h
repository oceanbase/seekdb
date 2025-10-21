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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_CHECKED_OBJECT_BASE_H_
#define OCEANBASE_STORAGE_OB_STORAGE_CHECKED_OBJECT_BASE_H_

#include "share/cache/ob_kvcache_struct.h"

namespace oceanbase
{
namespace storage
{

enum class ObStorageCheckID
{
  INVALID_ID,
  ALL_CACHE = MAX_CACHE_NUM,
  IO_HANDLE,
  STORAGE_ITER
};

class ObStorageCheckedObjectBase {
public:
  bool is_traced() const { return is_traced_; }
private:
  bool is_traced_{false};
  friend class ObStorageLeakChecker;
};

};
};

#endif
