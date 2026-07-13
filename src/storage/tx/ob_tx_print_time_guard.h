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

#ifndef OCEANBASE_TRANSACTION_OB_TX_PRINT_TIME_GUARD_H_
#define OCEANBASE_TRANSACTION_OB_TX_PRINT_TIME_GUARD_H_

#include <cstring>
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace transaction
{

class ObTxPrintTimeGuard
{
public:
  ObTxPrintTimeGuard()
      : start_ts_(common::ObTimeUtility::fast_current_time()), end_ts_(0)
  {
    memset(click_str_, 0, sizeof(click_str_));
    memset(click_start_ts_, 0, sizeof(click_start_ts_));
    memset(click_end_ts_, 0, sizeof(click_end_ts_));
  }

  void click_start(const char *str, const int64_t click_index)
  {
    click_str_[click_index] = str;
    click_start_ts_[click_index] = common::ObTimeUtility::fast_current_time();
  }

  void click_end(const int64_t click_index)
  {
    click_end_ts_[click_index] = common::ObTimeUtility::fast_current_time();
  }

  int64_t get_diff()
  {
    end_ts_ = common::ObTimeUtility::fast_current_time();
    return end_ts_ - start_ts_;
  }

  ~ObTxPrintTimeGuard()
  {
    end_ts_ = common::ObTimeUtility::fast_current_time();
  }

  int64_t to_string(char *buf, const int64_t buf_len) const;

private:
  static const int64_t MAX_CLICK_COUNT = 16;
  const char *click_str_[MAX_CLICK_COUNT];
  int64_t start_ts_;
  int64_t end_ts_;
  int64_t click_start_ts_[MAX_CLICK_COUNT];
  int64_t click_end_ts_[MAX_CLICK_COUNT];
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TX_PRINT_TIME_GUARD_H_
