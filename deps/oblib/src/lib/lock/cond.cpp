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

#include "lib/lock/cond.h"

using namespace oceanbase::common;

namespace obutil
{
Cond::Cond()
{

    int rt = pthread_condattr_init(&_attr);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to init cond attr, err=%d", rt);
    }
    // Set the attribute to use CLOCK_MONOTONIC clock source
    rt = pthread_condattr_setclock(&_attr, CLOCK_MONOTONIC);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to set MONOTONIC Clock, err=%d", rt);
    }

    rt = pthread_cond_init(&_cond, &_attr);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to init cond, err=%d", rt);
    }
}

Cond::~Cond()
{
  int rt = pthread_condattr_destroy(&_attr);
  if (0 != rt) {
    _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to destroy cond attr, err=%d", rt);
  }
  rt = pthread_cond_destroy(&_cond);
  if (0 != rt) {
    _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to destroy cond, err=%d", rt);
  }
}

void Cond::signal()
{
    const int rt = pthread_cond_signal(&_cond);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to signal condition, err=%d", rt);
    }
}

void Cond::broadcast()
{
    const int rt = pthread_cond_broadcast(&_cond);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to broadcast condition, err=%d", rt);
    }
}
}//end namespace obutil
