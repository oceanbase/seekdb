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

#ifndef OB_FTS_POSITION_LIST_STORE_H_
#define OB_FTS_POSITION_LIST_STORE_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace storage
{

static const uint16_t POS_LIST_MAGIC = 0xFACE;
static const uint16_t POS_LIST_VERSION = 1;

class ObFTSPositionListStore
{
public:
  ObFTSPositionListStore()
    : is_inited_(false), total_positions_(0), encoded_buf_(nullptr), encoded_len_(0)
  {
  }

  ~ObFTSPositionListStore()
  {
    destroy();
  }

  int init(ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    is_inited_ = true;
    return ret;
  }

  void destroy()
  {
    if (encoded_buf_ != nullptr) {
      ob_free(encoded_buf_);
      encoded_buf_ = nullptr;
    }
    positions_.reset();
    total_positions_ = 0;
    encoded_len_ = 0;
    is_inited_ = false;
  }

  int add_position(int64_t position)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(positions_.push_back(position))) {
    } else {
      total_positions_++;
    }
    return ret;
  }

  int encode_variable_int64(ObIAllocator &allocator)
  {
    int ret = OB_SUCCESS;
    int64_t required = header_size() + positions_.count() * sizeof(int64_t);
    char *buf = static_cast<char *>(allocator.alloc(required));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      int64_t offset = 0;
      MEMCPY(buf + offset, &POS_LIST_MAGIC, sizeof(POS_LIST_MAGIC));
      offset += sizeof(POS_LIST_MAGIC);
      MEMCPY(buf + offset, &POS_LIST_VERSION, sizeof(POS_LIST_VERSION));
      offset += sizeof(POS_LIST_VERSION);
      int32_t pos_count = static_cast<int32_t>(positions_.count());
      MEMCPY(buf + offset, &pos_count, sizeof(pos_count));
      offset += sizeof(pos_count);
      for (int64_t i = 0; i < positions_.count(); ++i) {
        int64_t pos_val = positions_.at(i);
        MEMCPY(buf + offset, &pos_val, sizeof(pos_val));
        offset += sizeof(pos_val);
      }
      uint32_t checksum = calc_checksum(reinterpret_cast<const uint8_t *>(buf), offset);
      MEMCPY(buf + offset, &checksum, sizeof(checksum));
      offset += sizeof(checksum);
      encoded_buf_ = buf;
      encoded_len_ = offset;
    }
    return ret;
  }

  int decode(const common::ObString &data, int64_t &cursor)
  {
    int ret = OB_SUCCESS;
    const char *ptr = data.ptr();
    int64_t len = data.length();
    if (len < static_cast<int64_t>(header_size())) {
      ret = OB_INVALID_DATA;
    } else {
      uint16_t magic = 0;
      MEMCPY(&magic, ptr, sizeof(magic));
      if (magic != POS_LIST_MAGIC) {
        ret = OB_INVALID_DATA;
      }
    }
    return ret;
  }

  OB_INLINE int64_t get_total_positions() const { return total_positions_; }
  OB_INLINE const char *get_encoded_data() const { return encoded_buf_; }
  OB_INLINE int64_t get_encoded_len() const { return encoded_len_; }

private:
  static int64_t header_size()
  {
    return sizeof(POS_LIST_MAGIC) + sizeof(POS_LIST_VERSION) + sizeof(int32_t);
  }

  static uint32_t calc_checksum(const uint8_t *data, int64_t len)
  {
    uint32_t cs = 0;
    for (int64_t i = 0; i < len; ++i) {
      cs = ((cs << 7) | (cs >> 25)) ^ data[i];
    }
    return cs;
  }

  bool is_inited_;
  int64_t total_positions_;
  ObIAllocator *allocator_;
  common::ObSEArray<int64_t, 32> positions_;
  char *encoded_buf_;
  int64_t encoded_len_;
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_FTS_POSITION_LIST_STORE_H_ */
