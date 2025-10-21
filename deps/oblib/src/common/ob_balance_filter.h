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

#ifndef  OCEANBASE_COMMON_BALANCE_FILTER_H_
#define  OCEANBASE_COMMON_BALANCE_FILTER_H_

#include "lib/ob_define.h"
#include "lib/hash/ob_hashutils.h"
#include "lib/thread/thread_pool.h"


namespace oceanbase
{
namespace common
{
class ObBalanceFilter : public lib::ThreadPool
{
  struct BucketNode
  {
    volatile int64_t thread_pos;
    volatile int64_t cnt;
  };
  struct ThreadNode
  {
    volatile int64_t cnt;
  };
  static const int64_t AMPLIFICATION_FACTOR = 100;
  static const int64_t MAX_THREAD_NUM = 4096;
  static const int64_t REBALANCE_INTERVAL = 3L * 1000000L;
public:
  ObBalanceFilter();
  ~ObBalanceFilter();
public:
  void destroy();
  void run1() override;
public:
  void migrate(const int64_t bucket_pos, const int64_t thread_pos);
private:
  bool inited_;
  int64_t bucket_node_num_;
  volatile int64_t thread_node_num_;
  BucketNode *bucket_nodes_;
  ThreadNode *thread_nodes_;
  uint64_t bucket_round_robin_;
};
}
}

#endif //OCEANBASE_COMMON_BALANCE_FILTER_H_
