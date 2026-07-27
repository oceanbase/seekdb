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
  : format_version_(MEDIUM_LIST_FORMAT_VERSION),
    last_compaction_type_(0),
    wait_check_flag_(0),
    reserved_(0),
    last_medium_scn_(0)
{
}

ObExtraMediumInfo::ObExtraMediumInfo(const ObExtraMediumInfo &other)
  : info_(other.info_),
    last_medium_scn_(other.last_medium_scn_)
{
}

ObExtraMediumInfo &ObExtraMediumInfo::operator=(const ObExtraMediumInfo &other)
{
  if (this != &other) {
    info_ = other.info_;
    last_medium_scn_ = other.last_medium_scn_;
  }
  return *this;
}

void ObExtraMediumInfo::reset()
{
  info_ = 0;
  format_version_ = MEDIUM_LIST_FORMAT_VERSION;
  last_medium_scn_ = 0;
}

int ObExtraMediumInfo::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0 || pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), KP(buf), K(buf_len), K(pos));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "extra medium info is invalid", K(ret), KPC(this));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,
        info_,
        last_medium_scn_);
  }
  return ret;
}

int ObExtraMediumInfo::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || data_len <= 0 || pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), KP(buf), K(data_len), K(pos));
  } else if (OB_FAIL(serialization::decode(buf, data_len, pos, info_))) {
    STORAGE_LOG(WARN, "failed to decode medium list format", K(ret), K(data_len), K(pos));
  } else if (OB_UNLIKELY(MEDIUM_LIST_FORMAT_VERSION != format_version_ || 0 != reserved_)) {
    ret = OB_NOT_SUPPORTED;
    STORAGE_LOG(WARN, "medium list format mismatch", K(ret), K_(format_version), K_(reserved));
  } else if (OB_FAIL(serialization::decode(buf, data_len, pos, last_medium_scn_))) {
    STORAGE_LOG(WARN, "failed to decode last medium scn", K(ret), K(data_len), K(pos));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "extra medium info is invalid", K(ret), KPC(this));
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int64_t ObExtraMediumInfo::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
      info_,
      last_medium_scn_);
  return len;
}
} // namespace compaction
} // namespace oceanbase
