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

#ifndef OCEANBASE_SHARE_OB_I_MAX_ID_CACHE_H_
#define OCEANBASE_SHARE_OB_I_MAX_ID_CACHE_H_

#include <stdint.h>

namespace oceanbase
{
namespace share
{

enum ObMaxIdType : int;

// Demand-owned port used by Share's ID allocator. Implementations may apply
// upper-layer service-readiness policy before delegating to a cache.
class ObIMaxIdCache
{
public:
  virtual ~ObIMaxIdCache() = default;
  virtual int fetch_max_id(ObMaxIdType id_type, uint64_t &min_id,
                           uint64_t size) = 0;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_I_MAX_ID_CACHE_H_
