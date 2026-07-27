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

#define USING_LOG_PREFIX RPC
#include "rpc/ob_sql_mem_pool.h"


namespace oceanbase
{
namespace obmysql
{

struct ObSqlMemPool::Page
{
  Page(int64_t limit): next_(NULL), limit_(limit - sizeof(*this)), cur_(0) {}
  ~Page() {}
  void* alloc(int64_t sz) {
    void* ret = NULL;
    if (cur_ + sz <= limit_) {
      ret = base_ + cur_;
      cur_ += sz;
    }
    return ret;
  }
  void reset() { cur_ = 0; }
  Page* next_;
  int64_t limit_;
  int64_t cur_;
  char base_[];
};
static void* rpc_mem_pool_direct_alloc(const char* label, int64_t sz) {
  ObMemAttr attr(label, common::ObCtxIds::DEFAULT_CTX_ID);
  lib::ObCtxAllocatorGuard allocator = lib::ObMallocAllocator::get_instance()->get_ctx_allocator(common::ObCtxIds::DEFAULT_CTX_ID);
  if (OB_ISNULL(allocator)) {
    
  }
  return common::ob_malloc(sz, attr);
}
static void rpc_mem_pool_direct_free(void* p) { common::ob_free(p); }
static ObSqlMemPool::Page* rpc_mem_pool_create_page(const char* label, int64_t sz, int64_t cache_sz = ObSqlMemPool::RPC_POOL_PAGE_SIZE) {
  int64_t alloc_sz = std::max(sizeof(ObSqlMemPool::Page) + sz, (uint64_t)cache_sz);
  ObSqlMemPool::Page* page = (typeof(page))rpc_mem_pool_direct_alloc(label, alloc_sz);
  if (OB_ISNULL(page)) {
    LOG_WARN_RET(common::OB_ALLOCATE_MEMORY_FAILED, "rpc memory pool alloc memory failed", K(sz), K(alloc_sz));
  } else {
    new(page)ObSqlMemPool::Page(alloc_sz);
  }
  return page;
}
static void rpc_mem_pool_destroy_page(ObSqlMemPool::Page* page) {
  if (OB_NOT_NULL(page)) {
    page->ObSqlMemPool::Page::~Page();
    common::ob_free(page);
  }
}

ObSqlMemPool* ObSqlMemPool::create(const char* label, int64_t req_sz, int64_t cache_sz)
{
  Page* page = nullptr;
  ObSqlMemPool* pool = nullptr;
  if (OB_NOT_NULL(page = rpc_mem_pool_create_page(label, req_sz + sizeof(ObSqlMemPool), cache_sz))) {
    if (OB_NOT_NULL(pool = (typeof(pool))page->alloc(sizeof(ObSqlMemPool)))) {
      new(pool)ObSqlMemPool(label); // can not be null
      pool->add_page(page);
    } else {
      rpc_mem_pool_destroy_page(page);
    }
  }
  return pool;
}

void* ObSqlMemPool::alloc(int64_t sz)
{
  void* ret = NULL;
  Page* page = NULL;
  if (NULL != last_ && NULL != (ret = last_->alloc(sz))) {
  } else if (NULL == (page = rpc_mem_pool_create_page(mem_label_, sz))) {
  } else {
    ret = page->alloc(sz);
    add_page(page);
  }
  return ret;
}

void ObSqlMemPool::destroy()
{
  Page* cur = last_;
  last_ = NULL;
  while(NULL != cur) {
    Page* next = cur->next_;
    rpc_mem_pool_direct_free(cur);
    cur = next;
  }
}

void ObSqlMemPool::reuse()
{
  Page* cur = last_;
  Page* next = NULL;
  last_ = NULL;
  while(NULL != cur && NULL != (next = cur->next_)) {
    rpc_mem_pool_direct_free(cur);
    cur = next;
  }
  if (NULL != cur) {
    cur->reset();
    last_ = cur;
  }
}

void ObSqlMemPool::add_page(Page* page)
{
  page->next_ = last_;
  last_ = page;
}

}; // end namespace obmysql
}; // end namespace oceanbase
