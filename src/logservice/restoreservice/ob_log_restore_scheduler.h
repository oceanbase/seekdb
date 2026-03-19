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
#ifndef OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_SCHEDULER_H_
#define OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_SCHEDULER_H_

#include "lib/utility/ob_macro_utils.h"
#include "share/restore/ob_log_restore_source.h"   // ObLogRestoreSourceType
#include <cstdint>

namespace oceanbase
{
namespace logservice
{
class ObLogRestoreAllocator;
class ObRemoteFetchWorker;
class ObLogRestoreScheduler
{
public:
  ObLogRestoreScheduler();
  ~ObLogRestoreScheduler();

  int init(const uint64_t tenant_id, ObLogRestoreAllocator *allocator, ObRemoteFetchWorker *worker);
  void destroy();
  int schedule(const share::ObLogRestoreSourceType &source_type);

private:
  int modify_thread_count_(const share::ObLogRestoreSourceType &source_type);
  int purge_cached_buffer_();

private:
  bool inited_;
  uint64_t tenant_id_;
  ObRemoteFetchWorker *worker_;
  ObLogRestoreAllocator *allocator_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObLogRestoreScheduler);
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_SCHEDULER_H_ */
