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

#ifndef OCEANBASE_SHARE_OB_BACKGROUND_TASK_EXECUTOR_H_
#define OCEANBASE_SHARE_OB_BACKGROUND_TASK_EXECUTOR_H_

#include "lib/lock/ob_mutex.h"
#include "lib/task/ob_timer.h"
#include "lib/thread/ob_simple_thread_pool.h"

namespace oceanbase
{
namespace share
{

// Phase-0 prototype for merging low-frequency background queue workers.
// Business task ownership, deduplication and batching remain in TaskSource.
enum ObBackgroundTaskPriority
{
  BG_TASK_HIGH = 0,
  BG_TASK_NORMAL,
  BG_TASK_LOW,
  BG_TASK_PRIORITY_COUNT
};

struct ObBackgroundTaskSourceHandle
{
  ObBackgroundTaskSourceHandle() : slot_id_(-1), generation_(0) {}

  bool is_valid() const { return slot_id_ >= 0 && generation_ > 0; }
  void reset()
  {
    slot_id_ = -1;
    generation_ = 0;
  }

  int64_t slot_id_;
  uint64_t generation_;
};

struct ObBackgroundTaskSourceConfig
{
  ObBackgroundTaskSourceConfig();

  bool is_valid(const int64_t max_worker_count) const;

  const char *name_;
  int64_t max_concurrency_;
  int64_t max_concurrency_by_priority_[BG_TASK_PRIORITY_COUNT];
};

struct ObBackgroundTaskRunResult
{
  ObBackgroundTaskRunResult()
    : processed_count_(0),
      has_more_ready_(false),
      next_ready_ts_(0)
  {}

  int64_t processed_count_;
  bool has_more_ready_;
  // Absolute timestamp in microseconds. When no task is immediately runnable,
  // the executor publishes one coalesced notification at this time.
  int64_t next_ready_ts_;
};

class ObIBackgroundTaskSource
{
public:
  virtual ~ObIBackgroundTaskSource() {}

  // Claims and processes one bounded quantum from the selected base-priority
  // lane. The source owns its queue and must make claiming thread-safe. On
  // failure it must retain or requeue uncompleted work before returning.
  virtual int process_one_quantum(
      const ObBackgroundTaskPriority priority,
      ObBackgroundTaskRunResult &result) = 0;
};

class ObBackgroundTaskExecutor : private common::ObSimpleThreadPool
{
public:
  static constexpr int64_t MAX_SOURCE_COUNT = 32;
  static constexpr int64_t MAX_WORKER_COUNT = 8;
  // Creation remains lazy, but periodic Sources repeatedly drive a 1-worker
  // floor back to the startup high-water mark. Retain the observed six-worker
  // mini-mode high-water mark to avoid reallocating pthread/TLS/stack state
  // every shrink interval. Two additional slots remain as burst/rescue
  // headroom for callbacks that may unblock synchronous metadata writes.
  static constexpr int64_t MINI_MODE_WARM_WORKER_COUNT = 6;
  static constexpr int64_t MINI_MODE_MAX_WORKER_COUNT = 8;
  static_assert(MAX_SOURCE_COUNT <= 64,
      "background source ready bitmap only supports 64 slots");

  ObBackgroundTaskExecutor();
  virtual ~ObBackgroundTaskExecutor();

  int init();
  int init(
      const int64_t max_worker_count,
      const int64_t min_worker_count = 1);
  void stop();
  void wait();
  void destroy();
  bool is_inited() const { return is_inited_; }

  int register_source(
      ObIBackgroundTaskSource &source,
      const ObBackgroundTaskSourceConfig &config,
      ObBackgroundTaskSourceHandle &handle);

  // The first call atomically moves the source to STOPPING and rejects future
  // notifications. It returns OB_EAGAIN while a claimed quantum is still
  // running; the caller must keep the source alive and retry.
  int unregister_source(ObBackgroundTaskSourceHandle &handle);

  // notify() publishes source readiness, not a business-task pointer.
  // Repeated notifications are coalesced into one ready bit. A notification
  // during execution may expose one additional runnable quantum, so worker
  // growth is gradual rather than immediately jumping to max_concurrency.
  int notify(
      const ObBackgroundTaskSourceHandle &handle,
      const ObBackgroundTaskPriority priority);

