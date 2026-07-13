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

#ifndef OCEANBASE_SQL_OB_ROUTE_POLICY_H
#define OCEANBASE_SQL_OB_ROUTE_POLICY_H

namespace oceanbase
{
namespace sql
{

enum ObRoutePolicyType
{
  INVALID_POLICY = 0,
  READONLY_ZONE_FIRST = 1,
  ONLY_READONLY_ZONE = 2,
  UNMERGE_ZONE_FIRST = 3,
  UNMERGE_FOLLOWER_FIRST = 4,
  FORCE_READONLY_ZONE = 5,
  POLICY_TYPE_MAX
};

} // namespace sql
} // namespace oceanbase

#endif
