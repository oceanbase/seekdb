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

#ifndef OCEANBASE_STORAGE_OB_TX_TABLE_LOCAL_BUFFER
#define OCEANBASE_STORAGE_OB_TX_TABLE_LOCAL_BUFFER

#include "lib/allocator/ob_allocator.h"

namespace oceanbase {
namespace storage {

struct ObTxLocalBuffer
{
  ObTxLocalBuffer() = delete;
  ObTxLocalBuffer(common::ObIAllocator &allocator) : allocator_(allocator) { buf_ = nullptr; buf_len_ = 0; }
  ~ObTxLocalBuffer() { reset(); }
  OB_INLINE char *get_ptr() { return buf_; }
  OB_INLINE int64_t get_length() { return buf_len_; }
  OB_INLINE void reset()
  {
    if (nullptr != buf_) {
      allocator_.free(buf_);
    }
    buf_ = nullptr;
    buf_len_ = 0;
  }
  OB_INLINE int reserve(const int64_t buf_len)
  {
    int ret = OB_SUCCESS;
    if (OB_UNLIKELY(buf_len <= 0)) {
      ret = OB_INVALID_ARGUMENT;
      TRANS_LOG(WARN, "Invalid argument to reserve local buffer", K(buf_len));
    } else if (buf_len > buf_len_) {
      reset();
      if (OB_ISNULL(buf_ = reinterpret_cast<char *>(allocator_.alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        TRANS_LOG(WARN, "Failed to alloc memory", K(ret), K(buf_len));
      } else {
        buf_len_ = buf_len;
      }
    }
    return ret;
  }

  TO_STRING_KV(K_(buf), K_(buf_len));

  common::ObIAllocator &allocator_;
  char *buf_;
  int64_t buf_len_;
};

}
}
#endif // OCEANBASE_STORAGE_OB_TX_TABLE_LOCAL_BUFFER
