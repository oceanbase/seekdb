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
