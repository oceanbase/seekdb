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
#include "share/ob_id_generator.h"

namespace oceanbase
{
namespace share
{

void ObIDGenerator::reset()
{
  inited_ = false;
  step_ = 0;
  start_id_ = common::OB_INVALID_ID;
  end_id_ = common::OB_INVALID_ID;
  current_id_ = common::OB_INVALID_ID;
}

int ObIDGenerator::init(
    const uint64_t step,
    const uint64_t start_id,
    const uint64_t end_id)
{
  int ret = OB_SUCCESS;
  reset();
  if (OB_UNLIKELY(start_id > end_id || 0 == step)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_id/end_id", KR(ret), K(start_id), K(end_id), K(step));
  } else {
    step_ = step;
    start_id_ = start_id;
    end_id_ = end_id;
    current_id_ = start_id - step_;
    inited_ = true;
  }
  return ret;
}

int ObIDGenerator::next(uint64_t &current_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else if (current_id_ >= end_id_) {
    ret = OB_ITER_END;
  } else {
    current_id_ += step_;
    current_id = current_id_;
  }
  return ret;
}

int ObIDGenerator::get_start_id(uint64_t &start_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    start_id = start_id_;
  }
  return ret;
}

int ObIDGenerator::get_current_id(uint64_t &current_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    current_id = current_id_;
  }
  return ret;
}

int ObIDGenerator::get_end_id(uint64_t &end_id) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else {
    end_id = end_id_;
  }
  return ret;
}

int ObIDGenerator::get_id_cnt(uint64_t &cnt) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("generator is not inited", KR(ret), KPC(this));
  } else if (OB_UNLIKELY(end_id_ < start_id_ || step_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid start_id/end_id/step", KR(ret), KPC(this));
  } else {
    cnt = (end_id_ - start_id_) / step_ + 1;
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
