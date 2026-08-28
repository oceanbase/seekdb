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

#ifndef OCEANBASE_RPC_OB_SQL_MEM_POOL_H_
#define OCEANBASE_RPC_OB_SQL_MEM_POOL_H_
#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
namespace obmysql
{
class ObSqlMemPool
{
public:
  enum {
    RPC_POOL_PAGE_SIZE = (1<<14) - 128,
    RPC_CACHE_SIZE = 3968
  };
  struct Page;
  explicit ObSqlMemPool(): last_(NULL), mem_label_("RpcDefault") {}
  explicit ObSqlMemPool(const char* label): last_(NULL), mem_label_(label) {}
  ~ObSqlMemPool() { destroy(); }
  static ObSqlMemPool* create(const char* label, int64_t req_sz, int64_t cache_sz = ObSqlMemPool::RPC_POOL_PAGE_SIZE);
  void* alloc(int64_t sz);
  
  void reuse();
  void destroy();
private:
  void add_page(Page* page);
private:
  Page* last_;
  
  const char* mem_label_;
};

}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_RPC_OB_SQL_MEM_POOL_H_ */
