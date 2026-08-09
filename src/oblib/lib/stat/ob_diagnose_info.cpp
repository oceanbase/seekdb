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

#define USING_LOG_PREFIX COMMON

#include "ob_diagnose_info.h"
#include "lib/hash/ob_hashutils.h"

namespace oceanbase
{
namespace common
{
/**
 * -----------------------------------------------------------ObLatchStat------------------------------------------------------
 */
ObLatchStat::ObLatchStat()
    : gets_(0),
      misses_(0),
      sleeps_(0),
      immediate_gets_(0),
      immediate_misses_(0),
      spin_gets_(0),
      wait_time_(0)
{
}

int ObLatchStat::add(const ObLatchStat &other)
{
  int ret = OB_SUCCESS;
  gets_ += other.gets_;
  misses_ += other.misses_;
  sleeps_ += other.sleeps_;
  immediate_gets_ += other.immediate_gets_;
  immediate_misses_ += other.immediate_misses_;
  spin_gets_ += other.spin_gets_;
  wait_time_ += other.wait_time_;
  return ret;
}

void ObLatchStat::reset()
{
  gets_ = 0;
  misses_ = 0;
  sleeps_ = 0;
  immediate_gets_ = 0;
  immediate_misses_ = 0;
  spin_gets_ = 0;
  wait_time_ = 0;
}

/**
 * ----------------------------------------------------------ObLatchStatArray-----------------------------------------------------
 */
ObLatchStatArray::ObLatchStatArray(ObIAllocator *allocator)
  : allocator_(allocator), items_()
{
}

ObLatchStatArray::~ObLatchStatArray()
{
  for (int64_t i = 0; i < ObLatchIds::LATCH_END; ++i) {
    if (OB_ISNULL(items_[i])) {
    } else {
      free_item(items_[i]);
      items_[i] = NULL;
    }
  }
}

int ObLatchStatArray::add(const ObLatchStatArray &other)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < ObLatchIds::LATCH_END && OB_SUCCESS == ret; ++i) {
    if (OB_ISNULL(other.get_item(i))) continue;
    auto *item = get_or_create_item(i);
    if (OB_NOT_NULL(item)) {
      ret = item->add(*other.get_item(i));
    } else {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      break;
    }
  }
  return ret;
}

void ObLatchStatArray::reset()
{
  for (int64_t i = 0; i < ObLatchIds::LATCH_END; ++i) {
    if (OB_ISNULL(items_[i])) {
    } else {
      items_[i]->reset();
    }
  }
}

static constexpr int NODE_NUM =
    hash::NodeNumTraits<ObLatchStat, OB_MALLOC_MIDDLE_BLOCK_SIZE>::NODE_NUM;
using LatchStatAlloc = hash::SimpleAllocer<ObLatchStat, NODE_NUM>;

LatchStatAlloc &get_latch_stat_alloc()
{
  struct Wrapper
  {
    Wrapper()
    {
      instance_.set_attr(ObMemAttr("LatchStat"));
      instance_.set_leak_check(false);
    }
    LatchStatAlloc instance_;
  };
  static Wrapper w;
  return w.instance_;
}

ObLatchStat *ObLatchStatArray::create_item()
{
  ObLatchStat *stat = NULL;
  lib::ObDisableDiagnoseGuard disable_diagnose_guard;
  if (OB_ISNULL(allocator_)) {
    stat = get_latch_stat_alloc().alloc();
  } else {
    stat = OB_NEWx(ObLatchStat, allocator_);
  }
  return stat;
}

void ObLatchStatArray::free_item(ObLatchStat *stat)
{
  lib::ObDisableDiagnoseGuard disable_diagnose_guard;
  if (OB_ISNULL(allocator_)) {
    get_latch_stat_alloc().free(stat);
  } else {
    stat->~ObLatchStat();
    allocator_->free(stat);
  }
}

/**
 * -------------------------------------------------------ObWaitEventHistory-------------------------------------------------------
 */
ObWaitEventHistoryIter::ObWaitEventHistoryIter()
  : items_(NULL),
    curr_(0),
    start_pos_(0),
    item_cnt_(0)
{
}

