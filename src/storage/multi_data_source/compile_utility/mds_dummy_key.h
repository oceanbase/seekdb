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
 
#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_COMPILE_UTILITY_MDS_DUMMYKEY_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_COMPILE_UTILITY_MDS_DUMMYKEY_H
#include "lib/list/ob_dlist.h"
#include "lib/ob_errno.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

struct DummyKey
{
  DummyKey() = default;
  int mds_serialize(char *, const int64_t, int64_t &) const { return  OB_SUCCESS; }
  int mds_deserialize(const char *, const int64_t, int64_t &) { return  OB_SUCCESS; }
  int64_t mds_get_serialize_size() const { return 0; }
  int reset() { return OB_SUCCESS; }
  int is_valid() { return OB_SUCCESS; }
  int64_t to_string(char *buf, const int64_t buf_len) const {
    int64_t pos = 0;
    databuff_printf(buf, buf_len, pos, "Dummy");
    return pos;
  }
};

}
}
}

#endif
