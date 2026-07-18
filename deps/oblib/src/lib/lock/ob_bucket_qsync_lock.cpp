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

#include "lib/lock/ob_bucket_qsync_lock.h"
#include "lib/lock/ob_qsync_lock.h"

namespace oceanbase
{
namespace common 
{
ObBucketQSyncLock::ObBucketQSyncLock()
    : bucket_cnt_(0),
      locks_(NULL),
      is_inited_(false)
{
}

ObBucketQSyncLock::~ObBucketQSyncLock()
{
  destroy();
}


void ObBucketQSyncLock::destroy()
{
  is_inited_ = false;
  if (nullptr != locks_) {
    for (uint64_t i = 0; i < bucket_cnt_; ++i) {
      locks_[i].~ObQSyncLock();
    }
    ob_free(locks_);
    locks_ = NULL;
  }
  bucket_cnt_ = 0;
}

int ObBucketQSyncLock::rdlock(const uint64_t bucket_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else if(OB_UNLIKELY(bucket_idx >= bucket_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "Invalid argument, ", K(bucket_idx), K_(bucket_cnt), K(ret));
  } else if (OB_FAIL(locks_[bucket_idx].rdlock())) {
    SHARE_LOG(WARN, "Fail to read lock latch, ", K(bucket_idx), K(ret));
  }
  return ret;
}

int ObBucketQSyncLock::wrlock(const uint64_t bucket_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else if(OB_UNLIKELY(bucket_idx >= bucket_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "Invalid argument, ", K(bucket_idx), K_(bucket_cnt), K(ret));
  } else if (OB_FAIL(locks_[bucket_idx].wrlock())) {
    SHARE_LOG(WARN, "Fail to write lock latch, ", K(bucket_idx), K(ret));
  }
  return ret;
}

int ObBucketQSyncLock::rdunlock(const uint64_t bucket_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else if(OB_UNLIKELY(bucket_idx >= bucket_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "Invalid argument, ", K(bucket_idx), K_(bucket_cnt), K(ret));
  } else {
    locks_[bucket_idx].rdunlock();
  }
  return ret;
}

int ObBucketQSyncLock::wrunlock(const uint64_t bucket_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else if(OB_UNLIKELY(bucket_idx >= bucket_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "Invalid argument, ", K(bucket_idx), K_(bucket_cnt), K(ret));
  } else {
    locks_[bucket_idx].wrunlock();
  }
  return ret;
}

int ObBucketQSyncLock::wrlock_all()
{
  int ret = OB_SUCCESS;
  int64_t last_succ_idx = -1;
  const int64_t start_ts = ObClockGenerator::getClock();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < bucket_cnt_; ++i) {
      if (OB_SUCC(locks_[i].wrlock())) {
        last_succ_idx = i;
      } else {
        if(OB_EAGAIN != ret) {
          SHARE_LOG(WARN, "failed to try wrlock", K(ret), K(i), K(bucket_cnt_));
        }
      }
    }

    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i <= last_succ_idx; ++i) {
        locks_[i].wrunlock();
      }
    }
  }

  const int64_t cost_ts = ObClockGenerator::getClock() - start_ts;
  SHARE_LOG(DEBUG, "wrlock all", K(bucket_cnt_), K(cost_ts), K(ret));
  return ret;
}

int ObBucketQSyncLock::wrunlock_all()
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = ObClockGenerator::getClock();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < bucket_cnt_; ++i) {
      locks_[i].wrunlock();
    }
  }
  const int64_t cost_ts = ObClockGenerator::getClock() - start_ts;
  SHARE_LOG(DEBUG, "unlock all", K(bucket_cnt_), K(cost_ts), K(ret));
  return ret;
}

int ObBucketQSyncLock::try_rdlock_all()
{
  int ret = OB_SUCCESS;
  int64_t last_succ_idx = -1;
  const int64_t start_ts = ObClockGenerator::getClock();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < bucket_cnt_; ++i) {
      ret = locks_[i].try_rdlock();
      if (OB_SUCCESS == ret) {
        last_succ_idx = i;
      } else if (OB_EAGAIN != ret) {
        SHARE_LOG(WARN, "failed to try wrlock", K(ret), K(i), K(bucket_cnt_));
      }
    }

    if (OB_FAIL(ret)) {
      for (int64_t i = 0; i <= last_succ_idx; ++i) {
        locks_[i].rdunlock();
      }
    }
  }
  const int64_t cost_ts = ObClockGenerator::getClock() - start_ts;
  SHARE_LOG(DEBUG, "try lock all", K(bucket_cnt_), K(cost_ts), K(ret));
  return ret;
}

int ObBucketQSyncLock::rdunlock_all()
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = ObClockGenerator::getClock();

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    SHARE_LOG(WARN, "ObBucketQSyncLock not inited", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < bucket_cnt_; ++i) {
      locks_[i].rdunlock();
    }
  }
  const int64_t cost_ts = ObClockGenerator::getClock() - start_ts;
  SHARE_LOG(DEBUG, "unlock all", K(bucket_cnt_), K(cost_ts), K(ret));
  return ret;
}
} //namespace common
} //namespace oceanbae
