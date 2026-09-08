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

#define USING_LOG_PREFIX TABLELOCK

#include "storage/tablelock/ob_named_lock_manager.h"

#include "lib/time/ob_time_utility.h"
#include "lib/worker.h"
#include "share/ob_errno.h"

namespace oceanbase
{
using namespace common;

namespace transaction
{
namespace tablelock
{

NamedLockManager::NamedLockManager()
  : cond_(),
    allocator_(),
    lock_map_(LockNameLess(), LockMapAllocator(allocator_)),
    owner_lock_map_(std::less<ObTableLockOwnerID>(), OwnerLockMapAllocator(allocator_)),
    next_lock_id_(1),
    is_inited_(false)
{
}

NamedLockManager::~NamedLockManager()
{
  destroy();
}

int NamedLockManager::init(const int64_t memory_limit)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("named lock manager init twice", K(ret));
  } else if (OB_UNLIKELY(memory_limit <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid named lock memory limit", K(ret), K(memory_limit));
  } else {
    allocator_.set_limit(memory_limit);
  }
  if (OB_SUCC(ret) && OB_FAIL(cond_.init(ObWaitEventIds::DEFAULT_COND_WAIT))) {
    LOG_WARN("failed to init named lock condition", K(ret));
    allocator_.reset();
  } else if (OB_SUCC(ret)) {
    is_inited_ = true;
  }
  return ret;
}

void NamedLockManager::destroy()
{
  if (is_inited_) {
    {
      ObThreadCondGuard guard(cond_);
      lock_map_.clear();
      owner_lock_map_.clear();
      next_lock_id_ = 1;
      cond_.broadcast();
      is_inited_ = false;
    }
    cond_.destroy();
    if (OB_UNLIKELY(allocator_.used() != 0)) {
      LOG_ERROR_RET(OB_ERR_UNEXPECTED,
                    "named lock allocator still owns memory after clearing locks",
                    "used", allocator_.used());
    }
    allocator_.reset();
  }
}

int NamedLockManager::create_lock_(const ObString &lock_name,
                                   const ObTableLockOwnerID &owner_id)
{
  int ret = OB_SUCCESS;
  LockName name((LockCharAllocator(allocator_)));
  OwnerLockMap::iterator owner_it = owner_lock_map_.end();
  LockNameSet::iterator owner_name_it;
  LockMap::iterator lock_it;
  bool owner_created = false;
  bool owner_name_inserted = false;
  bool lock_inserted = false;

  try {
    name.assign(lock_name.ptr(), lock_name.length());
    owner_it = owner_lock_map_.find(owner_id);
    if (owner_it == owner_lock_map_.end()) {
      const std::pair<OwnerLockMap::iterator, bool> owner_result = owner_lock_map_.emplace(
          owner_id, LockNameSet(LockNameLess(), LockNameSetAllocator(allocator_)));
      owner_it = owner_result.first;
      owner_created = owner_result.second;
      if (!owner_created) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to create named lock owner index", K(ret), K(owner_id));
      }
    }
    if (OB_SUCC(ret)) {
      const std::pair<LockNameSet::iterator, bool> name_result = owner_it->second.insert(name);
      owner_name_it = name_result.first;
      owner_name_inserted = name_result.second;
      if (!owner_name_inserted) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("named lock owner index already contains lock", K(ret), K(lock_name), K(owner_id));
      }
    }
    if (OB_SUCC(ret)) {
      const std::pair<LockMap::iterator, bool> lock_result = lock_map_.emplace(
          name, LockInfo(owner_id, 1, next_lock_id_, ObTimeUtility::current_time()));
      lock_it = lock_result.first;
      lock_inserted = lock_result.second;
      if (!lock_inserted) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("named lock already exists during insertion", K(ret), K(lock_name), K(owner_id));
      } else {
        ++next_lock_id_;
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("named lock memory quota is exhausted", K(ret), K(lock_name), K(owner_id));
  }

