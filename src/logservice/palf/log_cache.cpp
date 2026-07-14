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

#define USING_LOG_PREFIX PALF
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "log_cache.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace palf
{
LogHotCache::LogHotCache()
  : palf_id_(INVALID_PALF_ID),
    palf_handle_impl_(NULL),
    read_size_(0),
    hit_count_(0),
    read_count_(0),
    last_print_time_(0),
    is_inited_(false)
{}

LogHotCache::~LogHotCache()
{
  destroy();
}

void LogHotCache::destroy()
{
  reset();
}

void LogHotCache::reset()
{
  is_inited_ = false;
  palf_handle_impl_ = NULL;
  palf_id_ = INVALID_PALF_ID;
}

int LogHotCache::init(const int64_t palf_id, IPalfHandleImpl *palf_handle_impl)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (false == is_valid_palf_id(palf_id) || OB_ISNULL(palf_handle_impl)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    palf_id_ = palf_id;
    palf_handle_impl_ = palf_handle_impl;
    is_inited_ = true;
    PALF_LOG(TRACE, "init hot cache successfully", K(palf_id_));
  }
  return ret;
}

int LogHotCache::read(const LSN &read_begin_lsn,
                      const int64_t in_read_size,
                      char *buf,
                      int64_t &out_read_size) const
{
  int ret = OB_SUCCESS;
  int64_t read_size = 0, hit_cnt = 0, read_cnt = 0;
  out_read_size = 0;
  int64_t start_ts = ObTimeUtility::fast_current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(WARN, "hot cache is not inited", K(ret), K(palf_id_));
  } else if (!read_begin_lsn.is_valid() || in_read_size <= 0 || OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments", K(ret), K_(palf_id), K(read_begin_lsn), K(in_read_size),
        KP(buf));
  } else if (OB_FAIL(palf_handle_impl_->read_data_from_buffer(read_begin_lsn, in_read_size, \
          buf, out_read_size))) {
    if (OB_ERR_OUT_OF_LOWER_BOUND != ret) {
      PALF_LOG(WARN, "read_data_from_buffer failed", K(ret), K_(palf_id), K(read_begin_lsn),
          K(in_read_size));
    }
  } else {
    int64_t cost_ts = ObTimeUtility::fast_current_time() - start_ts;
    hit_cnt = ATOMIC_AAF(&hit_count_, 1);
    read_size = ATOMIC_AAF(&read_size_, out_read_size);
    EVENT_TENANT_INC(ObStatEventIds::PALF_READ_COUNT_FROM_HOT_CACHE);
    EVENT_ADD(ObStatEventIds::PALF_READ_SIZE_FROM_HOT_CACHE, out_read_size);
    EVENT_ADD(ObStatEventIds::PALF_READ_TIME_FROM_HOT_CACHE, cost_ts);
    PALF_LOG(TRACE, "read_data_from_buffer success", K(ret), K_(palf_id), K(read_begin_lsn),
        K(in_read_size), K(out_read_size));
  }
  read_cnt = ATOMIC_AAF(&read_count_, 1);
  if (palf_reach_time_interval(PALF_STAT_PRINT_INTERVAL_US, last_print_time_)) {
    read_cnt = read_cnt == 0 ? 1 : read_cnt;
    PALF_LOG(INFO, "[PALF STAT HOT CACHE HIT RATE]", K_(palf_id), K(read_size), K(hit_cnt), K(read_cnt), "hit rate", hit_cnt * 1.0 / read_cnt);
    hit_count_ = 0;
    read_size_ = 0;
    read_count_ = 0;
  }
  return ret;
}

// =======================================LogCache=======================================
LogCache::LogCache() : hot_cache_(), is_inited_(false) {}

LogCache::~LogCache() 
{
  destroy();
}

void LogCache::destroy()
{
  palf_id_ = INVALID_PALF_ID;
  hot_cache_.destroy();
  is_inited_ = false;
}

int LogCache::init(const int64_t palf_id,
                   IPalfHandleImpl *palf_handle_impl)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    PALF_LOG(WARN, "LogCache init failed", K(ret));
  } else if (OB_FAIL(hot_cache_.init(palf_id, palf_handle_impl))){
    PALF_LOG(WARN, "hot cache init failed", K(ret), K(palf_id));
  } else {
    palf_id_ = palf_id;
    is_inited_ = true;
    PALF_LOG(INFO, "LogCache init successfully", K(palf_id));
  }

  return ret;
}

bool LogCache::is_inited() const
{
  return is_inited_;
}

int LogCache::read(const LSN &lsn,
                   const int64_t in_read_size,
                   ReadBuf &read_buf,
                   int64_t &out_read_size,
                   LogIOContext &io_ctx)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    PALF_LOG(ERROR, "LogCache is not inited!", K(ret));
  } else if (!lsn.is_valid() || 0 >= in_read_size || !read_buf.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "Invalid argument!!!", K(ret), K(lsn), K(in_read_size), K(read_buf));
  } else {
    const int hot_ret = read_hot_cache_(lsn, in_read_size, read_buf.buf_, out_read_size);
    if (OB_SUCCESS == hot_ret) {
      io_ctx.inc_cache_hit_cnt();
      io_ctx.inc_cache_read_size(out_read_size);
    } else if (OB_ERR_OUT_OF_LOWER_BOUND == hot_ret || OB_READ_NOTHING == hot_ret) {
      io_ctx.inc_cache_miss_cnt();
      out_read_size = 0;
      ret = OB_ENTRY_NOT_EXIST;
      PALF_LOG(TRACE, "miss log hot cache", K(ret), K(hot_ret), K(lsn), K(in_read_size), K(read_buf), K(out_read_size));
    } else {
      ret = hot_ret;
      PALF_LOG(WARN, "fail to read log hot cache", K(ret), K(lsn), K(in_read_size), K(read_buf), K(out_read_size));
    }
  }

  return ret;
}

int LogCache::read_hot_cache_(const LSN &read_begin_lsn,
                             const int64_t in_read_size,
                             char *buf,
                             int64_t &out_read_size)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(hot_cache_.read(read_begin_lsn, in_read_size, buf, out_read_size))
      && out_read_size > 0) {
    // read data from hot_cache successfully
    PALF_LOG(TRACE, "read hot cache successfully", K(read_begin_lsn), K(in_read_size), K(out_read_size));
  } else if (OB_SUCCESS == ret && 0 == out_read_size){
    ret = OB_READ_NOTHING;
    PALF_LOG(TRACE, "read nothing from hot cache", K(ret), K(read_begin_lsn), K(in_read_size), K(out_read_size));
  } else if (OB_ERR_OUT_OF_LOWER_BOUND == ret) {
    PALF_LOG(TRACE, "miss hot cache", K(ret), K(read_begin_lsn), K(in_read_size), K(out_read_size));
  } else {
    PALF_LOG(WARN, "read hot cache failed", K(ret), K(read_begin_lsn), K(in_read_size), K(out_read_size));
  } 

  return ret;
}

} // end namespace palf
} // end namespace oceanbase
