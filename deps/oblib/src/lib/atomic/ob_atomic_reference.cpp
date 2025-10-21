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

#include "lib/atomic/ob_atomic_reference.h"
#include "lib/oblog/ob_log.h"

using namespace oceanbase;
using namespace oceanbase::lib;
using namespace oceanbase::common;

namespace oceanbase
{
namespace common
{

/*
 * -----------------------------------------------------------ObAtomicReference-----------------------------------------------------
 */
ObAtomicReference::ObAtomicReference()
{
  atomic_num_.seq = 0;
  atomic_num_.ref = 0;
}

ObAtomicReference::~ObAtomicReference()
{
}

void ObAtomicReference::reset()
{
  //The seq must NOT be reset
  atomic_num_.ref = 0;
}

int ObAtomicReference::inc_ref_cnt()
{
  int ret = OB_SUCCESS;
  AtomicInt64 atomic_old = { 0 };
  AtomicInt64 atomic_new = { 0 };
  while (OB_SUCC(ret)) {
#if defined(__aarch64__)
    atomic_old.atomic = ATOMIC_FAAx(&atomic_num_.atomic, 0, 0);
#else
    atomic_old.atomic = ATOMIC_LOAD(&atomic_num_.atomic);
#endif
    atomic_new.atomic = atomic_old.atomic;

    atomic_new.ref += 1;
    if (OB_UNLIKELY(0 == atomic_new.ref)) {
      ret = OB_INTEGER_PRECISION_OVERFLOW;
      COMMON_LOG(WARN, "The reference count is overflow, ", K(ret));
    } else {
      if (ATOMIC_BCAS(&(atomic_num_.atomic), atomic_old.atomic, atomic_new.atomic)) {
        break;
      } else {
        PAUSE();
      }
    }
  }
  return ret;
}

int ObAtomicReference::check_seq_num_and_inc_ref_cnt(const uint32_t seq_num)
{
  int ret = OB_SUCCESS;
  AtomicInt64 atomic_old = { 0 };
  AtomicInt64 atomic_new = { 0 };
  while (OB_SUCC(ret)) {
#if defined(__aarch64__)
    atomic_old.atomic = ATOMIC_FAAx(&atomic_num_.atomic, 0, 0);
#else
    atomic_old.atomic = ATOMIC_LOAD(&atomic_num_.atomic);
#endif
    atomic_new.atomic = atomic_old.atomic;
    atomic_new.ref += 1;

    if (OB_UNLIKELY(0 == atomic_new.ref)) {
      ret = OB_INTEGER_PRECISION_OVERFLOW;
      COMMON_LOG(WARN, "The reference count is overflow, ", K(ret));
    } else if (OB_UNLIKELY(seq_num != atomic_old.seq) || OB_UNLIKELY(0 == atomic_old.ref)) {
      ret = OB_EAGAIN;
      //normal case, do not print log
    } else if (ATOMIC_BCAS(&(atomic_num_.atomic), atomic_old.atomic, atomic_new.atomic)) {
      break;
    } else {
      PAUSE();
    }
  }
  return ret;
}

int ObAtomicReference::check_and_inc_ref_cnt()
{
  int ret = OB_SUCCESS;
  AtomicInt64 atomic_old = { 0 };
  AtomicInt64 atomic_new = { 0 };
  while (OB_SUCC(ret)) {
#if defined(__aarch64__)
    atomic_old.atomic = ATOMIC_FAAx(&atomic_num_.atomic, 0, 0);
#else
    atomic_old.atomic = ATOMIC_LOAD(&atomic_num_.atomic);
#endif
    atomic_new.atomic = atomic_old.atomic;
    atomic_new.ref += 1;

    if (OB_UNLIKELY(0 == atomic_new.ref)) {
      ret = OB_INTEGER_PRECISION_OVERFLOW;
      COMMON_LOG(WARN, "The reference count is overflow, ", K(ret));
    } else if (OB_UNLIKELY(0 == atomic_old.ref)) {
      ret = OB_EAGAIN;
      //normal case, do not print log
    } else if (ATOMIC_BCAS(&(atomic_num_.atomic), atomic_old.atomic, atomic_new.atomic)) {
      break;
    } else {
      PAUSE();
    }
  }
  return ret;
}

int ObAtomicReference::dec_ref_cnt_and_inc_seq_num(uint32_t &ref_cnt)
{
  int ret = OB_SUCCESS;
  AtomicInt64 atomic_old = { 0 };
  AtomicInt64 atomic_new = { 0 };
  while (OB_SUCC(ret)) {
#if defined(__aarch64__)
    atomic_old.atomic = ATOMIC_FAAx(&atomic_num_.atomic, 0, 0);
#else
    atomic_old.atomic = ATOMIC_LOAD(&atomic_num_.atomic);
#endif
    atomic_new.atomic = atomic_old.atomic;

    if (OB_UNLIKELY(0 == atomic_old.ref)) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "The reference count is 0, ", K(ret));
    } else {
      atomic_new.ref -= 1;
      if (0 == atomic_new.ref) {
        atomic_new.seq += 1;
      }

      if (ATOMIC_BCAS(&(atomic_num_.atomic), atomic_old.atomic, atomic_new.atomic)) {
        break;
      } else {
        PAUSE();
      }
    }
  }

  if (OB_SUCC(ret)) {
    ref_cnt = atomic_new.ref;
  }
  return ret;
}



}
}