ObWaitEventHistoryIter::~ObWaitEventHistoryIter()
{
  reset();
}

int ObWaitEventHistoryIter::init(ObWaitEventDesc *items, const int64_t start_pos, int64_t item_cnt)
{
  int ret = OB_SUCCESS;
  if (NULL == items || start_pos < 0 || item_cnt < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    items_ = items;
    start_pos_ = start_pos;
    item_cnt_ = item_cnt;
    curr_ = 0;
  }
  return ret;
}

int ObWaitEventHistoryIter::get_next(ObWaitEventDesc *&item)
{
  int ret = OB_SUCCESS;
  if (curr_ >= item_cnt_ || curr_ >= SESSION_WAIT_HISTORY_CNT) {
    ret = OB_ITER_END;
  } else {
    item = &items_[(start_pos_ - curr_ + SESSION_WAIT_HISTORY_CNT) % SESSION_WAIT_HISTORY_CNT];
    curr_++;
    if (!item->is_valid()) {
      LOG_WARN("wait event desc is invalid", K(ret), K(item->event_no_));
      if (OB_FAIL(get_next(item))) {
        if (ret != OB_ITER_END) {
          LOG_WARN("failed to get next wait event desc");
        }
      }
    }

  }
  return ret;
}

void ObWaitEventHistoryIter::reset()
{
  items_ = NULL;
  curr_ = 0;
  start_pos_ = 0;
  item_cnt_ = 0;
}

ObWaitEventHistory::ObWaitEventHistory()
  : curr_pos_(0),
    item_cnt_(0),
    nest_cnt_(0),
    current_wait_(0)
{
  memset(items_, 0, sizeof(items_));
}

ObWaitEventHistory::~ObWaitEventHistory()
{
  reset();
}

int ObWaitEventHistory::push(const int64_t event_no, const uint64_t timeout_ms,
    const uint64_t p1, const uint64_t p2, const uint64_t p3)
{
  int ret = OB_SUCCESS;
  if (event_no < 0 || event_no >= WAIT_EVENTS_TOTAL) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nest_cnt_ > 0) {
    // The compact runtime implementation records the outer wait.  This keeps
    // nested instrumentation from double-counting elapsed time.
    ret = OB_ARRAY_OUT_OF_RANGE;
  } else {
    ObWaitEventDesc &item = items_[curr_pos_];
    item.reset();
    item.event_no_ = event_no;
    item.p1_ = p1;
    item.p2_ = p2;
    item.p3_ = p3;
    item.timeout_ms_ = timeout_ms;
    item.is_phy_ = OB_WAIT_EVENTS[event_no].is_phy_;
    item.wait_begin_time_ = ObTimeUtility::current_time();
    item.level_ = 0;
    item.parent_ = 0;
    current_wait_ = curr_pos_;
    curr_pos_ = (curr_pos_ + 1) % SESSION_WAIT_HISTORY_CNT;
    item_cnt_ = std::min<int64_t>(item_cnt_ + 1, SESSION_WAIT_HISTORY_CNT);
    ++nest_cnt_;
  }
  return ret;
}

