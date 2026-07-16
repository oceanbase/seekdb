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

#ifndef OB_DDL_ENCODE_SORTKEY_UTILS_H_
#define OB_DDL_ENCODE_SORTKEY_UTILS_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace storage
{

static const int64_t ENCODED_SORTKEY_MIN_LEN = 2;
static const int64_t ENCODED_SORTKEY_MAX_LEN = 18;

class ObDDLEncodeSortkeyUtils
{
public:
  static bool is_fixed_length_encode(int64_t key_len)
  {
    return key_len >= ENCODED_SORTKEY_MIN_LEN && key_len <= ENCODED_SORTKEY_MAX_LEN;
  }

  static int encode_sortkey(ObIAllocator &allocator,
                            const common::ObIArray<common::ObString> &keys,
                            common::ObString &encoded_key)
  {
    int ret = OB_SUCCESS;
    int64_t total_len = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < keys.count(); ++i) {
      total_len += keys.at(i).length() + sizeof(int16_t);
    }
    char *buf = static_cast<char *>(allocator.alloc(total_len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      int64_t offset = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < keys.count(); ++i) {
        const common::ObString &key = keys.at(i);
        int16_t key_len = static_cast<int16_t>(key.length());
        MEMCPY(buf + offset, &key_len, sizeof(int16_t));
        offset += sizeof(int16_t);
        if (key_len > 0) {
          MEMCPY(buf + offset, key.ptr(), key_len);
          offset += key_len;
        }
      }
      encoded_key.assign_ptr(buf, static_cast<int32_t>(total_len));
    }
    return ret;
  }

  static int compare_encoded(const common::ObString &a, const common::ObString &b, int &cmp_ret)
  {
    int ret = OB_SUCCESS;
    int64_t min_len = MIN(a.length(), b.length());
    if (min_len == 0) {
      cmp_ret = (a.length() == b.length()) ? 0 : (a.length() < b.length() ? -1 : 1);
    } else {
      cmp_ret = MEMCMP(a.ptr(), b.ptr(), min_len);
      if (cmp_ret == 0 && a.length() != b.length()) {
        cmp_ret = (a.length() < b.length()) ? -1 : 1;
      }
    }
    return ret;
  }
};

} // end namespace storage
} // end namespace oceanbase

#endif /* OB_DDL_ENCODE_SORTKEY_UTILS_H_ */
