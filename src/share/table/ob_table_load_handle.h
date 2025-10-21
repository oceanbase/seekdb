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

#ifndef OB_TABLE_LOAD_HANDLE_H_
#define OB_TABLE_LOAD_HANDLE_H_

#include "lib/allocator/ob_malloc.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace table
{

template<class T>
class ObTableLoadHandle
{
  class Object
  {
  public:
    template<class... Args>
    Object(Args... args) : ref_count_(0), object_(args...) {}
  public:
    int64_t ref_count_;
    T object_;
  };

public:
  ObTableLoadHandle() : ptr_(nullptr) {}
  virtual ~ObTableLoadHandle() {
    reset();
  }

  template<class... Args >
  static int make_handle(ObTableLoadHandle &handle, Args... args)
  {
    int ret = OB_SUCCESS;
    ObMemAttr attr(MTL_ID(), "TLD_Handle");
    handle.reset();
    if (OB_ISNULL(handle.ptr_ = OB_NEW(Object, attr, args...))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "fail to new object", KR(ret));
    } else {
      handle.ptr_->ref_count_ = 1;
    }
    return ret;
  }

  ObTableLoadHandle(const ObTableLoadHandle &other) : ptr_(nullptr) {
    *this = other;
  }

  ObTableLoadHandle(ObTableLoadHandle &&other) : ptr_(nullptr) {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      other.ptr_ = nullptr;
    }
  }

  void operator = (const ObTableLoadHandle &other) {
    if (this != &other) {
      reset();
      ptr_ = other.ptr_;
      if (ptr_ != nullptr) {
        ATOMIC_AAF(&(ptr_->ref_count_), 1);
      }
    }
  }

  operator bool() const {
    return ptr_ != nullptr;
  }

  T *operator->() const {
    return &(ptr_->object_);
  }

  T &operator*() const {
    return ptr_->object_;
  }

  void reset() {
    if (ptr_ != nullptr) {
      int64_t ref_count = ATOMIC_AAF(&(ptr_->ref_count_), -1);
      if (ref_count == 0) {
        ptr_->~Object();
        ob_free(ptr_);
      }
      ptr_ = nullptr;
    }
  }

private:
  // data members
  Object *ptr_;
};

}
}

#endif /* OB_TABLE_LOAD_HANDLE_H_ */


