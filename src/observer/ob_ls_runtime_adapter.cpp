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

#define USING_LOG_PREFIX SERVER

#include "observer/ob_ls_runtime_adapter.h"

#include "data_plane/ob_log_service_handler.h"
#include "observer/dbms_scheduler/ob_dbms_sched_service.h"
#include "observer/vector_index/ob_plugin_vector_index_scheduler.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "rootserver/ddl_task/ob_ddl_scheduler.h"
#include "rootserver/freeze/ob_major_freeze_service.h"
#include "rootserver/ob_ddl_service_launcher.h"
#include "observer/ob_system_package_load_service.h"

namespace oceanbase
{
namespace observer
{
namespace
{
template <typename Handler>
int set_log_service_handler(
    Handler *module,
    data_plane::ObLogServiceHandler &handler)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(module)) {
    ret = common::OB_NOT_INIT;
  } else {
    handler.set(
        static_cast<logservice::ObIReplaySubHandler *>(module),
        static_cast<logservice::ObILocalLogHandler *>(module),
        static_cast<logservice::ObICheckpointSubHandler *>(module));
  }
  return ret;
}
} // namespace

ObLSRuntimeAdapter::ObLSRuntimeAdapter()
  : primary_major_freeze_service_(nullptr),
    dbms_sched_service_(nullptr),
    ddl_scheduler_(nullptr),
    ddl_service_launcher_(nullptr),
    sys_package_service_(nullptr),
    vector_index_service_(nullptr)
{
}

int ObLSRuntimeAdapter::init(
    rootserver::ObPrimaryMajorFreezeService &primary_major_freeze_service,
    rootserver::ObDBMSSchedService &dbms_sched_service,
    rootserver::ObDDLScheduler &ddl_scheduler,
    rootserver::ObDDLServiceLauncher &ddl_service_launcher,
    rootserver::ObSystemPackageLoadService &sys_package_service,
    share::ObPluginVectorIndexService &vector_index_service)
{
  primary_major_freeze_service_ = &primary_major_freeze_service;
  dbms_sched_service_ = &dbms_sched_service;
  ddl_scheduler_ = &ddl_scheduler;
  ddl_service_launcher_ = &ddl_service_launcher;
  sys_package_service_ = &sys_package_service;
  vector_index_service_ = &vector_index_service;
  return common::OB_SUCCESS;
}

int ObLSRuntimeAdapter::resolve_log_handler(
    int64_t log_type,
    data_plane::ObLogServiceHandler &handler)
{
  int ret = common::OB_SUCCESS;
  switch (static_cast<logservice::ObLogBaseType>(log_type)) {
    case logservice::MAJOR_FREEZE_LOG_BASE_TYPE:
      ret = set_log_service_handler(primary_major_freeze_service_, handler);
      break;
    case logservice::DBMS_SCHEDULER_LOG_BASE_TYPE:
      ret = set_log_service_handler(dbms_sched_service_, handler);
      break;
    case logservice::SYS_DDL_SCHEDULER_LOG_BASE_TYPE:
      ret = set_log_service_handler(ddl_scheduler_, handler);
      break;
    case logservice::DDL_SERVICE_LAUNCHER_LOG_BASE_TYPE:
      ret = set_log_service_handler(ddl_service_launcher_, handler);
      break;
    case logservice::SYSTEM_PACKAGE_LOAD_SERVICE_LOG_BASE_TYPE:
      ret = set_log_service_handler(sys_package_service_, handler);
      break;
    case logservice::VEC_INDEX_SERVICE_LOG_BASE_TYPE:
      ret = set_log_service_handler(vector_index_service_, handler);
      break;
    default:
      ret = common::OB_NOT_SUPPORTED;
      break;
  }
  return ret;
}

int ObLSRuntimeAdapter::create_vector_index_scheduler(
    storage::ObLS &ls,
    common::ObTimer &timer,
    data_plane::ObIVectorIndexScheduler *&scheduler)
{
  using SchedulerImpl = share::ObPluginVectorIndexLoadScheduler;
  int ret = common::OB_SUCCESS;
  SchedulerImpl *scheduler_impl = nullptr;
  if (OB_NOT_NULL(scheduler)) {
    ret = common::OB_INIT_TWICE;
    LOG_WARN("vector index scheduler already exists", KR(ret), KP(scheduler));
  } else if (OB_ISNULL(
                 scheduler_impl = OB_NEW(SchedulerImpl, ObMemAttr("VecIdxScheduler")))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate vector index scheduler", KR(ret));
  } else if (OB_FAIL(scheduler_impl->init(&ls, timer))) {
    LOG_WARN("fail to init vector index scheduler", KR(ret), KP(&ls));
    scheduler_impl->destroy();
    OB_DELETE(SchedulerImpl, "VecIdxScheduler", scheduler_impl);
  } else {
    scheduler = scheduler_impl;
  }
  return ret;
}

void ObLSRuntimeAdapter::destroy_vector_index_scheduler(
    data_plane::ObIVectorIndexScheduler *&scheduler)
{
  using SchedulerImpl = share::ObPluginVectorIndexLoadScheduler;
  if (OB_NOT_NULL(scheduler)) {
    SchedulerImpl *scheduler_impl = static_cast<SchedulerImpl *>(scheduler);
    scheduler_impl->destroy();
    OB_DELETE(SchedulerImpl, "VecIdxScheduler", scheduler_impl);
    scheduler = nullptr;
  }
}

} // namespace observer
} // namespace oceanbase
