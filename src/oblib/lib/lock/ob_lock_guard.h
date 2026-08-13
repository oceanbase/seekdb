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

#ifndef _OB_LOCK_GUARD_H_
#define _OB_LOCK_GUARD_H_

#include <new>
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace lib
{

template <typename LockT>
class ObLockGuard
{
public:
  [[nodiscard]] explicit ObLockGuard(LockT &lock);
  ~ObLockGuard();
  inline int get_ret() const { return ret_; }
private:
  // disallow copy
  ObLockGuard(const ObLockGuard &other);
  ObLockGuard &operator=(const ObLockGuard &other);
  // disallow new
  void *operator new(std::size_t size);
  void *operator new(std::size_t size, const std::nothrow_t &nothrow_constant) throw();
  void *operator new(std::size_t size, void *ptr) throw();
private:
  // data members
  LockT &lock_;
  int ret_;
};

template <typename LockT>
inline ObLockGuard<LockT>::ObLockGuard(LockT &lock)
    : lock_(lock),
      ret_(common::OB_SUCCESS)
{
  if (OB_UNLIKELY(common::OB_SUCCESS != (ret_ = lock_.lock()))) {
    COMMON_LOG_RET(ERROR, ret_, "Fail to lock, ", K_(ret));
  }
}

template <typename LockT>
inline ObLockGuard<LockT>::~ObLockGuard()
{
  if (OB_LIKELY(common::OB_SUCCESS == ret_)) {
    if (OB_UNLIKELY(common::OB_SUCCESS != (ret_ = lock_.unlock()))) {
      COMMON_LOG_RET(ERROR, ret_, "Fail to unlock, ", K_(ret));
    }
  }
}

template <typename LockT>
class ObLockGuardWithTimeout
{
public:
  [[nodiscard]] explicit ObLockGuardWithTimeout(LockT &lock, const int64_t abs_timeout_us = INT64_MAX);
  ~ObLockGuardWithTimeout();
  inline int get_ret() const { return ret_; }
private:
  // disallow copy
  ObLockGuardWithTimeout(const ObLockGuardWithTimeout &other);
  ObLockGuardWithTimeout &operator=(const ObLockGuardWithTimeout &other);
  // disallow new
  void *operator new(std::size_t size);
  void *operator new(std::size_t size, const std::nothrow_t &nothrow_constant) throw();
  void *operator new(std::size_t size, void *ptr) throw();
private:
  // data members
  LockT &lock_;
  int ret_;
};

template <typename LockT>
inline ObLockGuardWithTimeout<LockT>::ObLockGuardWithTimeout(LockT &lock, const int64_t abs_timeout_us)
    : lock_(lock),
      ret_(common::OB_SUCCESS)
{
  if (OB_UNLIKELY(common::OB_SUCCESS != (ret_ = lock_.lock(abs_timeout_us)) && OB_TIMEOUT != ret_)) {
    COMMON_LOG_RET(ERROR, ret_, "Fail to lock, ", K_(ret));
  }
}

template <typename LockT>
inline ObLockGuardWithTimeout<LockT>::~ObLockGuardWithTimeout()
{
  if (OB_LIKELY(common::OB_SUCCESS == ret_)) {
    if (OB_UNLIKELY(common::OB_SUCCESS != (ret_ = lock_.unlock()))) {
      COMMON_LOG_RET(ERROR, ret_, "Fail to unlock, ", K_(ret));
    }
  }
}

} // end of namespace lib
} // end of namespace oceanbase

#endif /* _OB_LOCK_GUARD_H_ */
