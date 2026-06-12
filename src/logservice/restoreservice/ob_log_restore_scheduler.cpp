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
#include "ob_log_restore_scheduler.h"
#include "ob_remote_fetch_log_worker.h"
#include "ob_log_restore_allocator.h"

namespace oceanbase
{
namespace logservice
{
using namespace oceanbase::common;
ObLogRestoreScheduler::ObLogRestoreScheduler() :
  inited_(false),
  tenant_id_(OB_INVALID_TENANT_ID),
  worker_(NULL)
{}

ObLogRestoreScheduler::~ObLogRestoreScheduler()
{
  destroy();
}

int ObLogRestoreScheduler::init(const uint64_t tenant_id,
    ObLogRestoreAllocator *allocator,
    ObRemoteFetchWorker *worker)
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "ObLogRestoreScheduler init twice", K(ret), K(inited_));
  } else if (OB_INVALID_TENANT_ID == tenant_id
      || NULL == allocator
      || NULL == worker) {
    ret = OB_INVALID_ARGUMENT;
    CLOG_LOG(WARN, "invalid argument", K(ret), K(tenant_id), K(allocator), K(worker));
  } else {
    tenant_id_ = tenant_id;
    allocator_ = allocator;
    worker_ = worker;
    inited_ = true;
    CLOG_LOG(INFO, "ObLogRestoreScheduler init succ", K(tenant_id_));
  }
  return ret;
}

void ObLogRestoreScheduler::destroy()
{
  inited_ = false;
  tenant_id_ = OB_INVALID_TENANT_ID;
  worker_ = NULL;
}

int ObLogRestoreScheduler::schedule(const share::ObLogRestoreSourceType &source_type)
{
  (void)modify_thread_count_(source_type);
  (void)purge_cached_buffer_();
  return OB_SUCCESS;
}

int ObLogRestoreScheduler::modify_thread_count_(const share::ObLogRestoreSourceType &source_type)
{
  int ret = OB_SUCCESS;
  const int64_t MIN_LOG_RESTORE_CONCURRENCY = 1;
  // Only SERVICE-type log restore is supported now; it does not need extra
  // restore concurrency (archive LOCATION/RAWPATH reading has been removed).
  UNUSED(source_type);
  const int64_t restore_concurrency = MIN_LOG_RESTORE_CONCURRENCY;
  if (OB_FAIL(worker_->modify_thread_count(restore_concurrency))) {
    CLOG_LOG(WARN, "modify worker thread failed", K(ret));
  }
  return ret;
}

int ObLogRestoreScheduler::purge_cached_buffer_()
{
  int ret = OB_SUCCESS;
  allocator_->weed_out_iterator_buffer();
  return ret;
}
} // namespace logservice
} // namespace oceanbase
