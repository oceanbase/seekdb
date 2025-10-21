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

#ifndef OCEANBASE_TRANSACTION_OB_TRANS_COND_
#define OCEANBASE_TRANSACTION_OB_TRANS_COND_

#include <stdint.h>
#include "lib/lock/ob_monitor.h"
#include "lib/lock/mutex.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace transaction
{

class ObTransCond
{
public:
  ObTransCond() { reset(); }
  ~ObTransCond() {}
  void reset();
public:
  // when SQL submit or abort transaction, it must wait transaction response for some time.
  // safer to call wait(wait_time_us, result)
  int wait(const int64_t wait_time_us, int &result);
  // @deprecated
  //int wait(const int64_t wait_time_us);

  // notify and set transaction result
  void notify(const int result);

  static void usleep(const int64_t us);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTransCond);
private:
  bool finished_;
  mutable obutil::ObMonitor<obutil::Mutex> monitor_;
  int result_;
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TRANS_COND_
