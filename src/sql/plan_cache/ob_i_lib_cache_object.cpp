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

#define USING_LOG_PREFIX SQL_PC
#include "ob_i_lib_cache_object.h"
#include "lib/time/ob_time_utility.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{
namespace
{
int64_t min_wall_clock_offset = INT64_MAX;
} // namespace

ObILibCacheObject::ObILibCacheObject(ObLibCacheNameSpace ns, lib::MemoryContext &mem_context)
  : mem_context_(mem_context),
    allocator_(mem_context->get_safe_arena_allocator()),
    ref_count_(0),
    object_id_(OB_INVALID_ID),
    log_del_time_(INT64_MAX),
    added_to_lc_(false),
    ns_(ns),
    obj_status_(ObILibCacheObject::ACTIVE),
    accounted_size_(0),
    plan_cache_(nullptr)
{
}

void ObILibCacheObject::reset()
{
  ref_count_ = 0;
  object_id_ = OB_INVALID_ID;
  ATOMIC_STORE(&log_del_time_, INT64_MAX);
  added_to_lc_ = false;
  ns_ = NS_INVALID;
  obj_status_ = ObILibCacheObject::ACTIVE;
  accounted_size_ = 0;
  plan_cache_ = nullptr;
}

void ObILibCacheObject::mark_logically_deleted()
{
  const int64_t wall_time = ObTimeUtility::current_time();
  const int64_t monotonic_time = ObTimeUtility::current_monotonic_time();
  int64_t offset = 0;
  if (__builtin_sub_overflow(wall_time, monotonic_time, &offset)) {
    offset = INT64_MIN;  // Disable forced cleanup if the bound cannot be represented.
  }
  // Keep a lower bound on wall time minus monotonic time for every recorded deletion.
  int64_t previous = ATOMIC_LOAD(&min_wall_clock_offset);
  while (offset < previous && !ATOMIC_BCAS(&min_wall_clock_offset, previous, offset)) {
    previous = ATOMIC_LOAD(&min_wall_clock_offset);
  }
  // Publish ERASE_TIME only after the shared bound covers this deletion.
  ATOMIC_STORE(&log_del_time_, wall_time);
}

bool ObILibCacheObject::should_release(const int64_t safe_monotonic_time) const
{
  const int64_t wall_time = get_logical_del_time();
  const int64_t offset = ATOMIC_LOAD(&min_wall_clock_offset);
  int64_t safe_wall_time = 0;
  // wall_time < safe_monotonic_time + offset implies that the deletion
  // is older than safe_monotonic_time. A smaller offset can defer cleanup.
  return 0 != get_ref_count() && INT64_MAX != wall_time
      && INT64_MIN != offset && INT64_MAX != offset
      && !__builtin_add_overflow(safe_monotonic_time, offset, &safe_wall_time)
      && wall_time < safe_wall_time;
}

void ObILibCacheObject::dump_deleted_log_info(const bool is_debug_log /* = true */) const
{
  if (is_debug_log) {
    SQL_PC_LOG_RET(WARN, OB_SUCCESS, "Dumping Cache Deleted Info",
               K(object_id_),
               K(added_to_lc_),
               K(ns_),
               K(get_ref_count()),
               "log_del_time_", get_logical_del_time(),
               K(this));
  } else {
    SQL_PC_LOG(INFO, "Dumping Cache Deleted Info",
               K(object_id_),
               K(added_to_lc_),
               K(ns_),
               K(get_ref_count()),
               "log_del_time_", get_logical_del_time(),
               K(this));
  }
}

int ObILibCacheObject::before_cache_evicted()
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("before_cache_evicted", K(this), KPC(this));
  return ret;
}

int ObILibCacheObject::check_need_add_cache_obj_stat(ObILibCacheCtx &ctx, bool &need_real_add)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  need_real_add = true;
  return ret;
}

int ObILibCacheObject::update_cache_obj_stat(ObILibCacheCtx &ctx)
{
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  return ret;
}

int64_t ObILibCacheObject::inc_ref_count()
{
  return ATOMIC_AAF(&ref_count_, 1);
}

bool ObILibCacheObject::try_inc_ref_count()
{
  int64_t ref_cnt = ATOMIC_LOAD(&ref_count_);
  while (ref_cnt > 0 && !ATOMIC_BCAS(&ref_count_, ref_cnt, ref_cnt + 1)) {
    ref_cnt = ATOMIC_LOAD(&ref_count_);
  }
  return ref_cnt > 0;
}

int64_t ObILibCacheObject::dec_ref_count()
{
  return ATOMIC_SAF(&ref_count_, 1);
}

} // namespace common
} // namespace oceanbase
