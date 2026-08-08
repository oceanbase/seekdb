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
#ifndef OCEANBASE_COMMON_IALLOCATOR_H_
#define OCEANBASE_COMMON_IALLOCATOR_H_

#include "lib/alloc/ob_iallocator.h"  // ObIAllocator/ObAllocAlign interface has been moved down to alloc layer (breaks the alloc->allocator cycle)

namespace oceanbase
{
namespace common
{

class ObWrapperAllocator: public ObIAllocator
{
public:
  explicit ObWrapperAllocator(ObIAllocator *alloc): alloc_(alloc) {};
  explicit ObWrapperAllocator(const lib::ObLabel &label): alloc_(NULL) {UNUSED(label);};
  explicit ObWrapperAllocator(ObIAllocator &alloc): alloc_(&alloc) { } // for ObArray::ObArray()
  ObWrapperAllocator(): alloc_(NULL) {};
  virtual ~ObWrapperAllocator() {};
  virtual void *alloc(int64_t sz, const ObMemAttr &attr)
  {
    return NULL == alloc_ ? NULL : alloc_->alloc(sz, attr);
  }
  virtual void *alloc(const int64_t sz)
  { return NULL == alloc_ ? NULL : alloc_->alloc(sz); }
  virtual void* realloc(const void *ptr, const int64_t size, const ObMemAttr &attr)
  { return NULL == alloc_ ? NULL : alloc_->realloc(ptr, size, attr); }

  virtual void *realloc(void *ptr, const int64_t oldsz, const int64_t newsz)
  { return NULL == alloc_ ? NULL : alloc_->realloc(ptr, oldsz, newsz); }

  void free(void *ptr)
  {
    if (NULL != alloc_) {
      alloc_->free(ptr); ptr = NULL;
    }
  }
  virtual int64_t total() const { return alloc_ != nullptr ? alloc_->total() : 0; }
  virtual int64_t used() const { return alloc_ != nullptr ? alloc_->used() : 0; }
  void set_alloc(ObIAllocator *alloc) { alloc_ = alloc; }
  ObWrapperAllocator &operator=(const ObWrapperAllocator &that)
  {
    if (this != &that) {
      alloc_ = that.alloc_;
    }
    return *this;
  }
  const ObIAllocator *get_alloc() const { return alloc_;}
  ObIAllocator *get_alloc() { return alloc_;}
  static uint32_t alloc_offset_bits()
  {
DISABLE_WARNING_GCC_PUSH
DISABLE_WARNING_GCC("-Winvalid-offsetof")
    return offsetof(ObWrapperAllocator, alloc_) * 8;
DISABLE_WARNING_GCC_POP
  }
private:
  // data members
  ObIAllocator *alloc_;
};

class ObWrapperAllocatorWithAttr: public ObWrapperAllocator
{
public:
  explicit ObWrapperAllocatorWithAttr(ObIAllocator *alloc, ObMemAttr attr = ObMemAttr())
    : ObWrapperAllocator(alloc), mem_attr_(attr) {};
  explicit ObWrapperAllocatorWithAttr(const lib::ObLabel &label)
    : ObWrapperAllocator(NULL), mem_attr_() { mem_attr_.label_ = label; };
  explicit ObWrapperAllocatorWithAttr(ObIAllocator &alloc, ObMemAttr attr = ObMemAttr())
    : ObWrapperAllocator(&alloc), mem_attr_(attr) {} // for ObArray::ObArray()
  ObWrapperAllocatorWithAttr(): ObWrapperAllocator(), mem_attr_() {};
  virtual ~ObWrapperAllocatorWithAttr() {};
  virtual void *alloc(const int64_t sz) { return ObWrapperAllocator::alloc(sz, mem_attr_); };
  const ObMemAttr &get_attr() const { return mem_attr_; }
  void set_attr(const ObMemAttr &attr) { mem_attr_ = attr; }
private:
  ObMemAttr mem_attr_;
};
}
}

#endif //OCEANBASE_COMMON_IALLOCATOR_H_
