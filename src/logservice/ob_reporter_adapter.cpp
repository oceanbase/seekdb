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

#include "ob_reporter_adapter.h"

namespace oceanbase
{
namespace logservice
{

ObLogReporterAdapter::ObLogReporterAdapter() :
    is_inited_(false),
    rs_reporter_(NULL)
  {}

ObLogReporterAdapter::~ObLogReporterAdapter()
{
  destroy();
}

int ObLogReporterAdapter::init(observer::ObIMetaReport *reporter)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    PALF_LOG(WARN, "ObLogReporterAdapter init twice", K(ret));
  } else if (OB_ISNULL(reporter)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments", K(reporter), K(ret));
  } else {
    rs_reporter_ = reporter;
    is_inited_ = true;
    PALF_LOG(INFO, "ObLogReporterAdapter init success", K(ret), KP(rs_reporter_));
  }
  return ret;
}

void ObLogReporterAdapter::destroy()
{
  is_inited_ = false;
  rs_reporter_ = NULL;
}

int ObLogReporterAdapter::report_replica_info(const int64_t palf_id)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  const share::ObLSID id(palf_id);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "ObLogReporterAdapter is not inited", K(ret));
  } else if (OB_FAIL(rs_reporter_->submit_ls_update_task(tenant_id, id))) {
    PALF_LOG(WARN, "report ls info failed", K(ret), K(tenant_id), K(palf_id));
  } else {
    PALF_LOG(INFO, "submit_ls_update_task success", K(tenant_id), K(palf_id));
    // do nothing.
  }
  return ret;
}

} // end namespace logservice
} // end namespace oceanbase
