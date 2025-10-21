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

#ifndef OCEANBASE_LIB_HASH_FUNC_OB_HASH_FUNC_
#define OCEANBASE_LIB_HASH_FUNC_OB_HASH_FUNC_
#include "lib/hash/xxhash.h"
#include "lib/hash_func/wyhash.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/utility/ob_template_utils.h"

namespace oceanbase
{
namespace common
{
template <typename K>
inline uint64_t dispatch_hash(const K &key, FalseType)
{
  // default hash function
  return murmurhash(&key, sizeof(key), 0);
}
template <typename K>
inline uint64_t dispatch_hash(const K &key, TrueType)
{
  return key.hash();
}
template <typename K>
inline uint64_t do_hash(const K &key)
{
  return dispatch_hash(key, BoolType<HAS_HASH(K)>());
}

template <typename K>
inline uint64_t dispatch_hash(const K &key, uint64_t seed, FalseType)
{
  // default hash function
  return murmurhash(&key, sizeof(key), seed);
}

template <typename K>
inline uint64_t dispatch_hash(const K &key, uint64_t seed, TrueType)
{
  uint64_t hash_val;
  key.hash(hash_val, seed);
  return hash_val;
}

template <typename K>
inline uint64_t do_hash(const K &key, uint64_t seed)
{
  return dispatch_hash(key, seed, BoolType<HAS_HASH(K)>());
}

template <typename K>
inline bool do_equal(const K &key1, const K &key2)
{
  return key1 == key2;
}
} // end namespace common
} // end namespace oceanbase
#endif //OCEANBASE_LIB_HASH_FUNC_OB_HASH_FUNC_