  int64_t get_worker_count() const;
  int64_t get_idle_worker_count() const;
  int64_t get_registered_source_count() const;

private:
  enum DispatchTokenState
  {
    TOKEN_IDLE = 0,
    TOKEN_QUEUED,
    TOKEN_RUNNING
  };

  enum SourceState
  {
    SOURCE_UNREGISTERED = 0,
    SOURCE_REGISTERED,
    SOURCE_STOPPING
  };

  struct PriorityLaneState
  {
    PriorityLaneState()
      : notify_epoch_(0),
        suspended_epoch_(0),
        runnable_limit_(0),
        running_count_(0),
        delayed_notify_ts_(0)
    {}

    void reset()
    {
      notify_epoch_ = 0;
      suspended_epoch_ = 0;
      runnable_limit_ = 0;
      running_count_ = 0;
      delayed_notify_ts_ = 0;
    }

    uint64_t notify_epoch_;
    uint64_t suspended_epoch_;
    int64_t runnable_limit_;
    int64_t running_count_;
    int64_t delayed_notify_ts_;
  };

  class DelayedNotifyTask : public common::ObTimerTask
  {
  public:
    DelayedNotifyTask()
      : owner_(NULL),
        slot_id_(-1),
        priority_(BG_TASK_NORMAL)
    {}

    void init(
        ObBackgroundTaskExecutor *owner,
        const int64_t slot_id,
        const ObBackgroundTaskPriority priority)
    {
      owner_ = owner;
      slot_id_ = slot_id;
      priority_ = priority;
    }

  private:
    virtual void runTimerTask() override;

    ObBackgroundTaskExecutor *owner_;
    int64_t slot_id_;
    ObBackgroundTaskPriority priority_;
  };

  struct SourceSlot
  {
    SourceSlot();

    void reset_for_register(
        ObIBackgroundTaskSource &source,
        const ObBackgroundTaskSourceConfig &config);
    void reset_after_unregister();

    ObIBackgroundTaskSource *source_;
    SourceState state_;
    ObBackgroundTaskSourceConfig config_;
    PriorityLaneState lanes_[BG_TASK_PRIORITY_COUNT];
    int64_t running_count_;
    uint64_t generation_;
  };

  struct DispatchToken
  {
    DispatchToken() : owner_(NULL), token_id_(-1), state_(TOKEN_IDLE) {}

    ObBackgroundTaskExecutor *owner_;
    int64_t token_id_;
    DispatchTokenState state_;
  };

  struct DispatchCandidate
  {
    DispatchCandidate();

    ObIBackgroundTaskSource *source_;
    int64_t slot_id_;
    uint64_t generation_;
    ObBackgroundTaskPriority priority_;
    uint64_t notify_epoch_;
  };

  virtual void handle(void *task) override;
  virtual void handle_drop(void *task) override;

  bool is_valid_handle_locked(const ObBackgroundTaskSourceHandle &handle) const;
  bool pick_candidate_locked(DispatchCandidate &candidate);
  void finish_candidate_locked(
      const DispatchCandidate &candidate,
      const ObBackgroundTaskRunResult &result,
      const int process_ret);
  int schedule_delayed_notify(
      const DispatchCandidate &candidate,
      const int64_t ready_ts);
  void on_delayed_notify(
      const int64_t slot_id,
      const ObBackgroundTaskPriority priority);
  int ensure_dispatch_tokens_locked();
  bool is_lane_runnable_locked(
      const int64_t slot_id,
      const ObBackgroundTaskPriority priority) const;
  int64_t get_runnable_capacity_locked() const;
  int64_t get_active_token_count_locked() const;
  void clear_ready_bits_locked(const int64_t slot_id);

private:
  mutable lib::ObMutex lock_;
  bool is_inited_;
  bool stopping_;
  int64_t max_worker_count_;
  SourceSlot source_slots_[MAX_SOURCE_COUNT];
  DispatchToken dispatch_tokens_[MAX_WORKER_COUNT];
  uint64_t ready_sources_[BG_TASK_PRIORITY_COUNT];
  int64_t source_rr_cursor_[BG_TASK_PRIORITY_COUNT];
  int64_t priority_schedule_cursor_;
  common::ObTimer delayed_notify_timer_;
  DelayedNotifyTask
      delayed_notify_tasks_[MAX_SOURCE_COUNT][BG_TASK_PRIORITY_COUNT];

  DISALLOW_COPY_AND_ASSIGN(ObBackgroundTaskExecutor);
};

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_OB_BACKGROUND_TASK_EXECUTOR_H_
