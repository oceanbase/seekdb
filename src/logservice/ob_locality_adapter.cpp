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

#include "ob_locality_adapter.h"
#include "storage/ob_locality_manager.h"

namespace oceanbase
{
namespace logservice
{

ObLocalityAdapter::ObLocalityAdapter() :
    is_inited_(false),
    locality_manager_(NULL)
  {}

ObLocalityAdapter::~ObLocalityAdapter()
{
  destroy();
}

int ObLocalityAdapter::init(storage::ObLocalityManager *locality_manager)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "ObLocalityAdapter init twice");
  } else if (OB_ISNULL(locality_manager)) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid arguments", KP(locality_manager));
  } else {
    locality_manager_ = locality_manager;
    is_inited_ = true;
    PALF_LOG(INFO, "ObLocalityAdapter init success", K(locality_manager_));
  }
  return ret;
}

void ObLocalityAdapter::destroy()
{
  is_inited_ = false;
  locality_manager_ = NULL;
}

int ObLocalityAdapter::get_server_region(const common::ObAddr &server,
                                         common::ObRegion &region) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(locality_manager_->get_server_region(server, region))) {
    CLOG_LOG(WARN, "get_server_region failed", K(server));
  }
  return ret;
}

} // end namespace logservice
} // end namespace oceanbase