int ObWaitEventHistory::add(const ObWaitEventHistory &other)
{
  int ret = OB_SUCCESS;
  ObWaitEventHistoryIter iter;
  if (OB_FAIL(const_cast<ObWaitEventHistory &>(other).get_iter(iter))) {
  } else {
    ObWaitEventDesc *item = NULL;
    while (OB_SUCC(ret) && OB_SUCCESS == (ret = iter.get_next(item))) {
      if (OB_ISNULL(item)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const int64_t pos = curr_pos_;
        items_[pos] = *item;
        curr_pos_ = (curr_pos_ + 1) % SESSION_WAIT_HISTORY_CNT;
        item_cnt_ = std::min<int64_t>(item_cnt_ + 1, SESSION_WAIT_HISTORY_CNT);
      }
    }
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}



int ObWaitEventHistory::get_next_and_compare(int64_t &iter_1, int64_t &iter_2, int64_t &cnt, const ObWaitEventHistory &other, ObWaitEventDesc *tmp)
{
  int64_t tmp_1 = iter_1;
  int64_t tmp_2 = iter_2;
  int ret = OB_SUCCESS;
  int tmp_ret = OB_ITER_END;
  int16_t N = SESSION_WAIT_HISTORY_CNT;

  if (iter_1 < 0 || iter_2 < 0 || cnt < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for(; tmp_1 < item_cnt_; ++tmp_1) {
      if(0 == items_[(curr_pos_ - 1 - tmp_1 + N) % N].level_) {
        tmp_ret = OB_SUCCESS;
        break;
      }
    }
    if (OB_SUCCESS == tmp_ret) {
      tmp_ret = OB_ITER_END;
      for(; tmp_2 < other.item_cnt_; ++tmp_2) {
        if(0 == other.items_[(other.curr_pos_ - 1 - tmp_2 + N) % N].level_) {
          tmp_ret = OB_SUCCESS;
          break;
        }
      }
      if (OB_SUCCESS == tmp_ret) {
        if (items_[(curr_pos_ - 1 - tmp_1 + N) % N] > other.items_[(other.curr_pos_ - 1 - tmp_2 + N) % N]) {
          for (int64_t i = iter_1; i <= tmp_1 && cnt < N; i++) {
            tmp[cnt++] = items_[(curr_pos_ - 1 - i + N) % N];
          }
          iter_1 = tmp_1 + 1;
        } else {
          for (int64_t i = iter_2; i <= tmp_2 && cnt < N; i++) {
            tmp[cnt++] = other.items_[(other.curr_pos_ - 1 - i + N) % N];
          }
          iter_2 = tmp_2 + 1;
        }
      } else {
        iter_2 = other.item_cnt_;
      }
    } else {
      iter_1 = item_cnt_;
    }
  }
  return ret;
}

int ObWaitEventHistory::get_iter(ObWaitEventHistoryIter &iter)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(iter.init(items_, (curr_pos_ - 1 + SESSION_WAIT_HISTORY_CNT) % SESSION_WAIT_HISTORY_CNT, item_cnt_))) {
  }
  return ret;
}

int ObWaitEventHistory::get_last_wait(ObWaitEventDesc *&item)
{
  int ret = OB_SUCCESS;
  if (0 == item_cnt_) {
    ret = OB_ITEM_NOT_SETTED;
  } else {
    item = &items_[(curr_pos_ - 1 + SESSION_WAIT_HISTORY_CNT) % SESSION_WAIT_HISTORY_CNT];
  }
  return ret;
}


int ObWaitEventHistory::get_curr_wait(ObWaitEventDesc *&item)
{
  int ret = OB_SUCCESS;
  int16_t N = SESSION_WAIT_HISTORY_CNT;
  if (0 == item_cnt_) {
    ret = OB_ITEM_NOT_SETTED;
  } else {
    // get current waiting event or latest event
    item = &items_[(curr_pos_ - 1 + N) % N];
  }
  return ret;
}

int ObWaitEventHistory::get_accord_event(ObWaitEventDesc *&event_desc)
{
  int ret = OB_SUCCESS;
  if (0 == nest_cnt_ || 0 == item_cnt_) {
    ret = OB_ITEM_NOT_SETTED;
  } else {
    event_desc = &items_[current_wait_];
  }
  return ret;
}

int ObWaitEventHistory::calc_wait_time(ObWaitEventDesc *&event_desc)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(event_desc)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    event_desc->wait_end_time_ = ObTimeUtility::current_time();
    event_desc->wait_time_ = std::max<int64_t>(
        0, event_desc->wait_end_time_ - event_desc->wait_begin_time_);
    nest_cnt_ = 0;
  }
  return ret;
}



void ObWaitEventHistory::reset()
{
  curr_pos_ = 0;
  item_cnt_ = 0;
  nest_cnt_ = 0;
  current_wait_ = 0;
  memset(items_, 0, sizeof(items_));
}

ObDiagnoseSessionInfo::ObDiagnoseSessionInfo()
    : curr_wait_(),
      max_wait_(NULL),
      total_wait_(NULL),
      event_history_(),
      event_stats_(),
      stat_add_stats_()
{
}

ObDiagnoseSessionInfo::~ObDiagnoseSessionInfo()
{
  reset();
}

