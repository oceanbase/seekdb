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

#ifndef OCEANBASE_UPGRADE_EXECUTOR_H_
#define OCEANBASE_UPGRADE_EXECUTOR_H_

#include "lib/thread/ob_async_task_queue.h"
#include "share/ob_upgrade_utils.h"
#include "share/ob_check_stop_provider.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/ob_rpc_struct.h"
#include "rootserver/ob_rs_job_table_operator.h"
#include "rootserver/ob_root_inspection.h"
#include "share/ob_global_stat_proxy.h"

namespace oceanbase
{
namespace rootserver
{

class ObUpgradeProcessorExecutor {
public:
  int init(const uint64_t &tenant_id, common::ObMySQLProxy *sql_proxy);
  virtual int get_data_version(uint64_t &data_version) = 0;
  virtual int update_data_version(const uint64_t &data_version) = 0;
  virtual int run_upgrade_processor(share::ObBaseUpgradeProcessor &processor) = 0;
  uint64_t get_tenant_id() { return tenant_id_; }
protected:
  int check_inner_stat_();
protected:
  uint64_t tenant_id_;
  common::ObMySQLProxy *sql_proxy_;
};

}//end rootserver
}//end oceanbase
#endif // OCEANBASE_UPGRADE_EXECUTOR_H
