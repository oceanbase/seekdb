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

#define USING_LOG_PREFIX STORAGE
#include "ob_ls_restore_args.h"

using namespace oceanbase;
using namespace storage;

ObTenantRestoreCtx::ObTenantRestoreCtx()
  : job_id_(0),
    restore_type_(),
    restore_scn_(),
    consistent_scn_(),
    tenant_id_(0),
    backup_cluster_version_(0),
    backup_data_version_(0),
    progress_display_mode_(share::ObRestoreProgressDisplayMode::TABLET_CNT)
{
}

ObTenantRestoreCtx::~ObTenantRestoreCtx()
{
}

bool ObTenantRestoreCtx::is_valid() const
{
  return job_id_ > 0 && restore_type_.is_valid();
}

int ObTenantRestoreCtx::assign(const ObTenantRestoreCtx &args)
{
  int ret = OB_SUCCESS;
  if (!args.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(args));
  } else {
    job_id_ = args.get_job_id();
    restore_type_ = args.get_restore_type();
    restore_scn_ = args.get_restore_scn();
    consistent_scn_ = args.get_consistent_scn();
    tenant_id_ = args.get_tenant_id();
    backup_cluster_version_ = args.get_backup_cluster_version();
    backup_data_version_ = args.get_backup_data_version();
    progress_display_mode_ = args.progress_display_mode_;
  }
  return ret;
}
