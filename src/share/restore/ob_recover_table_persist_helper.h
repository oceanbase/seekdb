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

#ifndef OCEANBASE_SHARE_RECOVER_TABLE_PERSIST_HELPER_H
#define OCEANBASE_SHARE_RECOVER_TABLE_PERSIST_HELPER_H

#include "lib/ob_define.h"
#include "share/ob_inner_table_operator.h"
#include "share/restore/ob_import_table_struct.h"

namespace oceanbase
{
namespace share
{
class ObRecoverTablePersistHelper final : public ObIExecTenantIdProvider
{
public:
  ObRecoverTablePersistHelper();
  virtual ~ObRecoverTablePersistHelper() {}
  int init(const uint64_t tenant_id);
  void reset() { is_inited_ = false; }
  uint64_t get_exec_tenant_id() const override { return gen_meta_tenant_id(tenant_id_); }
  int insert_recover_table_job(common::ObISQLClient &proxy, const ObRecoverTableJob &job) const;

  int get_all_recover_table_job(common::ObISQLClient &proxy, common::ObIArray<ObRecoverTableJob> &jobs) const;

  int get_recover_table_job(common::ObISQLClient &proxy, const uint64_t tenant_id, const int64_t job_id,
      ObRecoverTableJob &job) const;

  int is_recover_table_job_exist(common::ObISQLClient &proxy, const uint64_t target_tenant_id, bool &is_exist) const;
  
  int advance_status(common::ObISQLClient &proxy, 
      const ObRecoverTableJob &job, const ObRecoverTableStatus &next_status) const;
  int force_cancel_recover_job(common::ObISQLClient &proxy) const;

  int get_recover_table_job_by_initiator(common::ObISQLClient &proxy, 
      const ObRecoverTableJob &initiator_job, ObRecoverTableJob &target_job) const;

  int delete_recover_table_job(common::ObISQLClient &proxy, const ObRecoverTableJob &job) const;
  int insert_recover_table_job_history(common::ObISQLClient &proxy, const ObRecoverTableJob &job) const;

  int get_recover_table_job_history_by_initiator(common::ObISQLClient &proxy, 
      const ObRecoverTableJob &initiator_job, ObRecoverTableJob &target_job) const;
  TO_STRING_KV(K_(is_inited), K_(tenant_id));
private:
  DISALLOW_COPY_AND_ASSIGN(ObRecoverTablePersistHelper);
  bool is_inited_;
  uint64_t tenant_id_; // sys or user tenant id
  ObInnerTableOperator table_op_;
};

}
}

#endif
