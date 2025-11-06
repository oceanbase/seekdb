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

#ifndef OCEANBASE_MVCC_OB_ROW_DATA_
#define OCEANBASE_MVCC_OB_ROW_DATA_
#include "share/ob_define.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace memtable
{
struct ObRowData
{
  ObRowData(): data_(NULL), size_(0) {}
  ~ObRowData() {}
  void reset()
  {
    data_ = NULL;
    size_ = 0;
  }
  void set(const char *data, const int32_t size)
  {
    data_ = data;
    size_ = size;
  }
  bool operator==(const ObRowData &that) const
  {
    return this->size_ == that.size_
           && (size_ <= 0
               || (NULL != this->data_ && NULL != that.data_ && 0 == MEMCMP(this->data_, that.data_, size_)));
  }
  int serialize(char *buf, const int64_t buf_len, int64_t &pos);
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  TO_STRING_KV(KP_(data), K_(size));
  const char *data_;
  int32_t size_;
};

}; // end namespace mvcc
}; // end namespace oceanbase

#endif /* OCEANBASE_MVCC_OB_ROW_DATA_ */
