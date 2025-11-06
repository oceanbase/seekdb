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

#ifndef OCEANBASE_SHARE_OB_COMMON_BATCH_H_
#define OCEANBASE_SHARE_OB_COMMON_BATCH_H_
#include "common/ob_range.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace common
{
typedef ObNewRange SQLScanRange;
typedef ObSEArray<ObNewRange, 10, ModulePageAllocator, true> SQLScanRangeArray;

struct ObSimpleBatch
{
  //TODO shengle is extracted together with ObBatchType in ObBatch, and I will not change it this time.
  //To adjust the storage layer code, change it separately later
  enum ObBatchType
  {
    T_NONE,
    T_GET,
    T_MULTI_GET,
    T_SCAN,
    T_MULTI_SCAN,
  };
  ObBatchType type_;
  union
  {
    const SQLScanRange *range_;
    const SQLScanRangeArray *ranges_;
  };

  ObSimpleBatch() : type_(T_NONE), range_(NULL) { }
  virtual ~ObSimpleBatch() {};

  void destroy();

  bool is_valid() const
  {
    return (T_NONE != type_
            && T_GET != type_
            && T_MULTI_GET != type_
            && (NULL != range_ || NULL != ranges_));
  }
  int64_t to_string(char *buffer, const int64_t length) const
  {
    int64_t pos = 0;
    if (T_NONE == type_) {
      common::databuff_printf(buffer, length, pos, "NONE:");
    } else if (T_SCAN == type_) {
      common::databuff_printf(buffer, length, pos, "SCAN:");
      pos += range_->to_string(buffer + pos, length - pos);
    } else if (T_MULTI_SCAN == type_) {
      common::databuff_printf(buffer, length, pos, "MULTI SCAN:");
      pos += ranges_->to_string(buffer + pos, length - pos);
    } else {
      common::databuff_printf(buffer, length, pos, "invalid type:%d", type_);
    }
    return pos;
  }

  int deserialize(common::ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size(void) const;
};

} // end of namespace common
} // end of namespace oceanbase

#endif /*OCEANBASE_SHARE_OB_COMMON_BATCH_H_*/
