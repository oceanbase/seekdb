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

#ifndef OCEANBASE_LOGSERVICE_OB_CDC_SERVICE_UTIL_H_
#define OCEANBASE_LOGSERVICE_OB_CDC_SERVICE_UTIL_H_

#include <stdint.h>
#include "ob_cdc_define.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace cdc
{
class ObExtRpcQit
{
public:
  ObExtRpcQit() : deadline_(common::OB_INVALID_TIMESTAMP) {}
  int init(const int64_t deadline);
  // check should_hurry_quit when start to perform a time-consuming operation
  bool should_hurry_quit() const;

  TO_STRING_KV(K(deadline_));
private:
  static const int64_t RESERVED_INTERVAL = 1 * 1000 * 1000; // 1 second
  int64_t deadline_;
};

} // namespace cd
} // namespace oceanbase

#endif
