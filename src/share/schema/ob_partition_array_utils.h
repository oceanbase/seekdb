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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_PARTITION_ARRAY_UTILS_
#define OCEANBASE_SHARE_SCHEMA_OB_PARTITION_ARRAY_UTILS_

#include "lib/utility/serialization.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

class ObPartitionArrayUtils
{
public:
  template <class T>
  static int serialize(
      T **partition_array,
      const int64_t partition_num,
      char *buf,
      const int64_t buf_len,
      int64_t &pos)
  {
    int ret = common::OB_SUCCESS;
    if (OB_FAIL(common::serialization::encode_vi64(buf, buf_len, pos, partition_num))) {
    } else if (OB_NOT_NULL(partition_array)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < partition_num; ++i) {
        if (OB_ISNULL(partition_array[i])) {
          ret = OB_ERR_UNEXPECTED;
          SHARE_SCHEMA_LOG(WARN, "partition array element is null", KR(ret));
        } else if (OB_FAIL(partition_array[i]->serialize(buf, buf_len, pos))) {
          SHARE_SCHEMA_LOG(WARN, "failed to serialize partition", KR(ret));
        }
      }
    }
    return ret;
  }

  template <class T>
  static int64_t get_serialize_size(T **partition_array, const int64_t partition_num)
  {
    int64_t len = common::serialization::encoded_length_vi64(partition_num);
    if (OB_NOT_NULL(partition_array)) {
      for (int64_t i = 0; i < partition_num; ++i) {
        if (OB_NOT_NULL(partition_array[i])) {
          len += partition_array[i]->get_serialize_size();
        }
      }
    }
    return len;
  }

  template <class T>
  static int64_t get_convert_size(T **partition_array, const int64_t partition_num)
  {
    int64_t convert_size = 0;
    if (OB_NOT_NULL(partition_array)) {
      for (int64_t i = 0; i < partition_num && OB_NOT_NULL(partition_array[i]); ++i) {
        convert_size += partition_array[i]->get_convert_size();
      }
      convert_size += partition_num * sizeof(T *);
    }
    return convert_size;
  }
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_SCHEMA_OB_PARTITION_ARRAY_UTILS_
