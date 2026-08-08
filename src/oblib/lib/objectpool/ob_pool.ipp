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

#include "lib/atomic/ob_atomic.h"

namespace oceanbase
{
namespace common
{

template <typename BlockAllocatorT, typename LockT>
ObPool<BlockAllocatorT, LockT>::ObPool(int64_t obj_size, int64_t block_size,
                                       const BlockAllocatorT &alloc)
    : obj_size_(obj_size),
      block_size_(block_size),
      in_use_count_(0),
      free_count_(0),
      total_count_(0),
      freelist_(NULL),
      blocklist_(NULL),
      block_allocator_(alloc),
      lock_()
{
  if (OB_UNLIKELY(obj_size_ < static_cast<int64_t>(sizeof(FreeNode)))) {
    LIB_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "obj_size_ < size of FreeNode");
  } else {}
  if (block_size_ < (obj_size_ + static_cast<int64_t>(sizeof(BlockHeader)))) {
    LIB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obj size larger than block size", K(obj_size_), K(block_size_));
    block_size_ = obj_size_ + sizeof(BlockHeader);
  } else {}
}

template <typename BlockAllocatorT, typename LockT>
ObPool<BlockAllocatorT, LockT>::~ObPool()
{
  reset();
}

template <typename BlockAllocatorT, typename LockT>
void ObPool<BlockAllocatorT, LockT>::reset()
{
  BlockHeader *curr = blocklist_;
  BlockHeader *next = NULL;
  //if (in_use_count_ != 0) {
  //  LIB_LOG(ERROR, "there was memory leak", K(in_use_count_), K(free_count_), K(total_count_));
  //}
  while (NULL != curr) {
    next = curr->next_;
    block_allocator_.free(curr);
    curr = next;
  }
  blocklist_ = NULL;
  freelist_ = NULL;
  in_use_count_ = 0;
  free_count_ = 0;
  total_count_ = 0;
  // In some places, it is used after resetting, and block_size_ and obj_size_ cannot be reset here.
  //block_size_ = 0;
  //obj_size_ = 0;
}

template <typename BlockAllocatorT, typename LockT>
int ObPool<BlockAllocatorT, LockT>::mprotect_mem_pool(int prot)
{
  int ret = OB_SUCCESS;
  BlockHeader *curr = blocklist_;
  BlockHeader *next = NULL;
  while (OB_SUCC(ret) && NULL != curr) {
    next = curr->next_;
    if (OB_FAIL(mprotect_page(curr, block_size_, prot, "mem_pool"))) {
      LIB_LOG(WARN, "mprotect page failed", K(ret));
    }
    curr = next;
  }
  return ret;
}

template <typename BlockAllocatorT, typename LockT>
void ObPool<BlockAllocatorT, LockT>::alloc_new_block()
{
  int ret = OB_SUCCESS;
  BlockHeader *new_block = static_cast<BlockHeader *>(block_allocator_.alloc(block_size_));
  if (OB_ISNULL(new_block)) {
    LIB_LOG(ERROR, "no memory");
  } else {
    new_block->next_ = blocklist_;
    blocklist_ = new_block;

    const int64_t obj_count = (block_size_ - sizeof(BlockHeader)) / obj_size_;
    if (OB_UNLIKELY(0 >= obj_count)) {
      LIB_LOG(ERROR, "invalid block size", K(block_size_));
    } else {
      for (int i = 0; OB_SUCC(ret) && i < obj_count; ++i) {
        ATOMIC_INC(&total_count_);
        freelist_push(reinterpret_cast<char *>(new_block) + sizeof(BlockHeader) + obj_size_ * i);
      }
    }
  }
}

template <typename BlockAllocatorT, typename LockT>
void *ObPool<BlockAllocatorT, LockT>::alloc()
{
  void *ptr_ret = NULL;
  ObLockGuard<LockT> guard(lock_);
  if (NULL == (ptr_ret = freelist_pop())) {
    alloc_new_block();
    ptr_ret = freelist_pop();
  }
  return ptr_ret;
}

template <typename BlockAllocatorT, typename LockT>
void ObPool<BlockAllocatorT, LockT>::free(void *obj)
{
  ObLockGuard<LockT> guard(lock_);
  if (NULL != obj) {
    ATOMIC_DEC(&in_use_count_);
  }
  freelist_push(obj);
}

template <typename BlockAllocatorT, typename LockT>
void *ObPool<BlockAllocatorT, LockT>::freelist_pop()
{
  void *ptr_ret = NULL;
  if (NULL != freelist_) {
    ptr_ret = freelist_;
    freelist_ = freelist_->next_;
    ATOMIC_DEC(&free_count_);
    ATOMIC_INC(&in_use_count_);
  }
  return ptr_ret;
}

template <typename BlockAllocatorT, typename LockT>
void ObPool<BlockAllocatorT, LockT>::freelist_push(void *obj)
{
  if (NULL != obj) {
    FreeNode *node = static_cast<FreeNode *>(obj);
    if (OB_ISNULL(node)) {
      LIB_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "node is NULL");
    } else {
      node->next_ = freelist_;
      freelist_ = node;
      ATOMIC_INC(&free_count_);
    }
  }
}

}
}
