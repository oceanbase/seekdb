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

#ifndef OCEANBASE_UNITTEST_SHARE_CACHE_RECYCLE_BUFFER_TEST_TYPES_H_
#define OCEANBASE_UNITTEST_SHARE_CACHE_RECYCLE_BUFFER_TEST_TYPES_H_

#include "share/cache/ob_recycle_multi_kvcache.h"
#include "share/cache/ob_vtable_event_recycle_buffer.h"

namespace oceanbase
{
namespace unittest
{

struct DefaultAllocator : public common::ObIAllocator
{
  void *alloc(const int64_t size) override { return common::ob_malloc(size, "MDS"); }
  void *alloc(const int64_t size, const lib::ObMemAttr &attr) override
  {
    return common::ob_malloc(size, attr);
  }
  void free(void *ptr) override { common::ob_free(ptr); }
  static DefaultAllocator &get_instance()
  {
    static DefaultAllocator allocator;
    return allocator;
  }
};

struct TestEvent
{
  TestEvent() : buffer_(nullptr), len_(0), alloc_(nullptr) {}
  TestEvent &operator=(const TestEvent &rhs) = delete;
  ~TestEvent()
  {
    if (OB_NOT_NULL(alloc_)) {
      alloc_->free(buffer_);
      alloc_ = nullptr;
      buffer_ = nullptr;
      len_ = 0;
    }
  }
  int init(common::ObIAllocator &alloc, int64_t size)
  {
    int ret = OB_SUCCESS;
    if (nullptr == (buffer_ = static_cast<char *>(alloc.alloc(size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OCCAM_LOG(DEBUG, "fail to alloc", K(*this));
    } else {
      alloc_ = &alloc;
      len_ = size;
    }
    return ret;
  }
  int assign(common::ObIAllocator &alloc, const TestEvent &rhs)
  {
    return init(alloc, rhs.len_);
  }
  TO_STRING_KV(KP(this), KP_(buffer), KP_(alloc), K_(len));
  char *buffer_;
  int64_t len_;
  common::ObIAllocator *alloc_;
};

struct HashKey
{
  HashKey() : key_(0) {}
  HashKey(int key) : key_(key) {}
  bool operator<(const HashKey &rhs) { return key_ < rhs.key_; }
  bool operator==(const HashKey &rhs) { return key_ == rhs.key_; }
  int64_t hash() const { return key_ % 3; }
  TO_STRING_KV(K_(key));
  int key_;
};

struct Simple
{
  Simple() : val_(0) {}
  Simple(int val) : val_(val) {}
  bool operator==(const Simple &rhs) const { return val_ == rhs.val_; }
  TO_STRING_KV(K_(val));
  int val_;
};

struct Complicated
{
  Complicated() : data_(nullptr), len_(0), alloc_(nullptr) {}
  Complicated(char value, int len) : Complicated()
  {
    data_ = new char[len];
    for (int i = 0; i < len; ++i) {
      data_[i] = value;
    }
    len_ = len;
  }
  ~Complicated()
  {
    if (OB_NOT_NULL(alloc_)) {
      alloc_->free(data_);
      data_ = nullptr;
      len_ = 0;
      alloc_ = nullptr;
    }
  }
  bool operator==(const Complicated &rhs) const
  {
    bool equal = len_ == rhs.len_;
    for (int i = 0; equal && i < len_; ++i) {
      equal = data_[i] == rhs.data_[i];
    }
    return equal;
  }
  bool operator!=(const Complicated &rhs) const { return !(*this == rhs); }
  int assign(common::ObIAllocator &alloc, const Complicated &rhs)
  {
    int ret = OB_SUCCESS;
    if (nullptr == (data_ = static_cast<char *>(alloc.alloc(rhs.len_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(data_, rhs.data_, rhs.len_);
      len_ = rhs.len_;
      alloc_ = &alloc;
    }
    return ret;
  }
  TO_STRING_KV(KP_(data), K_(len), KP_(alloc));
  char *data_;
  int64_t len_;
  common::ObIAllocator *alloc_;
};

} // namespace unittest
} // namespace oceanbase

#endif // OCEANBASE_UNITTEST_SHARE_CACHE_RECYCLE_BUFFER_TEST_TYPES_H_
