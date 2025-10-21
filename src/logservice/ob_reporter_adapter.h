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

#ifndef OCEANBASE_LOGSERVICE_OB_REPORTER_ADAPTER_H_
#define OCEANBASE_LOGSERVICE_OB_REPORTER_ADAPTER_H_

#include <stdint.h>
#include "observer/ob_service.h"
#include "logservice/palf/palf_callback.h"

namespace oceanbase
{
namespace logservice
{
class ObLogReporterAdapter
{
public:
  ObLogReporterAdapter();
  virtual ~ObLogReporterAdapter();
  int init(observer::ObIMetaReport *reporter);
  void destroy();
public:
  // report the replica info to RS.
  int report_replica_info(const int64_t palf_id);
private:
  bool is_inited_;
  observer::ObIMetaReport *rs_reporter_;
};

} // logservice
} // oceanbase

#endif
