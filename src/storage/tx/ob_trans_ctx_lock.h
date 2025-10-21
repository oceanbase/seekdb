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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_CTX_LOCK_
#define OCEANBASE_TRANSACTION_OB_TRANS_CTX_LOCK_

#include "lib/list/ob_dlist.h"
#include "lib/lock/ob_latch.h"
#include "lib/list/ob_dlink_node.h"
#include "lib/objectpool/ob_concurrency_objpool.h"
#include "ob_trans_end_trans_callback.h"

namespace oceanbase
{
namespace memtable
{
class ObIMemtableCtx;
}
namespace transaction
{
class ObTransService;
class ObTransCtx;

class LocalTask : public common::ObDLinkBase<LocalTask>
{
public:
  LocalTask(int64_t msg_type) {}
  ~LocalTask() {}
};

typedef common::ObDList<LocalTask> LocalTaskList;

class CtxLockArg
{
public:
  CtxLockArg() : ls_id_(), trans_id_(), task_list_(), commit_cb_(),
      has_pending_callback_(false), need_retry_redo_sync_(false),
      p_mt_ctx_(NULL) {}
public:
  share::ObLSID ls_id_;
  ObTransID trans_id_;
  LocalTaskList task_list_;
  ObTxCommitCallback commit_cb_;
  bool has_pending_callback_;
  bool need_retry_redo_sync_;
  // It is used to wake up lock queue after submitting the log for elr transaction
  memtable::ObIMemtableCtx *p_mt_ctx_;
};

class CtxLock
{
public:
CtxLock() : ctx_lock_(), access_lock_(), flush_redo_lock_(),
            ctx_(NULL), lock_start_ts_(0), waiting_lock_cnt_(0) {}
  ~CtxLock() {}
  int init(ObTransCtx *ctx);
  int lock(const int64_t timeout_us = -1);
  int try_lock();
  void unlock();
  int try_rdlock_ctx();
  int wrlock_ctx();
  int wrlock_access();
  int wrlock_flush_redo();
  int rdlock_flush_redo();
  void unlock_ctx();
  void unlock_access();
  int try_wrlock_flush_redo();
  int try_rdlock_flush_redo();
  void unlock_flush_redo();
  void before_unlock(CtxLockArg &arg);
  void after_unlock(CtxLockArg &arg);
  ObTransCtx *get_ctx() { return ctx_; }
  bool is_locked_by_self() const { return ctx_lock_.is_wrlocked_by(); }
  int64_t get_waiting_lock_cnt() const { return ATOMIC_LOAD(&waiting_lock_cnt_); }
private:
  static const int64_t WARN_LOCK_TS = 1 * 1000 * 1000;
  DISALLOW_COPY_AND_ASSIGN(CtxLock);
private:
  common::ObLatch ctx_lock_;
  common::ObLatch access_lock_;
  common::ObLatch flush_redo_lock_;
  ObTransCtx *ctx_;
  int64_t lock_start_ts_;
  int64_t waiting_lock_cnt_;
};

class CtxLockGuard
{
public:
  enum MODE { CTX = 1, ACCESS = 2, REDO_FLUSH_X = 4, REDO_FLUSH_R = 8, ALL = (CTX | REDO_FLUSH_X | ACCESS) };
  CtxLockGuard() : lock_(NULL), mode_(0), request_ts_(0), hold_ts_(0) {}
  explicit CtxLockGuard(CtxLock &lock, int mode, bool need_lock = true): lock_(&lock), mode_(mode)
  { do_lock_(need_lock); }
  void do_lock_(bool need_lock)
  {
    request_ts_ = ObTimeUtility::fast_current_time();
    if (mode_ & ACCESS) {
      if (need_lock) {
        lock_->wrlock_access();
      }
    }
    if (mode_ & REDO_FLUSH_X) {
      if (need_lock) {
        lock_->wrlock_flush_redo();
      }
    }
    if (mode_ & REDO_FLUSH_R) {
      if (need_lock) {
        lock_->rdlock_flush_redo();
      }
    }
    if (mode_ & CTX) {
      if (need_lock) {
        lock_->wrlock_ctx();
      }
    }
    hold_ts_ = ObTimeUtility::fast_current_time();
  }
  explicit CtxLockGuard(CtxLock &lock, const bool need_lock = true)
    : CtxLockGuard(lock, MODE::ALL, need_lock) {}
  ~CtxLockGuard();
  void set(CtxLock &lock, uint8_t mode = MODE::ALL);
  void reset();
  int64_t get_hold_ts() { return hold_ts_; }
  int64_t get_lock_acquire_used_time() const
  {
    return hold_ts_ - request_ts_;
  }
private:
  DISALLOW_COPY_AND_ASSIGN(CtxLockGuard);
private:
  CtxLock *lock_;
  uint8_t mode_;
  int64_t request_ts_;
  int64_t hold_ts_;
};


} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TRANS_CTX_LOCK_
