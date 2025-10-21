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

#include "lib/hash_func/murmur_hash.h"
#include "parse_node_hash.h"

// this is a C wrapper to call murmurhash in C++ definition
uint64_t murmurhash(const void *data, int32_t len, uint64_t hash)
{
  return oceanbase::common::murmurhash(data, len, hash);
}
