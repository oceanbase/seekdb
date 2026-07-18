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

#define USING_LOG_PREFIX SHARE

#include "ob_gais_msg.h"

namespace oceanbase
{
using namespace oceanbase::common;
using namespace oceanbase::obcall;
using namespace oceanbase::share;

namespace obcall
{

int ObGAISNextValResult::init(const uint64_t start_inclusive,
                              const uint64_t end_inclusive,
                              const uint64_t sync_value)
{
  int ret = OB_SUCCESS;
  if (start_inclusive <= 0 || end_inclusive <= 0 || start_inclusive > end_inclusive
      || sync_value > end_inclusive) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    start_inclusive_ = start_inclusive;
    end_inclusive_ = end_inclusive;
    sync_value_ = sync_value;
  }
  return ret;
}

int ObGAISCurrValResult::init(const uint64_t sequence_value, const uint64_t sync_value)
{
  int ret = OB_SUCCESS;
  if (sequence_value < sync_value) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    sequence_value_ = sequence_value;
    sync_value_ = sync_value;
  }
  return ret;
}

} // namespace obcall

namespace share
{
int ObGAISNextAutoIncValReq::init(const AutoincKey &autoinc_key,
                                  const uint64_t offset,
                                  const uint64_t increment,
                                  const uint64_t base_value,
                                  const uint64_t max_value,
                                  const uint64_t desired_cnt,
                                  const uint64_t cache_size,
                                  const int64_t &autoinc_version)
{
  int ret = OB_SUCCESS;
  if (max_value <= 0 ||
        cache_size <= 0 || offset < 1 || increment < 1 || base_value > max_value ||
        desired_cnt <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(autoinc_key), K(offset), K(increment), K(base_value),
                                 K(max_value), K(desired_cnt), K(cache_size));
  } else {
    autoinc_key_ = autoinc_key;
    offset_ = offset;
    increment_ = increment;
    base_value_ = base_value;
    max_value_ = max_value;
    desired_cnt_ = desired_cnt;
    cache_size_ = cache_size;
    autoinc_version_ = autoinc_version;
  }
  return ret;
}

int ObGAISAutoIncKeyArg::init(const AutoincKey &autoinc_key, const int64_t autoinc_version)
{
  autoinc_key_ = autoinc_key;
  autoinc_version_ = autoinc_version;
  return OB_SUCCESS;
}

int ObGAISPushAutoIncValReq::init(const AutoincKey &autoinc_key,
                                  const uint64_t base_value,
                                  const uint64_t max_value,
                                  const int64_t &autoinc_version,
                                  const int64_t cache_size)
{
  int ret = OB_SUCCESS;
  if (max_value <= 0 || base_value > max_value) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(autoinc_key), K(base_value), K(max_value));
  } else {
    autoinc_key_ = autoinc_key;
    base_value_ = base_value;
    max_value_ = max_value;
    autoinc_version_ = autoinc_version;
    cache_size_ = cache_size;
  }
  return ret;
}

int ObGAISNextSequenceValReq::init(const schema::ObSequenceSchema &schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(schema_.assign(schema))){
    LOG_WARN("fail to init schemar_", K(ret));
  }
  return ret;
}


} // share
} // oceanbase
