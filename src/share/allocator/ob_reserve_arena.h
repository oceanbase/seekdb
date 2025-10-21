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

#ifndef OCEANBASE_COMMON_RESERVE_ARENA_H__
#define OCEANBASE_COMMON_RESERVE_ARENA_H__

#include "lib/allocator/page_arena.h"  // ModuleArena

namespace oceanbase
{
namespace common
{
template<int64_t MAX_RESERVE_SIZE = 0>
class ObReserveArenaAllocator : public ObIAllocator
{
public:
  ObReserveArenaAllocator(const lib::ObMemAttr &attr,
                const int64_t page_size = OB_MALLOC_NORMAL_BLOCK_SIZE)
              : pos_(0),
                allocator_(attr, page_size)
  {
    OB_ASSERT(MAX_RESERVE_SIZE >= 0);
  }
  virtual ~ObReserveArenaAllocator()
  {
    reset();
  }
  virtual void *alloc(const int64_t size, const ObMemAttr &attr) override
  {
    UNUSED(attr);
    return alloc(size);
  }
  virtual void* alloc(const int64_t size) override
  {
    void *p = NULL;
    if (size > MAX_RESERVE_SIZE - 1 - pos_) {
      p = allocator_.alloc(size);
    } else {
      p = reinterpret_cast<void *>(buf_ + pos_);
      pos_ += size;
    }
    return p;
  }
  virtual void* realloc(const void *ptr, const int64_t size, const ObMemAttr &attr) override
  {
    // not supportted
    UNUSED(ptr);
    UNUSED(size);
    UNUSED(attr);
    return nullptr;
  }
  virtual void *realloc(void *ptr, const int64_t oldsz, const int64_t newsz) override
  {
    // not supportted
    UNUSED(ptr);
    UNUSED(oldsz);
    UNUSED(newsz);
    return nullptr;
  }
  virtual void free(void *ptr) override
  {
    UNUSED(ptr);
  }
  virtual int64_t total() const override
  {
    return pos_ + allocator_.total();
  }
  virtual int64_t used() const override
  {
    return pos_ + allocator_.used();
  }
  virtual void reset() override
  {
    if (allocator_.used() > 0) {
      allocator_.reset();
    }
    pos_ = 0;
    buf_[0] = '\0';
  }
  virtual void reuse() override
  {
	  if (allocator_.used() > 0) {
      allocator_.reuse();
    }
    pos_ = 0;
    buf_[0] = '\0';
  }

  virtual void set_attr(const ObMemAttr &attr) { UNUSED(attr); }
private:
  char buf_[MAX_RESERVE_SIZE];
  int64_t pos_;
  common::ObArenaAllocator allocator_;
};
} // namespace common
} // namespace oceanbase
#endif /* OCEANBASE_COMMON_HIGH_PERFORMANC_ARENA_H__ */
