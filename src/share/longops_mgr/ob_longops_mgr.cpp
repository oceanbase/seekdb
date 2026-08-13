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
  : lock_(), longops_stats_()
{
}

ObLongopsMgr &ObLongopsMgr::get_instance()
{
  static ObLongopsMgr longops_mgr;
  return longops_mgr;
}

void ObLongopsMgr::destroy()
{
  common::ObSpinLockGuard guard(lock_);
  for (int64_t i = 0; i < longops_stats_.count(); ++i) {
    free_longops_without_lock_(longops_stats_.at(i));
  }
  longops_stats_.destroy();
}

void ObLongopsMgr::free_longops(ObILongopsStat *stat)
{
  if (OB_NOT_NULL(stat)) {
    free_longops_without_lock_(stat);
  }
}

void ObLongopsMgr::free_longops_without_lock_(ObILongopsStat *stat)
{
  if (OB_NOT_NULL(stat)) {
    stat->~ObILongopsStat();
    common::ob_free(stat);
  }
}

int ObLongopsMgr::find_longops_idx_(const ObILongopsKey &key, int64_t &idx) const
{
  int ret = OB_SUCCESS;
  idx = -1;
  for (int64_t i = 0; OB_SUCC(ret) && i < longops_stats_.count(); ++i) {
    ObILongopsStat *stat = longops_stats_.at(i);
    if (OB_ISNULL(stat)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("longops stat is null", K(ret), K(i));
    } else if (stat->get_longops_key() == key) {
      idx = i;
      break;
    }
  }
  if (OB_SUCC(ret) && idx < 0) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int ObLongopsMgr::register_longops(ObILongopsStat *stat)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stat) || OB_UNLIKELY(!stat->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(stat));
  } else {
    common::ObSpinLockGuard guard(lock_);
    int64_t idx = -1;
    if (OB_FAIL(find_longops_idx_(stat->get_longops_key(), idx))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("failed to find longops stat", K(ret), KPC(stat));
      }
    } else {
      ret = OB_ENTRY_EXIST;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(longops_stats_.push_back(stat))) {
    } else {
      LOG_INFO("register longops finish", K(ret), K(*stat));
    }
  }
  return ret;
}

int ObLongopsMgr::unregister_longops(ObILongopsStat *stat)
{
  int ret = OB_SUCCESS;
  bool need_free = false;
  if (OB_ISNULL(stat) || OB_UNLIKELY(!stat->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(stat));
  } else {
    ObILongopsKey key = stat->get_longops_key();
    {
      common::ObSpinLockGuard guard(lock_);
      int64_t idx = -1;
      if (OB_FAIL(find_longops_idx_(key, idx))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to find longops stat", K(ret), KPC(stat));
        } else {
          need_free = true;
        }
      } else if (OB_FAIL(longops_stats_.remove(idx))) {
      } else {
        need_free = true;
      }
    }
    if (need_free) {
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
  if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(key));
  } else {
    common::ObSpinLockGuard guard(lock_);
    int64_t idx = -1;
    if (OB_FAIL(find_longops_idx_(key, idx))) {
    } else if (OB_UNLIKELY(idx >= longops_stats_.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid longops stat index", K(ret), K(idx), K(longops_stats_.count()));
    } else if (OB_ISNULL(stat = longops_stats_.at(idx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("longops stat is null", K(ret), K(idx));
    } else if (OB_FAIL(stat->get_longops_value(value))) {
    }
  }
  return ret;
}

int ObLongopsMgr::begin_iter(ObLongopsIterator &iter)
{
  int ret = OB_SUCCESS;
  iter.reset();
  if (OB_FAIL(iter.init(this))) {
  }
  return ret;
}

template <typename Callback>
int ObLongopsMgr::foreach(Callback &callback)
{
  int ret = OB_SUCCESS;
  common::ObSpinLockGuard guard(lock_);
  for (int64_t i = 0; OB_SUCC(ret) && i < longops_stats_.count(); ++i) {
    ObILongopsStat *stat = longops_stats_.at(i);
    if (OB_ISNULL(stat)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("longops stat is null", K(ret), K(i));
    } else if (OB_FAIL(callback(stat->get_longops_key()))) {
    }
  }
  return ret;
}

ObLongopsIterator::ObKeySnapshotCallback::ObKeySnapshotCallback(
    ObIArray<ObILongopsKey> &key_snapshot)
  : key_snapshot_(key_snapshot)
{
}

int ObLongopsIterator::ObKeySnapshotCallback::operator()(const ObILongopsKey &key)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(key_snapshot_.push_back(key))) {
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
      if (OB_FAIL(longops_mgr_->get_longops(key, value))) {
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
