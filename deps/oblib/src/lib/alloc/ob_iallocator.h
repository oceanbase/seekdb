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
#ifndef OCEANBASE_COMMON_IALLOCATOR_BASE_H_
#define OCEANBASE_COMMON_IALLOCATOR_BASE_H_

#include "lib/ob_define.h"
#include "lib/alloc/alloc_struct.h"

namespace oceanbase
{
namespace common
{
using lib::ObMemAttr;
class ObAllocAlign
{
public:
  struct Header
  {
    static const uint32_t MAGIC_CODE = 0XAA22CCE1;
    bool check_magic_code() const { return MAGIC_CODE == magic_code_; }
    void mark_unused() { magic_code_ &= ~0x1; }
    uint32_t magic_code_;
    uint32_t offset_;
  };
  template<typename alloc_func, typename... Args>
  static void *alloc_align(const int64_t size, const int64_t align, alloc_func &&alloc, const Args&... args)
  {
    void *ptr = NULL;
    int64_t real_align = lib::align_up2(align, 16);
    int64_t real_size = real_align + size + sizeof(Header);
    char *tmp_ptr = (char*)alloc(real_size, args...);
    if (NULL != tmp_ptr) {
      ptr = (void*)lib::align_up2((int64_t)tmp_ptr + sizeof(Header), real_align);
      Header *header = (Header*)ptr - 1;
      header->magic_code_ = Header::MAGIC_CODE;
      header->offset_ = (char*)header - tmp_ptr;
    }
    return ptr;
  }

  template<typename free_func>
  static void free_align(void *ptr, free_func &&free)
  {
    if (NULL != ptr) {
      Header *header = (Header*)ptr - 1;
      abort_unless(header->check_magic_code());
      header->mark_unused();
      char *orig_ptr = (char*)header - header->offset_;
      free(orig_ptr);
    }
  }
};
class ObIAllocator
{
public:
  /************************************************************************/
  /*                     New Interface (Under construction)               */
  /************************************************************************/
  // Use attr passed in by set_attr().
  virtual ~ObIAllocator() {};
  virtual void *alloc(const int64_t size) = 0;
  virtual void* alloc(const int64_t size, const ObMemAttr &attr) = 0;
  virtual void* realloc(const void *ptr, const int64_t size, const ObMemAttr &attr)
  {
    UNUSED(ptr);
    UNUSED(size);
    UNUSED(attr);
    return nullptr;
  }
  virtual void *realloc(void *ptr, const int64_t oldsz, const int64_t newsz)
  {
    UNUSED(ptr);
    UNUSED(oldsz);
    UNUSED(newsz);
    return nullptr;
  }
  virtual void free(void *ptr) = 0;
  virtual void *alloc_align(const int64_t size, const int64_t align) final
  {
    return ObAllocAlign::alloc_align(size, align,
        [this](const int64_t size) { return this->alloc(size); });
  }
  virtual void *alloc_align(const int64_t size, const int64_t align, const ObMemAttr &attr) final
  {
    return ObAllocAlign::alloc_align(size, align,
        [this](const int64_t size, const ObMemAttr &attr) { return this->alloc(size, attr); }, attr);
  }
  virtual void free_align(void *ptr) final
  {
    ObAllocAlign::free_align(ptr, [this](void *ptr){ this->free(ptr); });
  }
  virtual int64_t total() const
  {
    return 0;
  }
  virtual int64_t used() const
  {
    return 0;
  }
  virtual void reset() {}
  virtual void reuse() {}

  virtual void set_attr(const ObMemAttr &attr) { UNUSED(attr); }

  virtual ObIAllocator &operator=(const ObIAllocator &that) {
    UNUSED(that);
    return *this;
  }
};

extern ObIAllocator *global_default_allocator;
}
}
#endif // OCEANBASE_COMMON_IALLOCATOR_BASE_H_