int ObDiagnoseSessionInfo::add(ObDiagnoseSessionInfo &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(event_stats_.add(other.event_stats_))) {
  } else if (OB_FAIL(event_history_.add(other.event_history_))) {
  } else if (OB_FAIL(stat_add_stats_.add(other.stat_add_stats_))) {
  }
  return ret;
}

void ObDiagnoseSessionInfo::reset()
{
  curr_wait_.reset();
  event_stats_.reset();
  event_history_.reset();
  stat_add_stats_.reset();
  max_wait_ = NULL;
  total_wait_ = NULL;
}

ObWaitEventDesc &ObDiagnoseSessionInfo::get_curr_wait()
{
  int ret = OB_SUCCESS;
  ObWaitEventDesc *event_desc = NULL;
  if (OB_FAIL(event_history_.get_curr_wait(event_desc))) {
    event_desc = &curr_wait_;
  }
  return *event_desc;
}

int ObDiagnoseSessionInfo::notify_wait_begin(const int64_t event_no,
    const uint64_t timeout_ms, const uint64_t p1, const uint64_t p2,
    const uint64_t p3, const bool is_atomic)
{
  UNUSED(is_atomic);
  return event_history_.push(event_no, timeout_ms, p1, p2, p3);
}

int ObDiagnoseSessionInfo::notify_wait_end(ObDiagnoseRuntimeInfo *runtime_info,
    const bool is_atomic, const bool is_idle)
{
  int ret = OB_SUCCESS;
  ObWaitEventDesc *event_desc = NULL;
  UNUSED(is_idle);
  if (OB_FAIL(event_history_.get_accord_event(event_desc))) {
  } else if (OB_ISNULL(event_desc)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(event_history_.calc_wait_time(event_desc))) {
  } else {
    ObWaitEventStat *event_stat = event_stats_.get(event_desc->event_no_);
    if (OB_ISNULL(event_stat)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ++event_stat->total_waits_;
      event_stat->time_waited_ += event_desc->wait_time_;
      if (event_desc->timeout_ms_ > 0
          && event_desc->wait_time_ > static_cast<int64_t>(event_desc->timeout_ms_) * 1000) {
        ++event_stat->total_timeouts_;
      }
      event_stat->max_wait_ = std::max<uint32_t>(event_stat->max_wait_,
          static_cast<uint32_t>(std::min<int64_t>(event_desc->wait_time_, UINT32_MAX)));

      if (!is_atomic && OB_NOT_NULL(max_wait_)
          && event_desc->wait_time_ >= max_wait_->wait_time_) {
        *max_wait_ = *event_desc;
      }
      if (!is_atomic && OB_NOT_NULL(total_wait_)) {
        ++total_wait_->total_waits_;
        total_wait_->time_waited_ += event_desc->wait_time_;
      }
      if (OB_NOT_NULL(runtime_info)) {
        (void)runtime_info->record_wait_event(event_desc->event_no_,
            event_desc->wait_time_, event_desc->timeout_ms_);
      } else {
        (void)ObDIGlobalRuntimeCache::get_instance().record_wait_event(
            event_desc->event_no_, event_desc->wait_time_, event_desc->timeout_ms_);
      }

      int16_t wait_stat_no = -1;
      switch (OB_WAIT_EVENTS[event_desc->event_no_].wait_class_) {
        case ObWaitClassIds::CONCURRENCY:
          wait_stat_no = ObStatEventIds::CCWAIT_TIME;
          break;
        case ObWaitClassIds::USER_IO:
          wait_stat_no = ObStatEventIds::USER_IO_WAIT_TIME;
          break;
        case ObWaitClassIds::APPLICATION:
          wait_stat_no = ObStatEventIds::APWAIT_TIME;
          break;
        case ObWaitClassIds::SCHEDULER:
          wait_stat_no = ObStatEventIds::SCHEDULE_WAIT_TIME;
          break;
        case ObWaitClassIds::NETWORK:
          wait_stat_no = ObStatEventIds::NETWORK_WAIT_TIME;
          break;
        default:
          break;
      }
      if (wait_stat_no >= 0) {
        if (OB_STAT_EVENTS[wait_stat_no].summary_in_session_) {
          (void)update_stat(wait_stat_no, event_desc->wait_time_);
        }
        if (OB_NOT_NULL(runtime_info)) {
          (void)runtime_info->update_stat(wait_stat_no, event_desc->wait_time_);
        } else {
          (void)ObDIGlobalRuntimeCache::get_instance().update_stat(
              wait_stat_no, event_desc->wait_time_);
        }
      }
    }
  }
  return ret;
}

