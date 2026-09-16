/**
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

#ifndef OCEANBASE_STORAGE_OB_ITER_CACHE_H_
#define OCEANBASE_STORAGE_OB_ITER_CACHE_H_

#include <stdint.h>
#include "share/ob_define.h"

namespace oceanbase
{
namespace storage
{

enum class ObIterCacheType : int64_t
{
  BTREE_ITER = 0,
  TABLE_SCAN_ITER,
  MAX_TYPE
};

class ObIterCache
{
public:
  static constexpr int64_t BTREE_ITER_MAX_FREE_COUNT = 8;
  static constexpr int64_t TABLE_SCAN_ITER_MAX_FREE_COUNT = 2;

  ObIterCache() : freelists_{}, free_counts_{} {}
  ~ObIterCache() { destroy(); }

  void destroy();
  void *alloc(ObIterCacheType type, int64_t size);
  void free(ObIterCacheType type, void *ptr);

private:
  struct FreeNode { FreeNode *next_; };
  static constexpr int64_t ITER_TYPE_COUNT =
      static_cast<int64_t>(ObIterCacheType::MAX_TYPE);

  static bool is_valid_type(ObIterCacheType type);
  static int64_t get_max_free_count(ObIterCacheType type);

  FreeNode *freelists_[ITER_TYPE_COUNT];
  int64_t free_counts_[ITER_TYPE_COUNT];

  DISALLOW_COPY_AND_ASSIGN(ObIterCache);
};

void *iter_alloc(ObIterCacheType type, int64_t size);
void iter_free(ObIterCacheType type, void *ptr);

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_ITER_CACHE_H_
