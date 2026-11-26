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

#ifndef _OB_LOG_ALLOCATOR_H_
#define _OB_LOG_ALLOCATOR_H_
#include "lib/alloc/alloc_struct.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/ob_block_alloc_mgr.h"
#include "lib/allocator/ob_slice_alloc.h"
#include "lib/allocator/ob_vslice_alloc.h"
#include "lib/queue/ob_link.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace logservice
{
class ObLogReplayTask;
}
namespace common
{
class ObTraceProfile;
// Interface for Clog module
class ObILogAllocator : public ObIAllocator
{
public:
  ObILogAllocator() = default;
  virtual ~ObILogAllocator() {}

public:
  virtual void *alloc(const int64_t size) = 0;
  virtual void* alloc(const int64_t size, const ObMemAttr &attr) = 0;
  virtual void free(void *ptr) = 0;
  virtual void *ge_alloc(const int64_t size) = 0;
  virtual void ge_free(void *ptr) = 0;
  virtual const ObBlockAllocMgr &get_clog_blk_alloc_mgr() const = 0;
  virtual void *alloc_replay_task(const int64_t size) = 0;
  virtual void *alloc_replay_log_buf(const int64_t size) = 0;
  virtual void free_replay_task(logservice::ObLogReplayTask *ptr) = 0;
  virtual void free_replay_log_buf(void *ptr) = 0;
};

class ObLogAllocator
    : public ObILogAllocator
{
public:
  // The memory percent of clog
  const int64_t CLOG_MEM_LIMIT_PERCENT = 30;
  // The memory percent of replay engine
  const int64_t REPLAY_MEM_LIMIT_PERCENT = 5;
  // The memory limit of replay engine
  const int64_t REPLAY_MEM_LIMIT_THRESHOLD = 512 * 1024 * 1024ll;
  // The memory percent of replay engine for inner_table
public:
  explicit ObLogAllocator();
  ~ObLogAllocator();
  void destroy();
  // Update the allocator concurrency when the server CPU limit changes.
  void set_nway(const int32_t nway);
  // Update the allocator limit when the server memory limit changes.
  void set_limit(const int64_t total_limit);
  int64_t get_limit() const;

  void *alloc(const int64_t size);
  void* alloc(const int64_t size, const ObMemAttr &attr);
  void free(void *ptr);
  void *ge_alloc(const int64_t size);
  void ge_free(void *ptr);
  const ObBlockAllocMgr &get_clog_blk_alloc_mgr() const;
  void *alloc_replay_task(const int64_t size);
  void *alloc_replay_log_buf(const int64_t size);
  void free_replay_task(logservice::ObLogReplayTask *ptr);
  void free_replay_log_buf(void *ptr);

private:
  int64_t total_limit_;
  int64_t pending_replay_mutator_size_;
  ObBlockAllocMgr clog_blk_alloc_;
  ObBlockAllocMgr replay_log_task_blk_alloc_;
  ObVSliceAlloc clog_ge_alloc_;
  ObVSliceAlloc replay_log_task_alloc_;
};

} // end of namespace common
} // end of namespace oceanbase

#endif /* _OB_LOG_ALLOCATOR_H_ */
