/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
