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
#include "ob_table_store_stat_mgr.h"
#include "share/ob_thread_mgr.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
// ------------------ Statistic ------------------ //
bool ObMergeIterStat::is_valid() const
{
  return call_cnt_ >= 0 && output_row_cnt_ >= 0;
}

int ObMergeIterStat::add(const ObMergeIterStat& other)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("self is invalid", K(ret), K(*this));
  } else if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("other is invalid", K(ret), K(other));
  } else {
    call_cnt_ += other.call_cnt_;
    output_row_cnt_ += other.output_row_cnt_;
  }
  return ret;
}


bool ObBlockAccessStat::is_valid() const
{
  return effect_read_cnt_ >= 0 && empty_read_cnt_ >= 0;
}

int ObBlockAccessStat::add(const ObBlockAccessStat& other)
{
  int ret = OB_SUCCESS;
  if (!is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("self is invalid", K(ret), K(*this));
  } else if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("other is invalid", K(ret), K(other));
  } else {
    effect_read_cnt_ += other.effect_read_cnt_;
    empty_read_cnt_ += other.empty_read_cnt_;
  }
  return ret;
}


ObTableStoreStat::ObTableStoreStat()
{
  reset();
}

void ObTableStoreStat::reset()
{
  MEMSET(this, 0, sizeof(ObTableStoreStat));
}


bool ObTableStoreStat::is_valid() const
{
  bool valid = true;
  if (row_cache_hit_cnt_ < 0 || row_cache_miss_cnt_ < 0 || row_cache_put_cnt_ < 0
      || bf_filter_cnt_ < 0 || bf_empty_read_cnt_ < 0 || bf_access_cnt_ < 0
      || block_cache_hit_cnt_ < 0 || block_cache_miss_cnt_ < 0
      || access_row_cnt_ < 0 || output_row_cnt_ < 0 || fuse_row_cache_hit_cnt_ < 0
      || fuse_row_cache_miss_cnt_ < 0 || fuse_row_cache_put_cnt_ < 0
      || macro_access_cnt_ < 0 || micro_access_cnt_ < 0 || pushdown_micro_access_cnt_ < 0
      || pushdown_row_access_cnt_ < 0 || pushdown_row_select_cnt_ < 0
      || !single_get_stat_.is_valid() || !multi_get_stat_.is_valid() || !index_back_stat_.is_valid()
      || !single_scan_stat_.is_valid() || !multi_scan_stat_.is_valid()
      || !exist_row_.is_valid() ||!get_row_.is_valid() || !scan_row_.is_valid()
      || logical_read_cnt_ < 0 || physical_read_cnt_ < 0) {
    valid = false;
  }
  return valid;
}



// ------------------ Iterator ------------------ //
ObTableStoreStatIterator::ObTableStoreStatIterator()
  : cur_idx_(0),
    is_opened_(false)
{
}

ObTableStoreStatIterator::~ObTableStoreStatIterator()
{
}

void ObTableStoreStatIterator::reset()
{
  cur_idx_ = 0;
  is_opened_ = false;
}

int ObTableStoreStatIterator::open()
{
  int ret = OB_SUCCESS;
  if (is_opened_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableStoreStatIterator has been opened", K(ret));
  } else {
    cur_idx_ = 0;
    is_opened_ = true;
  }
  return ret;
}

int ObTableStoreStatIterator::get_next_stat(ObTableStoreStat &stat)
{
  return OB_ITER_END;
}

} // namespace oceanbase
} // namespace storage
