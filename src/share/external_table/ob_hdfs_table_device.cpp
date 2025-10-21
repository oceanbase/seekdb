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

#include "ob_hdfs_table_device.h"

namespace oceanbase
{
namespace share
{
ObHDFSTableDevice::ObHDFSTableDevice()
 :external_storage_info_()
{
}

ObHDFSTableDevice::~ObHDFSTableDevice()
{
  destroy();
}

int ObHDFSTableDevice::setup_storage_info(const ObIODOpts &opts)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(external_storage_info_.set(device_type_, opts.opts_[0].value_.value_str))) {
    OB_LOG(WARN, "failed to build external storage info", K(ret));
  }
  return ret;
}

void ObHDFSTableDevice::destroy()
{
  ObObjectDevice::destroy();
}

} // namespace share
} // namespace oceanbase
