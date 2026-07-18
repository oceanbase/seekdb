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
      instance_.set_attr(SET_USE_500("LatchStat"));
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




} /* namespace common */
} /* namespace oceanbase */
