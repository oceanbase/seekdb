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

#ifndef OCEANBASE_ARCHIVE_TASK_QUEUE_H_
#define OCEANBASE_ARCHIVE_TASK_QUEUE_H_

#include "share/ob_ls_id.h"     // ObLSID
#include "ob_archive_util.h"
#include <cstdint>

namespace oceanbase
{
namespace archive
{
class ObArchiveWorker;
using oceanbase::share::ObLSID;
struct ObArchiveTaskStatus : common::ObLink
{
  static const int64_t PRINT_WARN_THRESHOLD = 30 * 1000 * 1000L;   // 30s
public:
  ObArchiveTaskStatus(const ObLSID &id);
  virtual ~ObArchiveTaskStatus();
  int64_t count();
  void inc_ref();
  int push(common::ObLink *task, ObArchiveWorker &worker);
  int pop(ObLink *&link, bool &task_exist);
  int top(ObLink *&link, bool &task_exist);
  int get_next(ObLink *&link, bool &task_exist);
  int retire(bool &is_empty, bool &is_discarded);  // Release from global public queue
  void free(bool &is_discarded);   // Release this structure pointer
  bool mark_io_error();
  void clear_error_info();
  void print_self();

  TO_STRING_KV(K_(issue),
               K_(ref),
               K_(num),
               K_(id));
private:
  int retire_unlock_(bool &is_discarded);  // Release from global public queue
  void print_self_();

  typedef common::SpinRWLock RWLock;
  typedef common::SpinRLockGuard  RLockGuard;
  typedef common::SpinWLockGuard  WLockGuard;
protected:
  bool                      issue_;     // Mark whether this structure is attached to the public queue
  int64_t                   ref_;       // the reference count of this structure
  int64_t                   num_;       // total number of tasks
  int64_t                   last_issue_timestamp_;
  ObLSID                    id_;
  SimpleQueue               queue_;     // task queue
  mutable RWLock            rwlock_;
};
}
}

#endif /* OCEANBASE_ARCHIVE_TASK_QUEUE_H_ */
