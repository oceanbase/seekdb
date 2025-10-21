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

#include "lib/queue/ob_link_queue.h"

using namespace oceanbase;
using namespace oceanbase::common;

namespace oceanbase
{
namespace common
{

// add interface like ObSpLinkQueue
int ObSimpleLinkQueue::pop(Link *&p)
{
  int ret = OB_SUCCESS;
  while(OB_SUCCESS == (ret = do_pop(p)) && p == &dummy_) {
    ret = push(p);
  }
  if (OB_SUCCESS != ret) {
    p = NULL;
  }
  return ret;
}

int ObSimpleLinkQueue::push(Link *p)
{
  int ret = OB_SUCCESS;
  Link *tail = NULL;
  if (NULL == p) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    p->next_ = NULL;
    tail = tail_;
    tail_ = p;
    tail->next_ = p;
  }
  return ret;
}

int ObSimpleLinkQueue::do_pop(Link *&p)
{
  int ret = OB_SUCCESS;
  if (head_ == tail_) {
    ret = OB_EAGAIN;
  } else {
    Link *head = head_;
    Link* next = head->next_;
    head_ = next;
    p = head;
  }
  return ret;
}

} // end namespace common
} // end namespace oceanbase
