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
#ifndef OCEANBASE_SHARE_OBJECT_OB_OBJ_CAST_HOOKS_H_
#define OCEANBASE_SHARE_OBJECT_OB_OBJ_CAST_HOOKS_H_
#include <stdint.h>
// obj_cast dependency hooks for upper-layer services:share declaration,owner modules register during static initialization。
// Purpose: remove the share/object inverted dependency on upper-layer SRS and runtime configuration.
namespace oceanbase
{
namespace common
{
class ObSrsItem;

// geo-cast SRS lookup(registered by observer/omt/ob_srs_service.cpp;
// type-erased holder keeps omt::ObSrsCacheGuard to guarantee the SRS lifetime)
struct ObSrsGuardErased
{
  ObSrsGuardErased() : impl_(nullptr), release_(nullptr) {}
  ~ObSrsGuardErased() { if (nullptr != release_) { release_(impl_); } }
  void *impl_;
  void (*release_)(void *);
};
typedef int (*ObObjCastGetSrsItemFn)(uint64_t srid, const ObSrsItem *&srs, ObSrsGuardErased &guard);
extern ObObjCastGetSrsItemFn g_obj_cast_get_srs_item;

// JSON parse max depth is registered by sql/ob_expr_json_func_helper.cpp;
// falls back to JSON_DOCUMENT_MAX_DEPTH when unregistered)
typedef int32_t (*ObObjCastJsonMaxDepthFn)();
extern ObObjCastJsonMaxDepthFn g_obj_cast_json_max_depth;

}  // namespace common
}  // namespace oceanbase
#endif
