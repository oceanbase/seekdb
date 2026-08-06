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

#define USING_LOG_PREFIX STORAGE

#include "ob_memstore_freezer_common.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{
using namespace lib;
using namespace share;
namespace storage
{
DEF_TO_STRING(ObMemstoreFreezeArg)
{
  int64_t pos = 0;
  J_KV(K_(freeze_type));
  return pos;
}

OB_SERIALIZE_MEMBER(ObMemstoreFreezeArg,
                    freeze_type_,
                    try_frozen_scn_);

ObMemstoreFreezeCtx::ObMemstoreFreezeCtx()
  : mem_memstore_limit_(0),
    memstore_freeze_trigger_(0),
    active_memstore_used_(0),
    freezable_active_memstore_used_(0),
    memstore_quota_used_(0),
    max_cached_memstore_size_(0)
{
}

void ObMemstoreFreezeCtx::reset()
{
  mem_memstore_limit_ = 0;
  memstore_freeze_trigger_ = 0;
  active_memstore_used_ = 0;
  freezable_active_memstore_used_ = 0;
  memstore_quota_used_ = 0;
  max_cached_memstore_size_ = 0;
}

ObMemstoreStatistic::ObMemstoreStatistic()
  : active_memstore_used_(0),
    memstore_quota_used_(0),
    memstore_freeze_trigger_(0),
    memstore_limit_(0),
    memory_budget_(0),
    max_cached_memstore_size_(0),
    memstore_allocated_pos_(0),
    memstore_frozen_pos_(0),
    memstore_reclaimed_pos_(0)
{}

void ObMemstoreStatistic::reset()
{
  active_memstore_used_ = 0;
  memstore_quota_used_ = 0;
  memstore_freeze_trigger_ = 0;
  memstore_limit_ = 0;
  memory_budget_ = 0;
  max_cached_memstore_size_ = 0;
  memstore_allocated_pos_ = 0;
  memstore_frozen_pos_ = 0;
  memstore_reclaimed_pos_ = 0;
}

ObMemstoreInfo::ObMemstoreInfo()
  :	is_loaded_(false),
    frozen_scn_(0),
    freeze_cnt_(0),
    slow_freeze_(false),
    slow_freeze_timestamp_(0),
    slow_freeze_mt_retire_clock_(0),
    freeze_interval_(0),
    last_freeze_timestamp_(0),
    mem_lower_limit_(0),
    mem_upper_limit_(0),
    mem_memstore_limit_(0)
{
}

void ObMemstoreInfo::reset()
{
// i64 max as invalid.
  is_loaded_ = false;
  frozen_scn_ = 0;
  freeze_cnt_ = 0;
  slow_freeze_ = false;
  slow_freeze_timestamp_ = 0;
  slow_freeze_mt_retire_clock_ = 0;
  freeze_interval_ = 0;
  last_freeze_timestamp_ = 0;
  slow_tablet_.reset();
  mem_memstore_limit_ = 0;
  mem_lower_limit_ = 0;
  mem_upper_limit_ = 0;
}

int ObMemstoreInfo::update_frozen_scn(int64_t frozen_scn)
{
  int ret = OB_SUCCESS;

  if (frozen_scn > frozen_scn_) {
    frozen_scn_ = frozen_scn;
    freeze_cnt_ = 0;
  }

  return ret;
}

void ObMemstoreInfo::get_mem_limit(int64_t &lower_limit, int64_t &upper_limit) const
{
  SpinRLockGuard guard(lock_);
  lower_limit = mem_lower_limit_;
  upper_limit = mem_upper_limit_;
}

void ObMemstoreInfo::update_mem_limit(const int64_t lower_limit,
                                    const int64_t upper_limit)
{
  SpinWLockGuard guard(lock_);
  mem_lower_limit_ = lower_limit;
  mem_upper_limit_ = upper_limit;
}

void ObMemstoreInfo::update_memstore_limit(const int64_t memstore_limit)
{
  SpinWLockGuard guard(lock_);
  mem_memstore_limit_ = memstore_limit;
}

int64_t ObMemstoreInfo::get_memstore_limit() const
{
  SpinRLockGuard guard(lock_);
  return mem_memstore_limit_;
}

bool ObMemstoreInfo::is_memstore_limit_changed(const int64_t curr_memstore_limit) const
{
  SpinRLockGuard guard(lock_);
  return curr_memstore_limit != mem_memstore_limit_;
}

void ObMemstoreInfo::get_freeze_ctx(ObMemstoreFreezeCtx &ctx) const
{
  SpinRLockGuard guard(lock_);
  ctx.mem_memstore_limit_ = mem_memstore_limit_;
}

bool ObMemstoreInfo::is_freeze_need_slow() const
{
  bool need_slow = false;
  SpinRLockGuard guard(lock_);
  if (slow_freeze_) {
    int64_t now = ObTimeUtility::fast_current_time();
    if (now - last_freeze_timestamp_ >= freeze_interval_) {
      need_slow = false;
    } else {
      // no need minor freeze
      need_slow = true;
    }
  }
  return need_slow;
}

void ObMemstoreInfo::update_slow_freeze_interval()
{
  if (!slow_freeze_) {
  } else {
    SpinWLockGuard guard(lock_);
    // if slow freeze, make freeze interval 2 times of now.
    if (slow_freeze_) {
      last_freeze_timestamp_ = ObTimeUtility::fast_current_time();
      freeze_interval_ = MIN(freeze_interval_ * 2, MAX_FREEZE_INTERVAL);
    }
  }
}

void ObMemstoreInfo::set_slow_freeze(
    const common::ObTabletID &tablet_id,
    const int64_t retire_clock,
    const int64_t default_interval)
{
  SpinWLockGuard guard(lock_);
  if (!slow_freeze_) {
    slow_freeze_ = true;
    slow_freeze_timestamp_ = ObTimeUtility::fast_current_time();
    slow_freeze_mt_retire_clock_ = retire_clock;
    slow_tablet_ = tablet_id;
    last_freeze_timestamp_ = ObTimeUtility::fast_current_time();
    freeze_interval_ = default_interval;
  }
}

void ObMemstoreInfo::unset_slow_freeze(const common::ObTabletID &tablet_id)
{
  SpinWLockGuard guard(lock_);
  if (slow_freeze_ && slow_tablet_ == tablet_id) {
    slow_freeze_ = false;
    slow_freeze_timestamp_ = 0;
    slow_freeze_mt_retire_clock_ = 0;
    last_freeze_timestamp_ = 0;
    freeze_interval_ = 0;
    slow_tablet_.reset();
  }
}

ObMemstoreFreezeGuard::ObMemstoreFreezeGuard(int &err_code, const ObMemstoreInfo &memstore_info, const int64_t warn_threshold)
    : memstore_info_(memstore_info),
      pre_retire_pos_(0),
      error_code_(err_code),
      time_guard_("FREEZE_CHECKER", warn_threshold)
{
  ObMemstoreAllocator &memstore_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();
  pre_retire_pos_ = memstore_allocator.get_retire_clock();
}

ObMemstoreFreezeGuard::~ObMemstoreFreezeGuard()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(error_code_)) {
    LOG_WARN("[FREEZE_CHECKER]global freeze failed, skip check frozen memstore", KR(error_code_));
  } else {
    ObMemstoreAllocator &memstore_allocator = share::g_mp->shared_mem_alloc_mgr()->memstore_allocator();
    int64_t curr_frozen_pos = 0;
    curr_frozen_pos = memstore_allocator.get_frozen_memstore_pos();
    const bool retired_mem_frozen = (curr_frozen_pos >= pre_retire_pos_);
    const bool has_no_active_memtable = (curr_frozen_pos == 0);
    if (!(retired_mem_frozen || has_no_active_memtable)) {
      ret = OB_ERR_UNEXPECTED;
      if (memstore_info_.is_freeze_slowed()) {
        LOG_WARN("[FREEZE_CHECKER]there may be frequent global freeze, but slowed",
                 KR(ret),
                 K(curr_frozen_pos),
                 K_(pre_retire_pos),
                 K(retired_mem_frozen),
                 K(has_no_active_memtable),
                 K_(memstore_info));
      } else {
        LOG_ERROR("[FREEZE_CHECKER]there may be frequent global freeze",
                  KR(ret),
                  K(curr_frozen_pos),
                  K_(pre_retire_pos),
                  K(retired_mem_frozen),
                  K(has_no_active_memtable));
      }
      char active_mt_info[DEFAULT_BUF_LENGTH];
      memstore_allocator.log_active_memstore_info(active_mt_info, sizeof(active_mt_info));
      FLOG_INFO("[FREEZE_CHECKER] oldest active memtable", "list", active_mt_info);
    }
  }
}

} // storage
} // oceanbase
