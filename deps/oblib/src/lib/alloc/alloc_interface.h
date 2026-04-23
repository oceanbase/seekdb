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

#ifndef _ALLOC_INTERFACE_H_
#define _ALLOC_INTERFACE_H_

#include <stdint.h>
#include <cstdlib>
#include <cstddef>
#include "lib/allocator/ob_mod_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/lock/ob_futex.h"
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace lib
{
class ObTenantCtxAllocator;
struct AChunk;
struct ABlock;
struct ObMemAttr;
class IChunkMgr
{
public:
  virtual AChunk *alloc_chunk(const uint64_t size, const ObMemAttr &attr) = 0;
  virtual void free_chunk(AChunk *chunk, const ObMemAttr &attr) = 0;
}; // end of class IChunkMgr

class IBlockMgr
{
public:
  IBlockMgr() {}
  IBlockMgr(int64_t tenant_id, int64_t ctx_id)
    : tenant_id_(tenant_id), ctx_id_(ctx_id) {}
  virtual ABlock *alloc_block(uint64_t size, const ObMemAttr &attr) = 0;
  virtual void free_block(ABlock *block) = 0;
  virtual int64_t sync_wash(int64_t wash_size) = 0;
  virtual int64_t get_tenant_id() { return tenant_id_; }
  virtual int64_t get_ctx_id() { return ctx_id_; }
  void set_tenant_id(const int64_t tenant_id) { tenant_id_ = tenant_id; }
  void set_ctx_id(const int64_t ctx_id) { ctx_id_ = ctx_id; }
protected:
  int64_t tenant_id_;
  int64_t ctx_id_;
}; // end of class IBlockMgr

class LightMutex
{
public:
  static constexpr int32_t WAIT_MASK = 1<<31;
  static constexpr int32_t WRITE_MASK = 1<<30;
  LightMutex() : v_(0), wait_cnt_(0) {}
  int lock(int64_t timeout_us = INT64_MAX)
  {
    const int32_t MAX_TRY_CNT = 16;
    int ret = common::OB_TIMEOUT;
    const int32_t tid = static_cast<uint32_t>(GETTID());
    for (int i = 0; i < MAX_TRY_CNT; ++i) {
      if (ATOMIC_BCAS(&v_, 0, tid | WRITE_MASK)) {
        ret = common::OB_SUCCESS;
        break;
      }
      PAUSE();
    }
    if (OB_FAIL(ret)) {
      ret = wait(tid, timeout_us);
    }
    return ret;
  }
  void unlock()
  {
    int32_t v = ATOMIC_SET(&v_, 0);
    if (OB_UNLIKELY(WAIT_MASK == (v & WAIT_MASK))) {
      futex_wake(&v_, 1);
    }
  }
  int trylock()
  {
    const int32_t tid = static_cast<uint32_t>(GETTID());
    return ATOMIC_BCAS(&v_, 0, tid | WRITE_MASK) ? common::OB_SUCCESS : common::OB_EAGAIN;
  }
  void enable_record_stat(bool) {}
  int32_t get_wait_cnt() const { return wait_cnt_; }
private:
  int wait(const int32_t tid, int64_t timeout_us)
  {
    int ret = common::OB_TIMEOUT;
    static constexpr timespec TIMEOUT = {0, 100 * 1000 * 1000};
    const int64_t abs_timeout_us = INT64_MAX == timeout_us ? INT64_MAX : common::ObTimeUtility::current_time() + timeout_us;
    ATOMIC_INC(&wait_cnt_);
    while (true) {
      const int32_t v = v_;
      // check timeout
      if (abs_timeout_us - common::ObTimeUtility::current_time() <= 0) {
        break;
      }
      if (WAIT_MASK == (v & WAIT_MASK) || ATOMIC_BCAS(&v_, v | WRITE_MASK, v | WAIT_MASK)) {
        futex_wait(&v_, v | WRITE_MASK | WAIT_MASK, &TIMEOUT);
      }
      if (ATOMIC_BCAS(&v_, 0, tid | WRITE_MASK | WAIT_MASK)) {
        ret = common::OB_SUCCESS;
        break;
      }
    }
    ATOMIC_DEC(&wait_cnt_);
    return ret;
  }
private:
  int32_t v_;
  int32_t wait_cnt_;
};

class ISetLocker
{
public:
  virtual int lock(const int64_t timeout_us = INT64_MAX) = 0;
  virtual void unlock() = 0;
  virtual bool trylock() = 0;
};

class SetDoNothingLocker : public ISetLocker
{
public:
  int lock(const int64_t timeout_us = INT64_MAX) override { return OB_SUCCESS; }
  void unlock() override {}
  bool trylock() override { return true; }
};

template<typename t_lock>
class SetLocker : public ISetLocker
{
public:
  SetLocker(t_lock &mutex)
    : mutex_(mutex) {}
  int lock(const int64_t timeout_us = INT64_MAX) override
  {
    return mutex_.lock(timeout_us);
  }
  void unlock() override
  {
    mutex_.unlock();
  }
  bool trylock() override
  {
    return 0 == mutex_.trylock();
  }
private:
  t_lock &mutex_;
};

template<typename t_lock>
class SetLockerNoLog : public ISetLocker
{
public:
  SetLockerNoLog(t_lock &mutex)
    : mutex_(mutex), is_disable_(false) {}
  int lock(const int64_t timeout_us = INT64_MAX) override
  {
    int ret = mutex_.lock(timeout_us);
    if (OB_SUCC(ret)) {
      is_disable_ = !OB_LOGGER.is_enable_logging();
      OB_LOGGER.set_disable_logging(true);
    }
    return ret;
  }
  void unlock() override
  {
    OB_LOGGER.set_disable_logging(is_disable_);
    mutex_.unlock();
  }
  bool trylock() override
  {
    bool succ = 0 == mutex_.trylock();
    if (succ) {
      is_disable_ = !OB_LOGGER.is_enable_logging();
      OB_LOGGER.set_disable_logging(true);
    }
    return succ;
  }
private:
  t_lock &mutex_;
  bool is_disable_;
};

} // end of namespace lib
} // end of namespace oceanbase

#endif /* _ALLOC_INTERFACE_H_ */
