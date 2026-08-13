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

#ifndef OCEANBASE_SHARE_CACHE_OB_CACHE_TYPE_H_
#define OCEANBASE_SHARE_CACHE_OB_CACHE_TYPE_H_

typedef enum ObCacheType
{
  CACHE_TYPE_INVALID = -1,
  CACHE_TYPE_PLAN,
  CACHE_TYPE_PL_OBJ,
  CACHE_TYPE_PS_OBJ,
  CACHE_TYPE_LIB_CACHE,
  CACHE_TYPE_MAX,
} ObCacheType;

#endif // OCEANBASE_SHARE_CACHE_OB_CACHE_TYPE_H_
