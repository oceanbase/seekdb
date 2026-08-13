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

#ifndef OCEANBASE_DATA_PLANE_API_OB_I_VECTOR_MEMORY_H_
#define OCEANBASE_DATA_PLANE_API_OB_I_VECTOR_MEMORY_H_

#include <cstddef>
#include <cstdint>

namespace oceanbase
{
namespace data_plane
{

class ObIVectorMemory
{
public:
  virtual ~ObIVectorMemory() = default;
  virtual void *allocate(size_t size) = 0;
  virtual void deallocate(void *ptr) = 0;
  virtual uint64_t allocated_bytes() const = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_OB_I_VECTOR_MEMORY_H_
