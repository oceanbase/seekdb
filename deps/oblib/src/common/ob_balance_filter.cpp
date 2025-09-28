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

#include "common/ob_balance_filter.h"

namespace oceanbase
{
namespace common
{
ObBalanceFilter::ObBalanceFilter() : inited_(false),
                                     bucket_node_num_(0),
                                     thread_node_num_(0),
                                     bucket_nodes_(NULL),
                                     thread_nodes_(NULL),
                                     bucket_round_robin_(0)
{
}

ObBalanceFilter::~ObBalanceFilter()
{
  destroy();
}


void ObBalanceFilter::destroy()
{
  stop();
  wait();
  inited_ = false;
  bucket_round_robin_ = 0;
  if (NULL != thread_nodes_) {
    ob_free(thread_nodes_);
    thread_nodes_ = NULL;
  }
  if (NULL != bucket_nodes_) {
    ob_free(bucket_nodes_);
    bucket_nodes_ = NULL;
  }
  thread_node_num_ = 0;
  bucket_node_num_ = 0;
}

void ObBalanceFilter::run1()
{
  while (!has_set_stop()) {
    int64_t max_thread_pos = 0;
    int64_t min_thread_pos = 0;
    for (int64_t i = 0; i < thread_node_num_; i++) {
      if (thread_nodes_[max_thread_pos].cnt < thread_nodes_[i].cnt) {
        max_thread_pos = i;
      }
      if (thread_nodes_[min_thread_pos].cnt > thread_nodes_[i].cnt) {
        min_thread_pos = i;
      }
    }
    if (0 < thread_nodes_[max_thread_pos].cnt
        && thread_nodes_[max_thread_pos].cnt > (thread_nodes_[min_thread_pos].cnt * 2)) {
      int64_t bucket_cnt = 0;
      int64_t min_bucket_pos = 0;
      for (int64_t i = 0; i < bucket_node_num_; i++) {
        if (max_thread_pos == bucket_nodes_[i].thread_pos
            && 0 < bucket_nodes_[i].cnt) {
          bucket_cnt += 1;
          if (0 == bucket_nodes_[min_bucket_pos].cnt) {
            min_bucket_pos = i;
          }
          if (bucket_nodes_[min_bucket_pos].cnt > bucket_nodes_[i].cnt) {
            min_bucket_pos = i;
          }
        }
      }
      if (1 < bucket_cnt
          && 0 <= bucket_nodes_[min_bucket_pos].thread_pos
          && 0 < bucket_nodes_[min_bucket_pos].cnt) {
        migrate(min_bucket_pos, min_thread_pos);
      } else {
        _OB_LOG(INFO, "thread=%ld cnt=%ld has only one bucket=%ld cnt=%ld, need not rebalance",
                  max_thread_pos, thread_nodes_[max_thread_pos].cnt, min_bucket_pos,
                  bucket_nodes_[min_bucket_pos].cnt);
      }
    } else {
      _OB_LOG(INFO, "max thread=%ld cnt=%ld, min thread=%ld cnt=%ld, need not rebalance",
                max_thread_pos, thread_nodes_[max_thread_pos].cnt, min_thread_pos,
                thread_nodes_[min_thread_pos].cnt);
    }
    for (int64_t i = 0; i < thread_node_num_; i++) {
      thread_nodes_[i].cnt = 0;
    }
    for (int64_t i = 0; i < bucket_node_num_; i++) {
      bucket_nodes_[i].cnt = 0;
    }
    ob_usleep(REBALANCE_INTERVAL, true);
  }
}


void ObBalanceFilter::migrate(const int64_t bucket_pos, const int64_t thread_pos)
{
  if (!inited_) {
    _OB_LOG_RET(WARN, OB_NOT_INIT, "have not inited");
  } else if (0 > bucket_pos
             || bucket_node_num_ <= bucket_pos
             || 0 > thread_pos
             || thread_node_num_ <= thread_pos) {
    _OB_LOG_RET(WARN, OB_NOT_INIT, "invalid param, bucket_pos=%ld thread_pos=%ld", bucket_pos, thread_pos);
  } else {
    _OB_LOG(INFO, "migrate bucket_pos=%ld bucket_cnt=%ld thread_pos from %ld:%ld to %ld:%ld",
              bucket_pos, bucket_nodes_[bucket_pos].cnt,
              bucket_nodes_[bucket_pos].thread_pos, thread_nodes_[bucket_nodes_[bucket_pos].thread_pos].cnt,
              thread_pos, thread_nodes_[thread_pos].cnt);
    bucket_nodes_[bucket_pos].thread_pos = thread_pos;
  }
}



}
}
