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

#include "observer/virtual_table/ob_tenant_show_restore_preview.h"


using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace observer
{

ObTenantShowRestorePreview::ObTenantShowRestorePreview()
  : ObVirtualTableIterator(),
    is_inited_(false),
    idx_(-1),
    total_cnt_(0),
    uri_(),
    restore_scn_(),
    only_contain_backup_set_(false),
    allocator_()
{
}

ObTenantShowRestorePreview::~ObTenantShowRestorePreview()
{
}

void ObTenantShowRestorePreview::reset()
{
  ObVirtualTableIterator::reset();
  uri_.reset();
  idx_ = -1;
  total_cnt_ = 0;
}

int ObTenantShowRestorePreview::init()
{
  int ret = OB_SUCCESS;
  is_inited_ = true;
  idx_ = 0;
  total_cnt_ = 0;
  return ret;
}

int ObTenantShowRestorePreview::parse_restore_scn_from_session_(
    const ObString &backup_passwd, ObIArray<ObString> &tenant_path_array)
{
  UNUSED(backup_passwd);
  UNUSED(tenant_path_array);
  return OB_SUCCESS;
}

int ObTenantShowRestorePreview::inner_get_next_row(common::ObNewRow *&row)
{
  UNUSED(row);
  return OB_ITER_END;
}

int ObTenantShowRestorePreview::inner_get_next_row_()
{
  return OB_ITER_END;
}

int ObTenantShowRestorePreview::get_backup_id_(int64_t &backup_id)
{
  backup_id = -1;
  return OB_SUCCESS;
}

int ObTenantShowRestorePreview::get_backup_type_(BackupType &type)
{
  type = BACKUP_TYPE_MAX;
  return OB_SUCCESS;
}

int ObTenantShowRestorePreview::get_backup_path_(common::ObString &str)
{
  str.reset();
  return OB_SUCCESS;
}

int ObTenantShowRestorePreview::get_backup_desc_(common::ObString &str)
{
  str.reset();
  return OB_SUCCESS;
}


} // end namespace observer
} // end namespace oceanbase
