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

#ifndef OCEANBASE_SHARE_OB_TENANT_MODULE_INIT_CTX_H_
#define OCEANBASE_SHARE_OB_TENANT_MODULE_INIT_CTX_H_

#include "lib/ob_define.h"
#include "logservice/palf/palf_options.h"
namespace oceanbase
{

namespace share
{
class DagSchedulerConfig;
// Tenant module initialization parameters
class ObTenantModuleInitCtx
{
public:
  ObTenantModuleInitCtx() : palf_options_()
  {}

  // for logservice
  palf::PalfOptions palf_options_;
  char tenant_clog_dir_[common::MAX_PATH_SIZE] = {'\0'};

#ifdef OB_BUILD_SHARED_STORAGE
  // for SS mode ObTenantDiskSpaceManager
  int64_t init_data_disk_size_ = 0;
#endif

  // TODO init DagSchedulerConfig, which will be used to config the params in ObTenantDagScheduler
  // share::DagSchedulerConfig *scheduler_config_;
};


}//end namespace share
}//end namespace oceanbase

#endif //OCEANBASE_SHARE_OB_TENANT_MODULE_INIT_CTX_H_
