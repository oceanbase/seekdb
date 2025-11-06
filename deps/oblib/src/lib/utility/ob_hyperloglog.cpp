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

#define USING_LOG_PREFIX LIB

#include "lib/utility/ob_hyperloglog.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{
OB_DEF_SERIALIZE(ObHyperLogLogCalculator)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, n_bit_, n_count_);
  for (int64_t i = 0; i < n_bucket_ && OB_SUCC(ret); ++i) {
    LST_DO_CODE(OB_UNIS_ENCODE, buckets_[i]);
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObHyperLogLogCalculator)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, n_bit_, n_count_);
  if (n_bit_ != 0) {
    if (OB_ISNULL(alloc_)) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "fail to get deserialize allocator of hyperloglog", K(ret));
    } else if (OB_FAIL(init(alloc_, n_bit_))) {
      COMMON_LOG(WARN, "fail to init hyperloglog", K(ret));
    } else {
      for (int64_t i = 0; i < n_bucket_ && OB_SUCC(ret); ++i) {
        LST_DO_CODE(OB_UNIS_DECODE, buckets_[i]);
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObHyperLogLogCalculator)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, n_bit_, n_count_);
  len += n_bucket_;
  return len;
}

} // end namespace common
} // end namespace oceanbase
