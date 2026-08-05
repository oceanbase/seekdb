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

#ifndef OCEANBASE_SHARE_OB_LIB_CACHE_NAMESPACE_H_
#define OCEANBASE_SHARE_OB_LIB_CACHE_NAMESPACE_H_

namespace oceanbase
{
namespace sql
{

enum ObLibCacheNameSpace
{
  NS_INVALID,
#define LIB_CACHE_OBJ_DEF(ns, ns_name, ck_class, cn_class, co_class, label) ns,
#include "share/ob_lib_cache_namespace.def"
#undef LIB_CACHE_OBJ_DEF
  NS_MAX
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_LIB_CACHE_NAMESPACE_H_
