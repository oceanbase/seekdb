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
class BatchPopQueue
{
public:
  BatchPopQueue(): top_(NULL) {}
  ~BatchPopQueue() {}
  void push(TCLink* p) {
    TCLink *nv = NULL;
    p->next_ = ATOMIC_LOAD(&top_);
    while(p->next_ != (nv = ATOMIC_VCAS(&top_, p->next_, p))) {
      p->next_ = nv;
    }
  }
  TCLink* pop() {
    TCLink* h = ATOMIC_TAS(&top_, NULL);
    return link_reverse(h);
  }
private:
  static TCLink* link_reverse(TCLink* h) {
    TCLink* nh = NULL;
    while(h) {
      TCLink* next = h->next_;
      h->next_ = nh;
      nh = h;
      h = next;
    }
    return nh;
  }
private:
  TCLink* top_ CACHE_ALIGNED;
};
