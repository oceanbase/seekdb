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
#include <gtest/gtest.h>
#define private public
#define protected public
#include "share/cache/ob_vtable_event_recycle_buffer.h"

namespace oceanbase {
namespace unittest {

using namespace common;
using namespace std;
using namespace common::cache;

namespace vtable_event_recycle_buffer_test
{

struct Allocator : public common::ObIAllocator
{
  void *alloc(const int64_t size) override { return common::ob_malloc(size, "VtableEvent"); }
  void *alloc(const int64_t size, const lib::ObMemAttr &attr) override
  {
    return common::ob_malloc(size, attr);
  }
  void free(void *ptr) override { common::ob_free(ptr); }
};

struct HashKey
{
  HashKey(const int key = 0) : key_(key) {}
  bool operator<(const HashKey &rhs) const { return key_ < rhs.key_; }
  bool operator==(const HashKey &rhs) const { return key_ == rhs.key_; }
  int64_t hash() const { return key_ % 3; }
  TO_STRING_KV(K_(key));
  int key_;
};

struct Value
{
  Value() : data_(nullptr), len_(0), alloc_(nullptr) {}
  Value(const char value, const int64_t len) : Value()
  {
    data_ = new char[len];
    MEMSET(data_, value, len);
    len_ = len;
  }
  ~Value()
  {
    if (OB_NOT_NULL(alloc_)) {
      alloc_->free(data_);
    } else {
      delete[] data_;
    }
  }
  int assign(common::ObIAllocator &alloc, const Value &rhs)
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

} // namespace vtable_event_recycle_buffer_test

class TestObVtableEventRecycleBuffer: public ::testing::Test
{
public:
  TestObVtableEventRecycleBuffer() {}
  virtual ~TestObVtableEventRecycleBuffer() {}
  virtual void SetUp() {
  }
  virtual void TearDown() {
  }
public:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestObVtableEventRecycleBuffer);
};

TEST_F(TestObVtableEventRecycleBuffer, basic_test) {
  using namespace vtable_event_recycle_buffer_test;
  Allocator allocator;
  ObVtableEventRecycleBuffer<HashKey, Value> vtable_event_buffer;
  const int64_t buffer_size = 1024;
  auto value1 = Value('1', 1);
  auto value2 = Value('2', 1);
  auto for_each_dummy_op = [](const Value &) { return OB_SUCCESS; };
  ASSERT_EQ(
      OB_SUCCESS,
      vtable_event_buffer.init(
          "TEST", allocator, 2, buffer_size, 100));
  ASSERT_EQ(OB_SUCCESS, vtable_event_buffer.append({1}, value1));
  ASSERT_NE(
      HashKey(1).hash(),
      vtable_event_buffer.buffer_bkt_[1].cache_.hash_bkt_.re_hash_idx_({1}));
  ASSERT_EQ(OB_SUCCESS, vtable_event_buffer.for_each({1}, for_each_dummy_op));
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, vtable_event_buffer.for_each({0}, for_each_dummy_op));
  ASSERT_EQ(OB_SUCCESS, vtable_event_buffer.append({0}, value2));
  ASSERT_EQ(OB_SUCCESS, vtable_event_buffer.buffer_bkt_[1].cache_.for_each({1}, for_each_dummy_op));
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, vtable_event_buffer.buffer_bkt_[1].cache_.for_each({0}, for_each_dummy_op));
  ASSERT_EQ(OB_ENTRY_NOT_EXIST, vtable_event_buffer.buffer_bkt_[0].cache_.for_each({1}, for_each_dummy_op));
  ASSERT_EQ(OB_SUCCESS, vtable_event_buffer.buffer_bkt_[0].cache_.for_each({0}, for_each_dummy_op));
}

}
}
