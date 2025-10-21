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

static void link_del(TCLink* h, TCLink* p)
{
  h->next_ = p->next_;
}

static void link_insert(TCLink* h, TCLink* p)
{
  p->next_ = h->next_;
  h->next_ = p;
}

typedef bool less_than_t(TCLink* p1, TCLink* p2);
static void order_link_insert(TCLink* h, TCLink* t, less_than_t less_than)
{
  TCLink* prev = h;
  TCLink* cur = NULL;
  while(h != (cur = prev->next_) && less_than(cur, t)) {
    prev = cur;
  }
  t->next_ = cur;
  prev->next_ = t;
}

struct TCDLink
{
  TCDLink(TCDLink* p): next_(p), prev_(p) {}
  ~TCDLink() {}
  TCDLink* next_;
  TCDLink* prev_;
};

static void dlink_insert(TCDLink* h, TCDLink* p)
{
  TCDLink* n = h->next_;
  p->next_ = n;
  p->prev_ = h;
  h->next_ = p;
  n->prev_ = p;
}

static void dlink_del(TCDLink* p)
{
  TCDLink* h = p->prev_;
  TCDLink* n = p->next_;
  h->next_ = n;
  n->prev_ = h;
}

/*
static bool dlist_is_empty(TCDLink* p)
{
  return p->next_ == p;
  }*/
