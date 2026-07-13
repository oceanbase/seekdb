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

#include "storage/tx/ob_tx_print_time_guard.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace transaction
{

int64_t ObTxPrintTimeGuard::to_string(char *buf, const int64_t buf_len) const
{
  int ret = common::OB_SUCCESS;
  int64_t pos = 0;
  const double total_diff = end_ts_ - start_ts_;
  if (total_diff > 0) {
    common::databuff_printf(buf, buf_len, pos, " [Total : %f ms] ", total_diff / 1000);
  }

  for (int64_t i = 0; i < MAX_CLICK_COUNT; i++) {
    const double tmp_diff = click_end_ts_[i] - click_start_ts_[i];
    if (tmp_diff > 0) {
      common::databuff_printf(buf, buf_len, pos, " [%s : %f ms] ", click_str_[i], tmp_diff / 1000);
    }
  }

  if (0 == pos) {
    ret = common::databuff_printf(buf, buf_len, pos, "invalid TxPrintTimeGuard");
  }
  return common::OB_SUCCESS == ret ? pos : 0;
}

} // namespace transaction
} // namespace oceanbase
