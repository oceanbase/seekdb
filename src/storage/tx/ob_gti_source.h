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

#ifndef OCEANBASE_TRANSACTION_OB_GTI_SOURCE_
#define OCEANBASE_TRANSACTION_OB_GTI_SOURCE_

#include "lib/net/ob_addr.h"
#include "lib/lock/ob_latch.h"

namespace oceanbase
{

namespace transaction
{

class ObIGtiSource
{
public:
  virtual int start() { return OB_SUCCESS; }
  virtual void stop() {}
  virtual void wait() {}
  virtual void destroy() {}
  virtual void reset() {}
  virtual int get_trans_id(int64_t &trans_id) = 0;
};

class ObGtiSource : public ObIGtiSource
{
public:
  ObGtiSource() { reset(); }
  ~ObGtiSource() { destroy(); }
  int init(const common::ObAddr &server);
  virtual int start();
  virtual void stop();
  virtual void wait();
  virtual void destroy();
  virtual void reset();
  virtual int get_trans_id(int64_t &trans_id);
private:
  int refill_trans_id_range_();
public:
  TO_STRING_KV(K_(is_inited), K_(is_running), K_(next_id), K_(end_id));
public:
  static const int64_t TRANS_ID_RANGE_SIZE = 10000;
private:
  bool is_inited_;
  bool is_running_;
  int64_t next_id_;
  int64_t end_id_;
  // lock for refilling trans id range
  common::ObLatch lock_;
};

} // transaction
} // oceanbase

#endif //OCEANBASE_TRANSACTION_OB_GTI_SOURCE_
