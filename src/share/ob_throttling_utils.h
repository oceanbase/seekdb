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

#ifndef OCEANBASE_SHARE_OB_WRITE_THROTTLING_UTILS_H_
#define OCEANBASE_SHARE_OB_WRITE_THROTTLING_UTILS_H_

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{
class ObThrottlingUtils
{
public:
  ObThrottlingUtils(){};
  ~ObThrottlingUtils(){};
public:
  static int calc_decay_factor(const int64_t available_size,
                               const int64_t duration_us,
                               const int64_t chunk_size,
                               double &decay_fatctor);
  static int get_throttling_interval(const int64_t chunk_size,
                                     const int64_t request_size,
                                     const int64_t trigger_limit,
                                     const int64_t cur_hold,
                                     const double decay_factor,
                                     int64_t &interval_us);

};

}//end of namespace share
}//end of namespace oceanbase
#endif
