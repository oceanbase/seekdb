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

#ifndef OB_DIAGNOSE_INFO_H_
#define OB_DIAGNOSE_INFO_H_

#include "lib/wait_event/ob_wait_event.h"
#include "lib/statistic_event/ob_stat_event.h"
#include "lib/stat/ob_stat_template.h"
#include "lib/stat/ob_latch_define.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/ob_lib_config.h"
#include "lib/thread/thread.h"

namespace oceanbase
{
namespace common
{
static const int16_t SESSION_WAIT_HISTORY_CNT = 10;
typedef ObStatArray<ObWaitEventStat, WAIT_EVENTS_TOTAL> ObWaitEventStatArray;
typedef ObStatArray<ObStatEventAddStat, ObStatEventIds::STAT_EVENT_ADD_END> ObStatEventAddStatArray;
typedef ObStatArray<ObStatEventSetStat, ObStatEventIds::STAT_EVENT_SET_END - ObStatEventIds::STAT_EVENT_ADD_END -1> ObStatEventSetStatArray;

struct ObLatchStat
{
  ObLatchStat();
  int add(const ObLatchStat &other);
  void reset();
  uint64_t gets_;
  uint64_t misses_;
  uint64_t sleeps_;
  uint64_t immediate_gets_;
  uint64_t immediate_misses_;
  uint64_t spin_gets_;
  uint64_t wait_time_;
};

typedef ObStatArray<ObLatchStat, ObLatchIds::LATCH_END> ObStatLatchArray;

struct ObLatchStatArray
{
public:
  ObLatchStatArray(ObIAllocator *allocator = NULL);
  ~ObLatchStatArray();
  int add(const ObLatchStatArray &other);
  int add(ObStatLatchArray &other)
  {
    int ret = OB_SUCCESS;
    ObLatchStat *cur = nullptr;
    for (int i = 0; i < ObLatchIds::LATCH_END; i++) {
      cur = other.get(i);
      if (cur->gets_ || cur->spin_gets_) {
        ObLatchStat *target = get_or_create_item(i);
        if (OB_NOT_NULL(target)) {
          ret = target->add(*cur);
        } else {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          break;
        }
      }
    }
    return ret;
  }
  void reset();
  ObLatchStat *get_item(int32_t idx) const
  {
    return items_[idx];
  }
  ObLatchStat *get_or_create_item(int32_t idx)
  {
    if (OB_ISNULL(items_[idx])) {
      items_[idx] = create_item();
    }
    return items_[idx];
  }
  void accumulate_to(ObStatLatchArray &array)
  {
    for (int64_t i = 0; i < ObLatchIds::LATCH_END; ++i) {
      if (OB_ISNULL(items_[i])) {
      } else {
        array.get(i)->add(*items_[i]);
      }
    }
  }

private:
  ObLatchStat *create_item();
  void free_item(ObLatchStat *stat);
private:
  ObIAllocator *allocator_;
  ObLatchStat *items_[ObLatchIds::LATCH_END] = {NULL};
};

class ObWaitEventHistoryIter
{
public:
  ObWaitEventHistoryIter();
  virtual ~ObWaitEventHistoryIter();
  int init(ObWaitEventDesc *items, const int64_t start_pos, int64_t item_cnt);
  int get_next(ObWaitEventDesc *&item);
  void reset();
private:
  ObWaitEventDesc *items_;
  int64_t curr_;
  int64_t start_pos_;
  int64_t item_cnt_;
};

class ObWaitEventHistory
{
public:
  ObWaitEventHistory();
  virtual ~ObWaitEventHistory();
  int push(const int64_t event_no, const uint64_t timeout_ms,
      const uint64_t p1, const uint64_t p2, const uint64_t p3);
  int add(const ObWaitEventHistory &other);
  int get_iter(ObWaitEventHistoryIter &iter);
  int get_last_wait(ObWaitEventDesc *&item);
  int get_curr_wait(ObWaitEventDesc *&item);
  int get_accord_event(ObWaitEventDesc *&event_desc);
  int calc_wait_time(ObWaitEventDesc *&event_desc);
  void reset();
private:
  int get_next_and_compare(int64_t &iter_1, int64_t &iter_2, int64_t &cnt, const ObWaitEventHistory &other, ObWaitEventDesc *tmp);
private:
  int64_t curr_pos_;
  int64_t item_cnt_;
  int64_t nest_cnt_;
  int64_t current_wait_;
  ObWaitEventDesc items_[SESSION_WAIT_HISTORY_CNT];
};

class ObDiagnoseSessionInfo
{
public:
  ObDiagnoseSessionInfo();
  virtual ~ObDiagnoseSessionInfo();
  int add(ObDiagnoseSessionInfo &other);
  void reset();
  int notify_wait_begin(
      const int64_t event_no,
      const uint64_t timeout_ms = 0,
      const uint64_t p1 = 0,
      const uint64_t p2 = 0,
      const uint64_t p3 = 0,
      const bool is_atomic = false);
  int notify_wait_end(class ObDiagnoseRuntimeInfo *runtime_info,
      const bool is_atomic, const bool is_idle);
  int set_max_wait(ObWaitEventDesc *max_wait)
  {
    max_wait_ = max_wait;
    return OB_SUCCESS;
  }

