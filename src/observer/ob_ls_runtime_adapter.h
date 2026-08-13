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

#ifndef OCEANBASE_OBSERVER_OB_LS_RUNTIME_ADAPTER_H_
#define OCEANBASE_OBSERVER_OB_LS_RUNTIME_ADAPTER_H_

#include "storage/ls/ob_i_ls_runtime_adapter.h"

namespace oceanbase
{
namespace rootserver
{
class ObPrimaryMajorFreezeService;
class ObDBMSSchedService;
class ObDDLScheduler;
class ObDDLServiceLauncher;
class ObSystemPackageLoadService;
}
namespace share
{
class ObPluginVectorIndexService;
}
namespace observer
{
class ObLSRuntimeAdapter final : public storage::ObILSRuntimeAdapter
{
public:
  ObLSRuntimeAdapter();

  int init(
      rootserver::ObPrimaryMajorFreezeService &primary_major_freeze_service,
      rootserver::ObDBMSSchedService &dbms_sched_service,
      rootserver::ObDDLScheduler &ddl_scheduler,
      rootserver::ObDDLServiceLauncher &ddl_service_launcher,
      rootserver::ObSystemPackageLoadService &sys_package_service,
      share::ObPluginVectorIndexService &vector_index_service);

  int resolve_log_handler(
      int64_t log_type,
      data_plane::ObLogServiceHandler &handler) override;
  int create_vector_index_scheduler(
      storage::ObLS &ls,
      common::ObTimer &timer,
      data_plane::ObIVectorIndexScheduler *&scheduler) override;
  void destroy_vector_index_scheduler(
      data_plane::ObIVectorIndexScheduler *&scheduler) override;

private:
  rootserver::ObPrimaryMajorFreezeService *primary_major_freeze_service_;
  rootserver::ObDBMSSchedService *dbms_sched_service_;
  rootserver::ObDDLScheduler *ddl_scheduler_;
  rootserver::ObDDLServiceLauncher *ddl_service_launcher_;
  rootserver::ObSystemPackageLoadService *sys_package_service_;
  share::ObPluginVectorIndexService *vector_index_service_;
};

} // namespace observer
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_LS_RUNTIME_ADAPTER_H_
