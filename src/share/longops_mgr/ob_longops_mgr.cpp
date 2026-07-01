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

#define USING_LOG_PREFIX SHARE

#include "ob_longops_mgr.h"

namespace oceanbase
{

namespace share
{
ObLongopsMgr::ObLongopsMgr()
  : is_inited_(false), allocator_(), bucket_lock_(), map_()
{
}

ObLongopsMgr &ObLongopsMgr::get_instance()
{
  static ObLongopsMgr longops_mgr;
  return longops_mgr;
}

int ObLongopsMgr::init()
{
  int ret = OB_SUCCESS;
  const int64_t memory_limit = 100 * 1024L * 1024L; // 100MB
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLongopsMgr has been inited", K(ret));
  } else if (OB_FAIL(bucket_lock_.init(DEFAULT_BUCKET_NUM))) {
  } else if (OB_FAIL(map_.create(DEFAULT_BUCKET_NUM, "ObLongopsMgr"))) {
  } else if (OB_FAIL(allocator_.init(DEFAULT_ALLOCATOR_PAGE_SIZE,
                                     lib::ObLabel("LongopsMgr"),
                                     memory_limit))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObLongopsMgr::destroy()
{
  if (map_.created()) {
    map_.destroy();
  }
}

void ObLongopsMgr::free_longops(ObILongopsStat *stat)
{
  stat->~ObILongopsStat();
  allocator_.free(stat);
}

int ObLongopsMgr::register_longops(ObILongopsStat *stat)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLongopsMgr has not been inited", K(ret));
  } else if (OB_ISNULL(stat) || OB_UNLIKELY(!stat->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(stat));
  } else {
    ObBucketHashWLockGuard guard(bucket_lock_, stat->get_longops_key().hash());
    if (OB_FAIL(map_.set_refactored(stat->get_longops_key(), stat))) {
      if (OB_HASH_EXIST == ret) {
        ret = OB_ENTRY_EXIST;
      }
    } else {
      LOG_INFO("register longops finish", K(ret), K(*stat));
    }
  }
  return ret;
}

int ObLongopsMgr::unregister_longops(ObILongopsStat *stat)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLongopsMgr has not been inited", K(ret));
  } else if (OB_ISNULL(stat) || OB_UNLIKELY(!stat->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(stat));
  } else {
    ObBucketHashWLockGuard guard(bucket_lock_, stat->get_longops_key().hash());
    ObILongopsKey key = stat->get_longops_key();
    if (OB_FAIL(map_.erase_refactored(stat->get_longops_key()))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("failed to erase map", K(ret), KPC(stat));
      } else {
        ret = OB_ENTRY_NOT_EXIST;
        free_longops(stat);
      }
    } else {
      free_longops(stat);
    }
    LOG_INFO("unregister longops finish", K(ret), K(key));
  }
  return ret;
}

int ObLongopsMgr::get_longops(const ObILongopsKey &key, ObLongopsValue &value)
{
  int ret = OB_SUCCESS;
  ObILongopsStat *stat = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLongopsMgr has not been inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key));
  } else {
    ObBucketHashRLockGuard guard(bucket_lock_, key.hash());
    if (OB_FAIL(map_.get_refactored(key, stat))) {
      LOG_WARN("failed to get key", K(ret), K(key));
      if (OB_HASH_NOT_EXIST == ret) {
        ret = OB_ENTRY_NOT_EXIST;
      }
    } else if (OB_ISNULL(stat)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("longops stat is null", K(ret));
    } else if (OB_FAIL(stat->get_longops_value(value))) {
    }
  }
  return ret;
}

int ObLongopsMgr::begin_iter(ObLongopsIterator &iter)
{
  int ret = OB_SUCCESS;
  iter.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLongopsMgr has not been inited", K(ret));
  } else if (OB_FAIL(iter.init(this))) {
  }
  return ret;
}

template <typename Callback>
int ObLongopsMgr::foreach(Callback &callback)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLongopsMgr has not been inited", K(ret));
  } else if (OB_FAIL(map_.foreach_refactored(callback))) {
  }
  return ret;
}

ObLongopsIterator::ObKeySnapshotCallback::ObKeySnapshotCallback(
    ObIArray<ObILongopsKey> &key_snapshot)
  : key_snapshot_(key_snapshot)
{
}

int ObLongopsIterator::ObKeySnapshotCallback::operator()(PAIR &pair)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(key_snapshot_.push_back(pair.first))) {
  }
  return ret;
}


ObLongopsIterator::ObLongopsIterator()
  : is_inited_(false), key_snapshot_(), key_cursor_(0), longops_mgr_(nullptr)
{
}

ObLongopsIterator::~ObLongopsIterator()
{
  reset();
}

void ObLongopsIterator::reset()
{
  key_snapshot_.reset();
  key_cursor_ = 0;
  longops_mgr_ = nullptr;
  is_inited_ = false;
}

int ObLongopsIterator::init(ObLongopsMgr *longops_mgr)
{
  int ret = OB_SUCCESS;
  ObKeySnapshotCallback callback(key_snapshot_);
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLongopsIterator has been inited twice", K(ret));
  } else if (OB_ISNULL(longops_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(longops_mgr));
  } else if (OB_FAIL(longops_mgr->foreach(callback))) {
  } else {
    key_cursor_ = 0;
    longops_mgr_ = longops_mgr;
    is_inited_ = true;
  }
  return ret;
}

int ObLongopsIterator::get_next(ObLongopsValue &value)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLongopsIterator has not been inited", K(ret));
  } else {
    bool need_retry = true;
    while (OB_SUCC(ret) && need_retry && key_cursor_ < key_snapshot_.count()) {
      const ObILongopsKey &key = key_snapshot_.at(key_cursor_);
      if (false && false) {
        // Normal user tenants can only check their own longops tasks.
      } else if (OB_FAIL(longops_mgr_->get_longops(key, value))) {
        if (OB_UNLIKELY(OB_ENTRY_NOT_EXIST != ret)) {
          LOG_WARN("fail to get parition stat", K(ret), K(key));
        } else {
          need_retry = true;
          ret = OB_SUCCESS;
        }
      } else {
        need_retry = false;
      }
      ++key_cursor_;
    }

    if (OB_SUCC(ret)) {
      // reach the end, but get no longops record.
      ret = need_retry && key_cursor_ >= key_snapshot_.count() ? OB_ITER_END : ret;
    }
  }
  return ret;
}

} //end namespace share
} //end namespace oceanbase
