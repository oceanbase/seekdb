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

#ifndef OCEANBASE_ARCHIVE_OB_ARCHIVE_SCHEDULER_H_
#define OCEANBASE_ARCHIVE_OB_ARCHIVE_SCHEDULER_H_

#include "lib/utility/ob_macro_utils.h"
#include <cstdint>
namespace oceanbase
{
namespace archive
{
class ObArchiveFetcher;
class ObArchiveSender;
class ObArchiveAllocator;
// modify archive resource parameters and monitor memory usage and so on
// until now, it modify sender and fetcher threads, cached buffer usage
class ObArchiveScheduler
{
public:
  ObArchiveScheduler();
  ~ObArchiveScheduler();

  int init(const uint64_t tenant_id,
      ObArchiveFetcher *fetcher,
      ObArchiveSender *sender,
      ObArchiveAllocator *allocator);
  void destroy();
  int schedule();

private:
  int modify_thread_count_();
  int purge_cached_buffer_();
private:
  bool inited_;
  uint64_t tenant_id_;
  ObArchiveFetcher *fetcher_;
  ObArchiveSender *sender_;
  ObArchiveAllocator *allocator_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObArchiveScheduler);
};
} // namespace archive
} // namespace oceanbase

#endif /* OCEANBASE_ARCHIVE_OB_ARCHIVE_SCHEDULER_H_ */
