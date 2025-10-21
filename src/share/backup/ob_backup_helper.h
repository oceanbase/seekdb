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

#ifndef OCEANBASE_SHARE_OB_BACKUP_HELPER_H_
#define OCEANBASE_SHARE_OB_BACKUP_HELPER_H_

#include "share/ob_inner_kv_table_operator.h"
#include "share/backup/ob_backup_struct.h"

namespace oceanbase
{
namespace share
{

class ObBackupHelper final : public ObIExecTenantIdProvider
{
public:
  ObBackupHelper();
  virtual ~ObBackupHelper() {}

  // Return tenant id to execute sql.
  uint64_t get_exec_tenant_id() const override;

  int init(const uint64_t tenant_id, common::ObISQLClient &sql_proxy);
  int get_backup_dest(share::ObBackupPathString &backup_dest) const;
  
  int set_backup_dest(const share::ObBackupPathString &backup_dest) const;

  TO_STRING_KV(K_(is_inited), K_(tenant_id));

private:
  DISALLOW_COPY_AND_ASSIGN(ObBackupHelper);

  int init_backup_parameter_table_operator_(ObInnerKVTableOperator &kv_table_operator) const;

private:
  bool is_inited_;
  uint64_t tenant_id_; // user tenant id
  common::ObISQLClient *sql_proxy_;
};

}
}

#endif
