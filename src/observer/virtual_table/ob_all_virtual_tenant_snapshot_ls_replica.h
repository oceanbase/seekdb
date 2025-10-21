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

#ifndef OB_OBSERVER_OB_ALL_VIRTUAL_TENANT_SNAPSHOT_LS_REPLICA_H
#define OB_OBSERVER_OB_ALL_VIRTUAL_TENANT_SNAPSHOT_LS_REPLICA_H

#include "observer/virtual_table/ob_iterate_private_virtual_table.h"
#include "storage/ls/ob_ls_meta_package.h"

namespace oceanbase {
namespace observer {

class ObAllVirtualTenantSnapshotLSReplica : public ObIteratePrivateVirtualTable
{
public:
  ObAllVirtualTenantSnapshotLSReplica() {}
  virtual ~ObAllVirtualTenantSnapshotLSReplica() {}
  virtual int try_convert_row(const ObNewRow *input_row, ObNewRow *&row) override;

private:
  enum COLUMN_ID_LIST
  {
    TENANT_ID = common::OB_APP_MIN_COLUMN_ID,
    SNAPSHOT_ID,
    LS_ID,
    SVR_IP,
    SVR_PORT,
    GMT_CREATE,
    GMT_MODIFIED,
    STATUS,
    ZONE,
    UNIT_ID,
    BEGIN_INTERVAL_SCN,
    END_INTERVAL_SCN,
    LS_META_PACKAGE
  };
  int decode_hex_string_to_package_(const ObString& hex_str,
                                    ObLSMetaPackage& ls_meta_package);
private:
  static const int64_t LS_META_BUFFER_SIZE = 16 * 1024;

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllVirtualTenantSnapshotLSReplica);
};

}  // namespace observer
}  // namespace oceanbase
#endif  // OB_OBSERVER_OB_ALL_VIRTUAL_TENANT_SNAPSHOT_LS_REPLICA_H
