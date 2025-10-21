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

#ifndef SRC_PL_OB_PL_PACKAGE_GUARD_H_
#define SRC_PL_OB_PL_PACKAGE_GUARD_H_

#include "sql/plan_cache/ob_cache_object_factory.h"
#include "observer/ob_req_time_service.h"

namespace oceanbase
{

namespace pl
{
class ObPLPackageGuard
{
public:
  ObPLPackageGuard(uint64_t tenant_id)
    : alloc_(),
      req_time_guard_()
  {
    lib::ObMemAttr attr;
    attr.label_ = "PLPKGGuard";
    attr.tenant_id_ = tenant_id;
    attr.ctx_id_ = common::ObCtxIds::EXECUTE_CTX_ID;
    alloc_.set_attr(attr);
  }
  virtual ~ObPLPackageGuard();

  int init();
  inline bool is_inited() { return map_.created(); }
  inline int put(uint64_t package_id, sql::ObCacheObjGuard *package)
  {
    return map_.set_refactored(package_id, package);
  }
  inline int get(uint64_t package_id, sql::ObCacheObjGuard *&package)
  {
    return map_.get_refactored(package_id, package);
  }
  common::ObArenaAllocator alloc_;
private:
  common::hash::ObHashMap<uint64_t, sql::ObCacheObjGuard*> map_;
  observer::ObReqTimeGuard req_time_guard_;
};

}
}
#endif
