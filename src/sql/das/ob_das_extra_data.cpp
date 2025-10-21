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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_extra_data.h"
namespace oceanbase
{
namespace sql
{
ObDASExtraData::ObDASExtraData()
  : output_exprs_(nullptr),
    eval_ctx_(nullptr),
    task_id_(0),
    timeout_ts_(0),
    result_addr_(),
    result_(),
    result_iter_(),
    has_more_(false),
    need_check_output_datum_(false),
    enable_rich_format_(false),
    tsc_monitor_info_(nullptr)
{
}

int ObDASExtraData::init(const int64_t task_id,
                         const int64_t timeout_ts,
                         const common::ObAddr &result_addr,
                         rpc::frame::ObReqTransport *transport,
                         const bool enable_rich_format)
{
  int ret = OB_SUCCESS;
  task_id_ = task_id;
  timeout_ts_ = timeout_ts;
  result_addr_ = result_addr;
  has_more_ = false;
  need_check_output_datum_ = false;
  enable_rich_format_ = enable_rich_format;
  return ret;
}

int ObDASExtraData::fetch_result()
{
  return OB_UNIMPLEMENTED_FEATURE;
}

int ObDASExtraData::get_next_row()
{
  int ret = OB_SUCCESS;
  bool got_row = false;
  if (!result_iter_.is_valid()) {
    // hasn't fetched any data yet
    if (OB_FAIL(fetch_result())) {
      LOG_WARN("fetch result failed", KR(ret));
    }
  }
  while (!got_row && OB_SUCC(ret)) {
    if (OB_FAIL(result_iter_.get_next_row<false>(*eval_ctx_, *output_exprs_))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next row from result iter failed", KR(ret));
      } else if (has_more_) {
        ret = OB_SUCCESS;
        if (OB_FAIL(fetch_result())) {
          LOG_WARN("fetch result failed", KR(ret));
        }
      }
    } else {
      got_row = true;
      LOG_DEBUG("get next row from result iter", KR(ret),
                "output", ROWEXPR2STR(*eval_ctx_, *output_exprs_));
    }
  }
  return ret;
}

int ObDASExtraData::get_next_rows(int64_t &count, int64_t capacity)
{
  int ret = OB_SUCCESS;
  bool got_row = false;
  if ((enable_rich_format_ && !vec_result_iter_.is_valid())
      || (!enable_rich_format_ && !result_iter_.is_valid())) {
    // hasn't fetched any data yet
    if (OB_FAIL(fetch_result())) {
      LOG_WARN("fetch result failed", KR(ret));
    }
  }
  while (!got_row && OB_SUCC(ret)) {
    if (enable_rich_format_) {
      ret = vec_result_iter_.get_next_batch(*output_exprs_, *eval_ctx_, capacity, count);
    } else if (OB_UNLIKELY(need_check_output_datum_)) {
      ret = result_iter_.get_next_batch<true>(*output_exprs_, *eval_ctx_,
                                                         capacity, count);
    } else {
      ret = result_iter_.get_next_batch<false>(*output_exprs_, *eval_ctx_,
                                                          capacity, count);
    }
    if (OB_FAIL(ret)) {
      if (OB_ITER_END != ret) {
        LOG_WARN("get next batch from result iter failed", KR(ret));
      } else if (has_more_) {
        ret = OB_SUCCESS;
        if (OB_FAIL(fetch_result())) {
          LOG_WARN("fetch result failed", KR(ret));
        }
      }
    } else {
      got_row = true;
      const ObBitVector *skip = NULL;
      PRINT_VECTORIZED_ROWS(SQL, DEBUG, *eval_ctx_, *output_exprs_, count, skip, KR(ret));
    }
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
