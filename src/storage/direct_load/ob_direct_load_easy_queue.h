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
#pragma once

#include "lib/container/ob_se_array.h"
#include "lib/lock/ob_mutex.h"
#include "lib/list/ob_list.h"
#include "share/ob_errno.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace storage
{

template<class T>
class ObDirectLoadEasyQueue // A queue with very poor performance, mainly for convenience of use
{
public:
  ObDirectLoadEasyQueue() : malloc_(ObMemAttr(MTL_ID(), "TLD_EasyQueue")), queue_(malloc_) {}

  int push(const T &e) {
    int ret = OB_SUCCESS;
    lib::ObMutexGuard guard(mutex_);
    if (OB_FAIL(queue_.push_back(e))) {
      SERVER_LOG(WARN, "fail to push back queue", KR(ret));
    }
    return ret;
  }

  int64_t size() {
    lib::ObMutexGuard guard(mutex_);
    return queue_.size();
  }

  void pop_one(T &value) {
    lib::ObMutexGuard guard(mutex_);
    if (!queue_.empty()) {
      value = queue_.get_first();
      queue_.pop_front();
    }
  }

  void pop_all(common::ObIArray<T> &values) {
    lib::ObMutexGuard guard(mutex_);
    while (!queue_.empty()) {
      T &e = queue_.get_first();
      values.push_back(e);
      queue_.pop_front();
    }
  }

  void pop_count(common::ObIArray<T> &values, int count) {
    lib::ObMutexGuard guard(mutex_);
    while (!queue_.empty() && count--) {
      T &e = queue_.get_first();
      values.push_back(e);
      queue_.pop_front();
    }
  }

private:
  ObMalloc malloc_;
  common::ObList<T, common::ObIAllocator> queue_;
  lib::ObMutex mutex_;
};


}  // namespace storage
}  // namespace oceanbase