int ObDiagnoseSessionInfo::inc_stat(const int16_t stat_no)
{
  return update_stat(stat_no, 1);
}

int ObDiagnoseSessionInfo::update_stat(const int16_t stat_no, const int64_t delta)
{
  int ret = OB_SUCCESS;
  ObStatEventAddStat *stat = stat_add_stats_.get(stat_no);
  if (OB_ISNULL(stat)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    stat->stat_value_ += delta;
  }
  return ret;
}

ObDiagnoseSessionInfo *ObDiagnoseSessionInfo::get_local_diagnose_info()
{
  ObDiagnoseSessionInfo *info = NULL;
  if (lib::is_diagnose_info_enabled()) {
    static thread_local ObDiagnoseSessionInfo local_info;
    info = &local_info;
  }
  return info;
}

ObDiagnoseRuntimeInfo::ObDiagnoseRuntimeInfo(ObIAllocator *allocator)
    : event_stats_(),
      stat_add_stats_(),
      stat_set_stats_(),
      latch_stats_(allocator)
{
}

ObDiagnoseRuntimeInfo::~ObDiagnoseRuntimeInfo()
{
  reset();
}

int ObDiagnoseRuntimeInfo::add(const ObDiagnoseRuntimeInfo &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(add_wait_event(other))) {
  } else if (OB_FAIL(add_stat_event(other))) {
  } else if (OB_FAIL(add_latch_stat(other))) {
  }
  return ret;
}

int ObDiagnoseRuntimeInfo::add_wait_event(const ObDiagnoseRuntimeInfo &other)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < WAIT_EVENTS_TOTAL; ++i) {
    ObWaitEventStat *target = event_stats_.get(i);
    ObWaitEventStat *source = const_cast<ObDiagnoseRuntimeInfo &>(other).event_stats_.get(i);
    if (OB_ISNULL(target) || OB_ISNULL(source)) {
      ret = OB_ERR_UNEXPECTED;
      break;
    } else {
      target->total_timeouts_ += ATOMIC_LOAD(&source->total_timeouts_);
      target->max_wait_ = std::max(target->max_wait_, ATOMIC_LOAD(&source->max_wait_));
      target->total_waits_ += ATOMIC_LOAD(&source->total_waits_);
      target->time_waited_ += ATOMIC_LOAD(&source->time_waited_);
    }
  }
  return ret;
}

int ObDiagnoseRuntimeInfo::add_stat_event(const ObDiagnoseRuntimeInfo &other)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < ObStatEventIds::STAT_EVENT_ADD_END; ++i) {
    ObStatEventAddStat *target = stat_add_stats_.get(i);
    ObStatEventAddStat *source = const_cast<ObDiagnoseRuntimeInfo &>(other).stat_add_stats_.get(i);
    if (OB_ISNULL(target) || OB_ISNULL(source)) {
      ret = OB_ERR_UNEXPECTED;
      break;
    } else {
      target->stat_value_ += ATOMIC_LOAD(&source->stat_value_);
    }
  }
  for (int64_t i = 0; OB_SUCC(ret)
      && i < ObStatEventIds::STAT_EVENT_SET_END - ObStatEventIds::STAT_EVENT_ADD_END - 1; ++i) {
    ObStatEventSetStat *target = stat_set_stats_.get(i);
    ObStatEventSetStat *source = const_cast<ObDiagnoseRuntimeInfo &>(other).stat_set_stats_.get(i);
    if (OB_ISNULL(target) || OB_ISNULL(source)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      target->stat_value_ = ATOMIC_LOAD(&source->stat_value_);
    }
  }
  return ret;
}

int ObDiagnoseRuntimeInfo::add_latch_stat(const ObDiagnoseRuntimeInfo &other)
{
  return latch_stats_.add(other.latch_stats_);
}

