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

#define USING_LOG_PREFIX  LIB

#include "lib/queue/ob_ms_queue.h"

namespace oceanbase
{
namespace common
{
////////////////////////////////////////////// ObMsQueue::TaskHead ///////////////////////////////////
void ObMsQueue::TaskHead::add(ObMsQueue::Task* node)
{
  if (NULL == node) {
  } else {
    node->next_ = NULL;
    if (NULL == head_) {
      head_ = node;
      tail_ = node;
    } else {
      tail_->next_ = node;
      tail_ = node;
    }
  }
}

ObMsQueue::Task* ObMsQueue::TaskHead::pop()
{
  ObMsQueue::Task* pret = NULL;
  if (NULL == head_) {
  } else {
    pret = head_;
    head_ = head_->next_;
    if (NULL == head_) {
      tail_ = NULL;
    }
  }
  return pret;
}

////////////////////////////////////////////// ObMsQueue::QueueInfo ///////////////////////////////////

int ObMsQueue::QueueInfo::destroy()
{
  array_ = NULL;
  len_ = 0;
  pop_ = 0;
  return OB_SUCCESS;
}

// NOT thread-safe

////////////////////////////////////////////// ObMsQueue::TaskHead ///////////////////////////////////

ObMsQueue::ObMsQueue() : inited_(false),
                         qlen_(0),
                         qcount_(0),
                         qinfo_(NULL),
                         seq_queue_(),
                         allocator_(NULL)
{}

ObMsQueue::~ObMsQueue()
{
  destroy();
}


int ObMsQueue::destroy()
{
  inited_ = false;

  seq_queue_.destroy();

  if (NULL != qinfo_ && NULL != allocator_) {
    for (int64_t index = 0; index < qcount_; index++) {
      if (NULL != qinfo_[index].array_) {
        allocator_->free(qinfo_[index].array_);
      }

      qinfo_[index].~QueueInfo();
    }

    allocator_->free(qinfo_);
    qinfo_ = NULL;
  }

  allocator_ = NULL;
  qcount_ = 0;
  qlen_ = 0;

  return OB_SUCCESS;
}

// it must be one thread in common slot

// different threads operate different qinfo


}; // end namespace clog
}; // end namespace oceanbase
