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

#ifndef OCEANBASE_STORAGE_MEMTABLE_MVCC_OB_BTREE_ITER_CACHE_H_
#define OCEANBASE_STORAGE_MEMTABLE_MVCC_OB_BTREE_ITER_CACHE_H_

#include <stdint.h>
#include "share/ob_define.h"

namespace oceanbase
{
namespace memtable
{

class ObBtreeIterCache
{
public:
  static constexpr int64_t MAX_FREE_COUNT = 8;

  ObBtreeIterCache() : freelist_(nullptr), free_count_(0) {}
  ~ObBtreeIterCache() { destroy(); }

  void destroy();
  void *alloc(int64_t size);
  void free(void *ptr);

private:
  struct FreeNode { FreeNode *next_; };
  FreeNode *freelist_;
  int64_t free_count_;

  DISALLOW_COPY_AND_ASSIGN(ObBtreeIterCache);
};

void *btree_iter_alloc(int64_t size);
void btree_iter_free(void *ptr);

} // namespace memtable
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_MEMTABLE_MVCC_OB_BTREE_ITER_CACHE_H_
