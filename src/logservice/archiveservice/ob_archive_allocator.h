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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_ALLOCATOR_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_ALLOCATOR_H_

#include "lib/allocator/ob_small_allocator.h"
#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "large_buffer_pool.h"
#include <cstdint>

namespace oceanbase
{
namespace share
{
class ObLSID;
}

namespace archive
{
class ObArchiveLogFetchTask;
class ObArchiveSendTask;
class ObArchiveTaskStatus;
using oceanbase::share::ObLSID;
class ObArchiveAllocator
{
public:
  ObArchiveAllocator();
  ~ObArchiveAllocator();
  int init(const uint64_t tenant_id);
  void destroy();

public:
  // allocate ObArchiveLogFetchTask
  ObArchiveLogFetchTask *alloc_log_fetch_task();

  // free ObArchiveLogFetchTask
  void free_log_fetch_task(ObArchiveLogFetchTask *task);

  // allocate ObArchiveSendTask, include SendTask and log buffer
  char *alloc_send_task(const int64_t buf_len);

  // free ObArchiveSendTask, include SendTask and log buffer
  void free_send_task(void *buf);

  // week out cached send task buffer
  void weed_out_send_task();

  // alloc log handle buffer
  void *alloc_log_handle_buffer(const int64_t size);

  // free log handle buffer
  void free_log_handle_buffer(char *buf);

  // alloc send task status
  ObArchiveTaskStatus *alloc_send_task_status(const ObLSID &id);

  // free send task status
  void free_send_task_status(ObArchiveTaskStatus *status);

  void print_state();
private:
  typedef LargeBufferPool ArchiveSendAllocator;
  bool                                  inited_;
  common::ObSmallAllocator              log_fetch_task_allocator_;
  //common::ObConcurrentFIFOAllocator     send_task_allocator_;
  ArchiveSendAllocator                  send_task_allocator_;
  common::ObSmallAllocator              send_task_status_allocator_;
};
} // namespace archive
} // namespace oceanbase
#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_ALLOCATOR_H_*/
