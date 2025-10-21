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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_LIB_CACHE_NODE_FACTORY_
#define OCEANBASE_SQL_PLAN_CACHE_OB_LIB_CACHE_NODE_FACTORY_

#include "sql/plan_cache/ob_i_lib_cache_node.h"
#include "sql/plan_cache/ob_plan_cache_util.h"

namespace oceanbase
{
namespace sql
{
class ObPlanCache;

class ObLCNodeFactory
{
public:
  ObLCNodeFactory() : lib_cache_(NULL) {}
  ObLCNodeFactory(ObPlanCache *lib_cache)
    : lib_cache_(lib_cache)
  {
  }
  void set_lib_cache(ObPlanCache *lib_cache) { lib_cache_ = lib_cache; }
  void destroy_cache_node(ObILibCacheNode* node);
  int create_cache_node(ObLibCacheNameSpace ns,
                        ObILibCacheNode*& node,
                        uint64_t tenant_id,
                        lib::MemoryContext &parent_context);
  template<typename ClassT>
  static int create(lib::MemoryContext &mem_ctx, ObILibCacheNode*& node, ObPlanCache *lib_cache);

private:
  ObPlanCache *lib_cache_;
};

template<typename ClassT>
int ObLCNodeFactory::create(lib::MemoryContext &mem_ctx,
                            ObILibCacheNode*& node,
                            ObPlanCache *lib_cache)
{
  int ret = OB_SUCCESS;
  char *ptr = NULL;
  if (OB_ISNULL(lib_cache)) {
    ret = OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(ret));
  } else if (NULL == (ptr = (char *)mem_ctx->get_arena_allocator().alloc(sizeof(ClassT)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    OB_LOG(WARN, "failed to allocate memory for lib cache node", K(ret));
  } else {
    node = new(ptr)ClassT(lib_cache, mem_ctx);
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_PLAN_CACHE_OB_LIB_CACHE_NODE_FACTORY_
