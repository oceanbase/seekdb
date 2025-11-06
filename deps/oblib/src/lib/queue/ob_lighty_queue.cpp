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

#include "lib/queue/ob_lighty_queue.h"


#include "lib/utility/utility.h"

namespace oceanbase
{
namespace common
{
void ObLightyCond::signal()
{
  (void)ATOMIC_FAA(&futex_.uval(), 1);
  if (ATOMIC_LOAD(&n_waiters_) > 0) {
    futex_.wake(INT32_MAX);
  }
}

void ObLightyCond::wait(const uint32_t cmp, const int64_t timeout)
{
  if (timeout > 0) {
    (void)ATOMIC_FAA(&n_waiters_, 1);
    (void)futex_.wait(cmp, timeout);
    (void)ATOMIC_FAA(&n_waiters_, -1);
  }
}

int ObLightyQueue::init(const uint64_t capacity,
                        const lib::ObLabel &label,
                        const uint64_t tenant_id) {
  int ret = OB_SUCCESS;
  uint64_t n_cond = calc_n_cond(capacity);
  ObMemAttr attr;
  attr.tenant_id_ = tenant_id;
  attr.label_ = label;
  if (is_inited()) {
    ret = OB_INIT_TWICE;
  } else if (NULL == (data_ = (void**)ob_malloc(capacity * sizeof(void*) + n_cond * sizeof(Cond), attr))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    memset(data_, 0, capacity * sizeof(void*));
    capacity_ = capacity;
    n_cond_ = n_cond;
    cond_ = (Cond*)(data_ + capacity);
    for(int i = 0; i < n_cond; i++) {
      new(cond_ + i)Cond();
    }
  }
  return ret;
}

void ObLightyQueue::destroy()
{
  if (NULL != data_) {
    ob_free(data_);
    data_ = NULL;
    cond_ = NULL;
  }
}

void ObLightyQueue::clear()
{
  void* p = NULL;
  while(OB_SUCCESS == pop(p, 0))
    ;
}

int ObLightyQueue::push(void* p)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(data_)) {
    ret = OB_NOT_INIT;
  } else {
    uint64_t limit = 0;
    uint64_t seq = inc_if_lt(&push_, &pop_, capacity_, limit);
    if (seq < limit) {
      store(seq, p);
      get_cond(seq).signal();
    } else {
      ret = OB_SIZE_OVERFLOW;
    }
  }
  return ret;
}

int ObLightyQueue::pop(void*& p, int64_t timeout)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(data_)) {
    ret = OB_NOT_INIT;
  } else {
    void* DUMMY = (void*)(~0ULL);
    uint64_t seq = ATOMIC_FAA(&pop_, 1);
    uint64_t push_idx = ATOMIC_LOAD(&push_);
    if (push_idx <= seq) {
      int64_t abs_timeout = (timeout > 0 ? (get_us() + timeout) : 0);
      while((push_idx = wait_push(seq, timeout)) <= seq && (timeout = abs_timeout - get_us()) > 0) {
        PAUSE();
      }
      while((push_idx = push_bounded(DUMMY, seq + 1)) < seq) {
        PAUSE();
      }
    }
    if (DUMMY == (p = fetch(seq))) {
      p = NULL;
      ret = OB_ENTRY_NOT_EXIST;
    }
  }
  return ret;
}

uint64_t ObLightyQueue::push_bounded(void* p, uint64_t limit)
{
  uint64_t seq = inc_if_lt(&push_, limit);
  if (seq < limit) {
    store(seq, p);
    get_cond(seq).signal();
  }
  return seq;
}

uint64_t ObLightyQueue::inc_if_lt(uint64_t *addr, uint64_t *limit_addr, uint64_t delta, uint64_t &limit)
{
  uint64_t ov = 0;
  uint64_t nv = ATOMIC_LOAD(addr);
  limit = ATOMIC_LOAD(limit_addr) + delta;
  while (((ov = nv) < limit || ov < (limit = ATOMIC_LOAD(limit_addr) + delta))
         && ov != (nv = ATOMIC_VCAS(addr, ov, ov + 1))) {
    PAUSE();
  }
  return nv;
}

uint64_t ObLightyQueue::wait_push(uint64_t seq, int64_t timeout)
{
  uint32_t wait_id = get_cond(seq).get_seq();
  uint64_t push_idx = ATOMIC_LOAD(&push_);
  if (push_idx <= seq) {
    ObBKGDSessInActiveGuard inactive_guard;
    get_cond(seq).wait(wait_id, timeout);
  }
  return push_idx;
}

int64_t ObLightyQueue::get_us()
{
  return ::oceanbase::common::ObTimeUtility::current_time();
}

uint64_t ObLightyQueue::inc_if_lt(uint64_t* addr, uint64_t b)
{
  uint64_t ov = ATOMIC_LOAD(addr);
  uint64_t nv = 0;
  while(ov < b && ov != (nv = ATOMIC_VCAS(addr, ov, ov + 1))) {
    ov = nv;
  }
  return ov;
}

void* ObLightyQueue::fetch(uint64_t seq)
{
  void* p = NULL;
  void** addr = data_ + idx(seq);
  while(NULL == ATOMIC_LOAD(addr) || NULL == (p = ATOMIC_TAS(addr, NULL))) {
    PAUSE();
  }
  return p;
}

void ObLightyQueue::store(uint64_t seq, void* p)
{
  void** addr = data_ + idx(seq);
  while(!ATOMIC_BCAS(addr, NULL, p)) {
    PAUSE();
  }
}

static int64_t get_us() { return ::oceanbase::common::ObTimeUtility::current_time(); }

int LightyQueue::init(const uint64_t capacity, const lib::ObLabel &label, const uint64_t tenant_id)
{
  ObMemAttr attr(tenant_id, label);
  return queue_.init(capacity, global_default_allocator, attr);
}

int LightyQueue::push(void *data, const int64_t timeout)
{
  int ret = OB_SUCCESS;
  int64_t abs_timeout = (timeout > 0 ? (get_us() + timeout) : 0);
  int64_t wait_timeout = 0;
  while (true) { // WHITESCAN: OB_CUSTOM_ERROR_COVERED
    uint32_t seq = cond_.get_seq();
    if (OB_SUCCESS == (ret = queue_.push(data))) {
      break;
    } else if (timeout <= 0 || (wait_timeout = abs_timeout - get_us()) <= 0) {
      ret = OB_TIMEOUT;
      break;
    } else {
      cond_.wait(seq, wait_timeout);
    }
  }
  if (OB_SUCCESS == ret) {
    cond_.signal();
  }
  return ret;
}

int LightyQueue::pop(void *&data, const int64_t timeout)
{
  int ret = OB_SUCCESS;
  int64_t abs_timeout = (timeout > 0 ? (get_us() + timeout) : 0);
  int64_t wait_timeout = 0;
  while (true) { // WHITESCAN: OB_CUSTOM_ERROR_COVERED
    uint32_t seq = cond_.get_seq();
    if (OB_SUCCESS == (ret = queue_.pop(data))) {
      break;
    } else if (timeout <= 0 || (wait_timeout = abs_timeout - get_us()) <= 0) {
      break;
    } else {
      cond_.wait(seq, wait_timeout);
    }
  }
  if (OB_SUCCESS == ret) {
    cond_.signal();
  }
  return ret;
}



}; // end namespace common
}; // end namespace oceanbase
