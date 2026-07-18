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

#ifndef OCEANBASE_IO_SCHEDULE_OB_IO_SCHEDULE_V2_H_
#define OCEANBASE_IO_SCHEDULE_OB_IO_SCHEDULE_V2_H_
#include "share/io/ob_io_define.h"

namespace oceanbase
{
namespace common
{

class ObTenantIOSchedulerV2
{
public:
  ObTenantIOSchedulerV2() {}
  ~ObTenantIOSchedulerV2() {}
  int init(const ObTenantIOConfig &io_config) {
    UNUSED(io_config);
    return OB_SUCCESS;
  }
  void destroy() {}
  int update_config(const ObTenantIOConfig &io_config) {
    UNUSED(io_config);
    return OB_SUCCESS;
  }
  int schedule_request(ObIORequest &req);
  static int64_t get_qindex(ObIORequest& req);
private:
};

}; // end namespace common
}; // end namespace oceanbase
#endif /* OCEANBASE_IO_SCHEDULE_OB_IO_SCHEDULE_V2_H_ */
