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
#ifndef OB_STORAGE_MULTI_DATA_SOURCE_MDS_KEY_SERIALIZE_UTIL_H_
#define OB_STORAGE_MULTI_DATA_SOURCE_MDS_KEY_SERIALIZE_UTIL_H_
#include "/usr/include/stdint.h"
namespace oceanbase
{
namespace storage
{
namespace mds
{

struct ObMdsSerializeUtil final
{
  static int mds_key_serialize(const int64_t key, char *buf,
                               const int64_t buf_len, int64_t &pos);
  static int mds_key_deserialize(const char *buf, const int64_t buf_len, int64_t &pos, int64_t &key);
  template <typename T>
  static int64_t mds_key_get_serialize_size(const T key) { return sizeof(T); }
};

} // namespace mds
} // namespace storage
} // namespace oceanbase

#endif // OB_STORAGE_MULTI_DATA_SOURCE_MDS_KEY_SERIALIZE_UTIL_H_
