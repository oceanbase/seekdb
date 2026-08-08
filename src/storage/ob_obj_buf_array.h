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

#ifndef OCEANBASE_STORAGE_OB_OBJ_BUF_ARRAY_H_
#define OCEANBASE_STORAGE_OB_OBJ_BUF_ARRAY_H_

#include "common/object/ob_object.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace storage
{

// Small-object buffer shared by Storage rowkey/value adapters.  Keeping this
// type independent makes rowkey headers self-contained without importing the
// broad storage utility surface.
class ObObjBufArray final
{
public:
  ObObjBufArray()
      : capacity_(0), is_inited_(false), data_(NULL), allocator_(NULL)
  {}

  ~ObObjBufArray() { reset(); }

  int init(common::ObIAllocator *allocator)
  {
    int ret = common::OB_SUCCESS;
    if (IS_INIT) {
      ret = common::OB_INIT_TWICE;
      STORAGE_LOG(WARN, "init twice", K(ret), K(is_inited_));
    } else if (OB_ISNULL(allocator)) {
      ret = common::OB_INVALID_ARGUMENT;
      STORAGE_LOG(WARN, "invalid arguments", K(ret), KP(allocator));
    } else {
      allocator_ = allocator;
      data_ = reinterpret_cast<common::ObObj *>(local_data_buf_);
      capacity_ = LOCAL_ARRAY_SIZE;
      is_inited_ = true;
    }
    return ret;
  }

  inline bool is_inited() const { return is_inited_; }

  inline int reserve(int64_t count)
  {
    int ret = common::OB_SUCCESS;
    if (IS_NOT_INIT) {
      ret = common::OB_NOT_INIT;
      STORAGE_LOG(WARN, "ObObjBufArray not inited", K(ret), K(is_inited_));
    } else if (count > capacity_) {
      const int64_t new_size = count * sizeof(common::ObObj);
      common::ObObj *new_data =
          reinterpret_cast<common::ObObj *>(allocator_->alloc(new_size));
      if (OB_NOT_NULL(new_data)) {
        if (reinterpret_cast<char *>(data_) != local_data_buf_) {
          allocator_->free(data_);
        }
        MEMSET(new_data, 0, new_size);
        data_ = new_data;
        capacity_ = count;
      } else {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(ERROR, "no memory", K(ret), K(new_size), K(capacity_));
      }
    }
    return ret;
  }

  inline int64_t get_count() const { return capacity_; }
  inline common::ObObj *get_data() { return data_; }

  void reset()
  {
    if (NULL != allocator_ && reinterpret_cast<char *>(data_) != local_data_buf_) {
      allocator_->free(data_);
    }
    allocator_ = NULL;
    data_ = NULL;
    capacity_ = 0;
    is_inited_ = false;
  }

  inline common::ObObj &at(int64_t idx) const
  {
    OB_ASSERT(idx >= 0 && idx < capacity_);
    return data_[idx];
  }

private:
  static const int64_t LOCAL_ARRAY_SIZE = 64;
  int64_t capacity_;
  bool is_inited_;
  common::ObObj *data_;
  char local_data_buf_[LOCAL_ARRAY_SIZE * sizeof(common::ObObj)];
  common::ObIAllocator *allocator_;
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_OBJ_BUF_ARRAY_H_