  int set_total_wait(ObWaitEventStat *total_wait)
  {
    total_wait_ = total_wait;
    return OB_SUCCESS;
  }
  ObWaitEventDesc &get_curr_wait();
  int inc_stat(const int16_t stat_no);
  int update_stat(const int16_t stat_no, const int64_t delta);
  static ObDiagnoseSessionInfo *get_local_diagnose_info();
  inline ObWaitEventHistory &get_event_history()  { return event_history_; }
  inline ObWaitEventStatArray &get_event_stats()  { return event_stats_; }
  inline ObStatEventAddStatArray &get_add_stat_stats()  { return stat_add_stats_; }
  inline void reset_max_wait() { max_wait_ = NULL; }
  inline void reset_total_wait() { total_wait_ = NULL; }
  inline ObWaitEventDesc *get_max_wait() { return max_wait_; }
  inline ObWaitEventStat *get_total_wait() { return total_wait_; }
  inline bool is_valid() const { return true; }
  const ObWaitEventDesc &get_curr_wait() const
  {
    return curr_wait_;
  };
  void set_curr_wait(ObWaitEventDesc &wait)
  {
    curr_wait_ = wait;
  };
  
  TO_STRING_EMPTY();
private:
  ObWaitEventDesc curr_wait_;
  ObWaitEventDesc *max_wait_;
  ObWaitEventStat *total_wait_;
  ObWaitEventHistory event_history_;
  ObWaitEventStatArray event_stats_;
  ObStatEventAddStatArray stat_add_stats_;
  
