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

class SimpleTCLinkQueue
{
public:
  SimpleTCLinkQueue(): head_(NULL), tail_(NULL), cnt_(0) {}
  ~SimpleTCLinkQueue() {}
public:
  int64_t cnt() { return cnt_; }
  TCLink* top() { return head_; }
  TCLink* pop() {
    TCLink* p = NULL;
    if (NULL != head_) {
      cnt_--;
      p = head_;
      head_ = head_->next_;
      if (NULL == head_) {
        tail_ = NULL;
      }
    }
    return p;
  }
  void push(TCLink* p) {
    cnt_++;
    p->next_ = NULL;
    if (NULL == tail_) {
      head_ = tail_ = p;
    } else {
      tail_->next_ = p;
      tail_ = p;
    }
  }
private:
  TCLink* head_;
  TCLink* tail_;
  int64_t cnt_;
};