void ObDiagnoseRuntimeInfo::reset()
{
  event_stats_.reset();
  stat_add_stats_.reset();
  stat_set_stats_.reset();
  latch_stats_.reset();
}

int ObDiagnoseRuntimeInfo::inc_stat(const int16_t stat_no)
{
  return update_stat(stat_no, 1);
}

int ObDiagnoseRuntimeInfo::update_stat(const int16_t stat_no, const int64_t delta)
{
  int ret = OB_SUCCESS;
  ObStatEventAddStat *stat = stat_add_stats_.get(stat_no);
  if (OB_ISNULL(stat)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    (void)stat->atomic_add(delta);
  }
  return ret;
}

int ObDiagnoseRuntimeInfo::set_stat(const int16_t stat_no, const int64_t value)
{
  int ret = OB_SUCCESS;
  ObStatEventSetStat *stat = stat_set_stats_.get(
      stat_no - ObStatEventIds::STAT_EVENT_ADD_END - 1);
  if (OB_ISNULL(stat)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ATOMIC_STORE(&stat->stat_value_, value);
  }
  return ret;
}

int ObDiagnoseRuntimeInfo::get_stat(const int16_t stat_no, int64_t &value) const
{
  int ret = OB_SUCCESS;
  if (stat_no < 0 || ObStatEventIds::STAT_EVENT_ADD_END == stat_no
      || stat_no >= ObStatEventIds::STAT_EVENT_SET_END) {
    ret = OB_INVALID_ARGUMENT;
  } else if (stat_no < ObStatEventIds::STAT_EVENT_ADD_END) {
    ObStatEventAddStat *stat = const_cast<ObDiagnoseRuntimeInfo *>(this)->stat_add_stats_.get(stat_no);
    if (OB_ISNULL(stat)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      value = ATOMIC_LOAD(&stat->stat_value_);
    }
  } else {
    ObStatEventSetStat *stat = const_cast<ObDiagnoseRuntimeInfo *>(this)->stat_set_stats_.get(
        stat_no - ObStatEventIds::STAT_EVENT_ADD_END - 1);
    if (OB_ISNULL(stat)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      value = ATOMIC_LOAD(&stat->stat_value_);
    }
  }
  return ret;
}

int ObDiagnoseRuntimeInfo::record_wait_event(const int64_t event_no,
    const int64_t wait_time, const uint64_t timeout_ms)
{
  int ret = OB_SUCCESS;
  if (wait_time < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObWaitEventStat delta;
    delta.total_waits_ = 1;
    delta.time_waited_ = wait_time;
    delta.max_wait_ = static_cast<uint32_t>(std::min<int64_t>(wait_time, UINT32_MAX));
    if (timeout_ms > 0 && wait_time > static_cast<int64_t>(timeout_ms) * 1000) {
      delta.total_timeouts_ = 1;
    }
    ret = merge_wait_event(event_no, delta);
  }
  return ret;
}

int ObDiagnoseRuntimeInfo::merge_wait_event(const int64_t event_no,
    const ObWaitEventStat &other)
{
  int ret = OB_SUCCESS;
  ObWaitEventStat *stat = event_stats_.get(event_no);
  if (OB_ISNULL(stat)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    (void)ATOMIC_AAF(&stat->total_waits_, other.total_waits_);
    (void)ATOMIC_AAF(&stat->time_waited_, other.time_waited_);
    (void)ATOMIC_AAF(&stat->total_timeouts_, other.total_timeouts_);
    uint32_t max_wait = ATOMIC_LOAD(&stat->max_wait_);
    while (other.max_wait_ > max_wait
        && !ATOMIC_BCAS(&stat->max_wait_, max_wait, other.max_wait_)) {
      max_wait = ATOMIC_LOAD(&stat->max_wait_);
    }
  }
  return ret;
}

ObDIGlobalRuntimeCache &ObDIGlobalRuntimeCache::get_instance()
{
  static ObDIGlobalRuntimeCache instance;
  return instance;
}

int ObDIGlobalRuntimeCache::update_stat(const int16_t stat_no, const int64_t delta)
{
  return runtime_info_.update_stat(stat_no, delta);
}

