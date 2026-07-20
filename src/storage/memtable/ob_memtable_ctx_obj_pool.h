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

#ifndef OCEANBASE_STORAGE_MEMTABLE_OB_MEMTABLE_CTX_OBJ_POOL_
#define OCEANBASE_STORAGE_MEMTABLE_OB_MEMTABLE_CTX_OBJ_POOL_

#include <stdint.h>
#include "storage/tablelock/ob_mem_ctx_table_lock.h"

namespace oceanbase
{
namespace memtable 
{

class ObMemtableCtxObjPool
{
public:

  ObMemtableCtxObjPool(common::ObIAllocator &allocator)
      : lock_op_node_pool_(allocator), lock_callback_pool_(allocator), mvcc_callback_pool_(allocator) {}

  ObMemtableCtxObjPool() = delete;

  template <typename T>
  void *alloc();

  template <>
  void *alloc<transaction::tablelock::ObMemCtxLockOpLinkNode>()
  {
    return lock_op_node_pool_.alloc();
  }

  template <>
  void *alloc<transaction::tablelock::ObOBJLockCallback>()
  {
    return lock_callback_pool_.alloc();
  }

  template <>
  void *alloc<memtable::ObMvccRowCallback>()
  {
    return mvcc_callback_pool_.alloc();
  }

  template <typename T>
  void free(void *);

  template <>
  void free<transaction::tablelock::ObMemCtxLockOpLinkNode>(void *obj)
  {
    lock_op_node_pool_.free(obj);
  }

  template <>
  void free<transaction::tablelock::ObOBJLockCallback>(void *obj)
  {
    lock_callback_pool_.free(obj);
  }

  template <>
  void free<memtable::ObMvccRowCallback>(void *obj)
  {
    mvcc_callback_pool_.free(obj);
  }

  void reset()
  {
    lock_op_node_pool_.reset();
    lock_callback_pool_.reset();
    mvcc_callback_pool_.reset();
  }

private:
  static constexpr int64_t OBJ_NUM = 2;
  ObArenaObjPool<transaction::tablelock::ObMemCtxLockOpLinkNode, OBJ_NUM> lock_op_node_pool_;
  ObArenaObjPool<transaction::tablelock::ObOBJLockCallback, OBJ_NUM> lock_callback_pool_;
  ObArenaObjPool<memtable::ObMvccRowCallback, OBJ_NUM> mvcc_callback_pool_;

};

}  // namespace memtable
}  // namespace oceanbase

#endif