  if (OB_FAIL(ret)) {
    if (lock_inserted) {
      lock_map_.erase(lock_it);
    }
    if (owner_name_inserted) {
      owner_it->second.erase(owner_name_it);
    }
    if (owner_created && owner_it != owner_lock_map_.end() && owner_it->second.empty()) {
      owner_lock_map_.erase(owner_it);
    }
  }
  return ret;
}

int NamedLockManager::acquire(const ObString &lock_name,
                              const ObTableLockOwnerID &owner_id,
                              const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(lock_name.empty() || !owner_id.is_valid() || timeout_us < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid named lock argument", K(ret), K(lock_name), K(owner_id), K(timeout_us));
  } else if (OB_UNLIKELY(lock_name.length() > MAX_LOCK_NAME_LENGTH)) {
    ret = OB_ERR_DATA_TOO_LONG;
    LOG_WARN("named lock name is longer than legacy limit",
             K(ret), K(lock_name.length()), K(MAX_LOCK_NAME_LENGTH));
  } else {
    const int64_t start_ts = ObTimeUtility::current_time();
    const int64_t deadline_ts = start_ts + timeout_us;
    ObThreadCondGuard guard(cond_);
    bool finished = false;

    while (OB_SUCC(ret) && !finished) {
      LockMap::iterator lock_it = lock_map_.find(lock_name);
      if (lock_it == lock_map_.end()) {
        if (OB_FAIL(create_lock_(lock_name, owner_id))) {
          LOG_WARN("failed to create named lock", K(ret), K(lock_name), K(owner_id));
        } else {
          finished = true;
        }
      } else if (lock_it->second.owner_id_ == owner_id) {
        ++lock_it->second.ref_count_;
        finished = true;
      } else {
        const int64_t now = ObTimeUtility::current_time();
        if (timeout_us == 0 || now >= deadline_ts) {
          ret = OB_ERR_EXCLUSIVE_LOCK_CONFLICT;
        } else {
          const int64_t wait_us = MIN(WAIT_SLICE_US, deadline_ts - now);
          const int wait_ret = cond_.wait_us(wait_us);
          if (OB_SUCCESS != wait_ret && OB_TIMEOUT != wait_ret) {
            ret = wait_ret;
            LOG_WARN("failed to wait for named lock", K(ret), K(lock_name), K(owner_id));
          } else {
            const int status_ret = THIS_WORKER.check_status();
            if (OB_SUCCESS != status_ret && OB_TIMEOUT != status_ret) {
              ret = status_ret;
            }
          }
        }
      }
    }
  }
  return ret;
}

int NamedLockManager::release(const ObString &lock_name,
                              const ObTableLockOwnerID &owner_id,
                              int64_t &release_result)
{
  int ret = OB_SUCCESS;
  release_result = LOCK_NOT_EXIST_RELEASE_RESULT;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(lock_name.empty() || !owner_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObThreadCondGuard guard(cond_);
    LockMap::iterator lock_it = lock_map_.find(lock_name);
    if (lock_it == lock_map_.end()) {
      // MySQL RELEASE_LOCK() returns NULL if the named lock does not exist.
    } else if (lock_it->second.owner_id_ != owner_id) {
      release_result = LOCK_NOT_OWN_RELEASE_RESULT;
    } else {
      release_result = LOCK_RELEASED_RESULT;
      if (--lock_it->second.ref_count_ == 0) {
        OwnerLockMap::iterator owner_it = owner_lock_map_.find(owner_id);
        if (owner_it != owner_lock_map_.end()) {
          owner_it->second.erase(lock_it->first);
          if (owner_it->second.empty()) {
            owner_lock_map_.erase(owner_it);
          }
        }
        lock_map_.erase(lock_it);
        cond_.broadcast();
      }
    }
  }
  return ret;
}

int NamedLockManager::release_all(const ObTableLockOwnerID &owner_id,
                                  int64_t &release_count)
{
  int ret = OB_SUCCESS;
  release_count = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!owner_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObThreadCondGuard guard(cond_);
    OwnerLockMap::iterator owner_it = owner_lock_map_.find(owner_id);
    if (owner_it != owner_lock_map_.end()) {
      for (LockNameSet::const_iterator name_it = owner_it->second.begin();
           name_it != owner_it->second.end(); ++name_it) {
        LockMap::iterator lock_it = lock_map_.find(*name_it);
        if (lock_it != lock_map_.end()) {
          release_count += lock_it->second.ref_count_;
          lock_map_.erase(lock_it);
        }
      }
      owner_lock_map_.erase(owner_it);
      cond_.broadcast();
    }
  }
  return ret;
}

int NamedLockManager::is_free(const ObString &lock_name, bool &is_free)
{
  int ret = OB_SUCCESS;
  is_free = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(lock_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObThreadCondGuard guard(cond_);
    is_free = lock_map_.find(lock_name) == lock_map_.end();
  }
  return ret;
}

int NamedLockManager::get_owner(const ObString &lock_name,
                                ObTableLockOwnerID &owner_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(lock_name.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObThreadCondGuard guard(cond_);
    LockMap::const_iterator lock_it = lock_map_.find(lock_name);
    if (lock_it == lock_map_.end()) {
      ret = OB_EMPTY_RESULT;
    } else {
      owner_id = lock_it->second.owner_id_;
    }
  }
  return ret;
}

int NamedLockManager::has_lock(const ObTableLockOwnerID &owner_id, bool &has_lock)
{
  int ret = OB_SUCCESS;
  has_lock = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(!owner_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObThreadCondGuard guard(cond_);
    has_lock = owner_lock_map_.find(owner_id) != owner_lock_map_.end();
  }
  return ret;
}

int NamedLockManager::get_lock_snapshot(std::vector<LockSnapshot> &snapshot)
{
  int ret = OB_SUCCESS;
  snapshot.clear();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else {
    ObThreadCondGuard guard(cond_);
    try {
      snapshot.reserve(lock_map_.size());
      for (LockMap::const_iterator lock_it = lock_map_.begin();
           lock_it != lock_map_.end(); ++lock_it) {
        snapshot.push_back(LockSnapshot(
            std::string(lock_it->first.data(), lock_it->first.length()),
            lock_it->second.lock_id_,
            lock_it->second.owner_id_,
            lock_it->second.ref_count_,
            lock_it->second.create_timestamp_));
      }
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      snapshot.clear();
      LOG_WARN("failed to allocate named lock snapshot", K(ret));
    }
  }
  return ret;
}

} // namespace tablelock
} // namespace transaction
} // namespace oceanbase
