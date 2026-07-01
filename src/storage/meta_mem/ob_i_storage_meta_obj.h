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

#ifndef OB_STORAGE_META_MEM_OB_I_STORAGE_META_OBJ_H_
#define OB_STORAGE_META_MEM_OB_I_STORAGE_META_OBJ_H_

#include <stdint.h>

namespace oceanbase
{
namespace storage
{

class ObIStorageMetaObj
{
public:
  ObIStorageMetaObj() = default;
  virtual ~ObIStorageMetaObj() = default;
  virtual int deep_copy(char *buf, const int64_t buf_len, ObIStorageMetaObj *&value) const = 0;
  virtual int64_t get_deep_copy_size() const = 0;
};

}  // namespace storage
}  // namespace oceanbase

#endif
