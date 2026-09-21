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

#ifndef OCEANBASE_TRANSACTION_OB_MEMTABLE_CTX_OBJ_POOL_
#define OCEANBASE_TRANSACTION_OB_MEMTABLE_CTX_OBJ_POOL_

#include <stdint.h>
#include "ob_arena_object_pool.h"
#include "storage/memtable/mvcc/ob_mvcc_trans_ctx.h"

namespace oceanbase
{
namespace transaction 
{


class ObMemtableCtxObjPool
{
public:

  ObMemtableCtxObjPool(common::ObIAllocator &allocator)
      : mvcc_callback_pool_(allocator) {}

  ObMemtableCtxObjPool() = delete;

  template <typename T>
  void *alloc();

  template <>
  void *alloc<memtable::ObMvccRowCallback>()
  {
    return mvcc_callback_pool_.alloc();
  }

  template <typename T>
  void free(void *);

  template <>
  void free<memtable::ObMvccRowCallback>(void *obj)
  {
    mvcc_callback_pool_.free(obj);
  }

  void reset()
  {
    mvcc_callback_pool_.reset();
  }

private:
  static constexpr int64_t OBJ_NUM = 1;
  ObArenaObjPool<memtable::ObMvccRowCallback, OBJ_NUM> mvcc_callback_pool_;

};


} // transaction
} // oceanbase

#endif