  DIRWLock lock_;
};

// Process-wide diagnostic counters.  seekdb has one runtime, so this object
// deliberately has no runtime id and no id-to-bucket lookup layer.
class ObDiagnoseRuntimeInfo final
{
public:
  explicit ObDiagnoseRuntimeInfo(ObIAllocator *allocator = NULL);
  ~ObDiagnoseRuntimeInfo();
  int add(const ObDiagnoseRuntimeInfo &other);
  int add_wait_event(const ObDiagnoseRuntimeInfo &other);
  int add_stat_event(const ObDiagnoseRuntimeInfo &other);
  int add_latch_stat(const ObDiagnoseRuntimeInfo &other);
  void reset();
  int inc_stat(const int16_t stat_no);
  int update_stat(const int16_t stat_no, const int64_t delta);
  int set_stat(const int16_t stat_no, const int64_t value);
  int get_stat(const int16_t stat_no, int64_t &value) const;
  int record_wait_event(const int64_t event_no, const int64_t wait_time,
      const uint64_t timeout_ms);
  int merge_wait_event(const int64_t event_no, const ObWaitEventStat &other);
  inline ObWaitEventStatArray &get_event_stats() { return event_stats_; }
  inline ObStatEventAddStatArray &get_add_stat_stats() { return stat_add_stats_; }
  inline ObStatEventSetStatArray &get_set_stat_stats() { return stat_set_stats_; }
  inline ObLatchStatArray &get_latch_stats() { return latch_stats_; }
  TO_STRING_EMPTY();
private:
  ObWaitEventStatArray event_stats_;
  ObStatEventAddStatArray stat_add_stats_;
  ObStatEventSetStatArray stat_set_stats_;
  ObLatchStatArray latch_stats_;
};

class ObDIGlobalRuntimeCache
{
public:
  static ObDIGlobalRuntimeCache &get_instance();
  int update_stat(const int16_t stat_no, const int64_t delta);
  int set_stat(const int16_t stat_no, const int64_t value);
  int record_wait_event(const int64_t event_no, const int64_t wait_time,
      const uint64_t timeout_ms);
  int merge_wait_event(const int64_t event_no, const ObWaitEventStat &other);
  int get_runtime_info(ObDiagnoseRuntimeInfo &runtime_info) const;
  void reset();
private:
  ObDIGlobalRuntimeCache() = default;
  ~ObDIGlobalRuntimeCache() = default;
  DISALLOW_COPY_AND_ASSIGN(ObDIGlobalRuntimeCache);
private:
  ObDiagnoseRuntimeInfo runtime_info_;
};

class ObWaitEventGuard
{
public:
  explicit ObWaitEventGuard(
      const int64_t event_no,
      const uint64_t timeout_ms = 0,
      const int64_t p1 = 0,
      const int64_t p2 = 0,
      const int64_t p3 = 0,
      const bool is_atomic = false);
  ~ObWaitEventGuard();
private:
  int64_t event_no_;
  ObDiagnoseSessionInfo *di_;
  bool is_atomic_;
  bool need_record_;
};

template<ObWaitEventIds::ObWaitEventIdEnum EVENT_ID>
class ObSleepEventGuard : public ObWaitEventGuard
{
public:
  ObSleepEventGuard(const int64_t sleep_us, const uint64_t timeout_ms = 0)
      : ObWaitEventGuard(EVENT_ID, timeout_ms, sleep_us, 0, 0)
  {}
  ObSleepEventGuard(const int64_t sleep_us, const int64_t p1,
      const int64_t p2, const int64_t p3, const uint64_t timeout_ms = 0)
      : ObWaitEventGuard(EVENT_ID, timeout_ms, p1, p2, p3)
  {
    UNUSED(sleep_us);
  }
};

class ObMaxWaitGuard
{
public:
  explicit ObMaxWaitGuard(ObWaitEventDesc *max_wait);
  ~ObMaxWaitGuard();
private:
  ObWaitEventDesc *prev_wait_;
  ObDiagnoseSessionInfo *di_;
  bool need_record_;
  ObWaitEventDesc *max_wait_;
};

class ObTotalWaitGuard
{
public:
  explicit ObTotalWaitGuard(ObWaitEventStat *total_wait);
  ~ObTotalWaitGuard();
private:
  ObWaitEventStat *prev_wait_;
  ObDiagnoseSessionInfo *di_;
  bool need_record_;
  ObWaitEventStat *total_wait_;
};


} /* namespace common */
} /* namespace oceanbase */

#ifdef _WIN32
#define SLEEP(time)                                                                        \
  do {                                                                                     \
    oceanbase::common::ObSleepEventGuard<oceanbase::common::ObWaitEventIds::DEFAULT_SLEEP> \
        wait_guard(((int64_t)time) * 1000 * 1000);                                         \
    ::Sleep((DWORD)(time) * 1000);                                                         \
  } while (0)

#define USLEEP(time)                                                                       \
  do {                                                                                     \
    oceanbase::common::ObSleepEventGuard<oceanbase::common::ObWaitEventIds::DEFAULT_SLEEP> \
        wait_guard((int64_t)time);                                                         \
    ::Sleep((DWORD)(((time) + 999) / 1000));                                               \
  } while (0)
#else
#define SLEEP(time)                                                                        \
  do {                                                                                     \
    oceanbase::common::ObSleepEventGuard<oceanbase::common::ObWaitEventIds::DEFAULT_SLEEP> \
        wait_guard(((int64_t)time) * 1000 * 1000);                                         \
    ::sleep(time);                                                                         \
  } while (0)

#define USLEEP(time)                                                                       \
  do {                                                                                     \
    oceanbase::common::ObSleepEventGuard<oceanbase::common::ObWaitEventIds::DEFAULT_SLEEP> \
        wait_guard((int64_t)time);                                                         \
    ::usleep(time);                                                                        \
  } while (0)
#endif

#endif /* OB_DIAGNOSE_INFO_H_ */