int ObDIGlobalRuntimeCache::set_stat(const int16_t stat_no, const int64_t value)
{
  return runtime_info_.set_stat(stat_no, value);
}

int ObDIGlobalRuntimeCache::record_wait_event(const int64_t event_no,
    const int64_t wait_time, const uint64_t timeout_ms)
{
  return runtime_info_.record_wait_event(event_no, wait_time, timeout_ms);
}

int ObDIGlobalRuntimeCache::merge_wait_event(const int64_t event_no,
    const ObWaitEventStat &other)
{
  return runtime_info_.merge_wait_event(event_no, other);
}

int ObDIGlobalRuntimeCache::get_runtime_info(ObDiagnoseRuntimeInfo &runtime_info) const
{
  runtime_info.reset();
  return runtime_info.add(runtime_info_);
}

void ObDIGlobalRuntimeCache::reset()
{
  runtime_info_.reset();
}

ObWaitEventGuard::ObWaitEventGuard(const int64_t event_no,
    const uint64_t timeout_ms, const int64_t p1, const int64_t p2,
    const int64_t p3, const bool is_atomic)
    : event_no_(event_no),
      di_(NULL),
      is_atomic_(is_atomic),
      need_record_(false)
{
  if (lib::is_diagnose_info_enabled()
      && event_no >= 0 && event_no < WAIT_EVENTS_TOTAL
      && OB_NOT_NULL(di_ = ObDiagnoseSessionInfo::get_local_diagnose_info())) {
    need_record_ = OB_SUCCESS == di_->notify_wait_begin(
        event_no, timeout_ms, p1, p2, p3, is_atomic);
  }
}

ObWaitEventGuard::~ObWaitEventGuard()
{
  if (need_record_ && OB_NOT_NULL(di_)) {
    (void)di_->notify_wait_end(NULL, is_atomic_,
        OB_WAIT_EVENTS[event_no_].wait_class_ == ObWaitClassIds::IDLE);
  }
}

ObMaxWaitGuard::ObMaxWaitGuard(ObWaitEventDesc *max_wait)
    : prev_wait_(NULL),
      di_(NULL),
      need_record_(false),
      max_wait_(max_wait)
{
  if (lib::is_diagnose_info_enabled() && OB_NOT_NULL(max_wait_)
      && OB_NOT_NULL(di_ = ObDiagnoseSessionInfo::get_local_diagnose_info())) {
    max_wait_->reset();
    prev_wait_ = di_->get_max_wait();
    (void)di_->set_max_wait(max_wait_);
    need_record_ = true;
  }
}

ObMaxWaitGuard::~ObMaxWaitGuard()
{
  if (need_record_ && OB_NOT_NULL(di_)) {
    if (OB_NOT_NULL(prev_wait_)) {
      if (max_wait_->wait_time_ > prev_wait_->wait_time_) {
        *prev_wait_ = *max_wait_;
      }
      (void)di_->set_max_wait(prev_wait_);
    } else {
      di_->reset_max_wait();
    }
  }
}

ObTotalWaitGuard::ObTotalWaitGuard(ObWaitEventStat *total_wait)
    : prev_wait_(NULL),
      di_(NULL),
      need_record_(false),
      total_wait_(total_wait)
{
  if (lib::is_diagnose_info_enabled() && OB_NOT_NULL(total_wait_)
      && OB_NOT_NULL(di_ = ObDiagnoseSessionInfo::get_local_diagnose_info())) {
    total_wait_->reset();
    prev_wait_ = di_->get_total_wait();
    (void)di_->set_total_wait(total_wait_);
    need_record_ = true;
  }
}

ObTotalWaitGuard::~ObTotalWaitGuard()
{
  if (need_record_ && OB_NOT_NULL(di_)) {
    if (OB_NOT_NULL(prev_wait_)) {
      prev_wait_->total_waits_ += total_wait_->total_waits_;
      prev_wait_->time_waited_ += total_wait_->time_waited_;
      (void)di_->set_total_wait(prev_wait_);
    } else {
      di_->reset_total_wait();
    }
  }
}




} /* namespace common */
} /* namespace oceanbase */
