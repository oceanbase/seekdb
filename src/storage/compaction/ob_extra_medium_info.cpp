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

#include "storage/compaction/ob_extra_medium_info.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace compaction
{
ObExtraMediumInfo::ObExtraMediumInfo()
  : last_compaction_type_(0),
    wait_check_flag_(0),
    last_medium_scn_(0)
{
}

ObExtraMediumInfo::ObExtraMediumInfo(const ObExtraMediumInfo &other)
{
  if (this != &other) {
    last_compaction_type_ = other.last_compaction_type_;
    wait_check_flag_ = other.wait_check_flag_;
    last_medium_scn_ = other.last_medium_scn_;
  }
}

ObExtraMediumInfo &ObExtraMediumInfo::operator=(const ObExtraMediumInfo &other)
{
  if (this != &other) {
    last_compaction_type_ = other.last_compaction_type_;
    wait_check_flag_ = other.wait_check_flag_;
    last_medium_scn_ = other.last_medium_scn_;
  }
  return *this;
}

void ObExtraMediumInfo::reset()
{
  last_compaction_type_ = 0;
  wait_check_flag_ = 0;
  last_medium_scn_ = 0;
}

int ObExtraMediumInfo::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
      last_compaction_type_,
      wait_check_flag_,
      last_medium_scn_);
  return ret;
}

int ObExtraMediumInfo::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
      last_compaction_type_,
      wait_check_flag_,
      last_medium_scn_);
  return ret;
}

int64_t ObExtraMediumInfo::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
      last_compaction_type_,
      wait_check_flag_,
      last_medium_scn_);
  return len;
}
} // namespace compaction
} // namespace oceanbase
