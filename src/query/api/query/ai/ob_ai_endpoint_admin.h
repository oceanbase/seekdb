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

#ifndef OCEANBASE_QUERY_AI_OB_AI_ENDPOINT_ADMIN_H_
#define OCEANBASE_QUERY_AI_OB_AI_ENDPOINT_ADMIN_H_

namespace oceanbase
{
namespace common
{
class ObArenaAllocator;
class ObIJsonBase;
class ObString;
}
namespace query
{

// Query-owned seam for endpoint mutations whose adapter is hosted by
// Observer. PL depends on this interface, never on the Observer executor.
class ObIAiEndpointAdmin
{
public:
  virtual ~ObIAiEndpointAdmin() = default;
  virtual int create_endpoint(
      common::ObArenaAllocator &allocator,
      const common::ObString &endpoint_name,
      const common::ObIJsonBase &definition) = 0;
  virtual int alter_endpoint(
      common::ObArenaAllocator &allocator,
      const common::ObString &endpoint_name,
      const common::ObIJsonBase &definition) = 0;
  virtual int drop_endpoint(const common::ObString &endpoint_name) = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_AI_OB_AI_ENDPOINT_ADMIN_H_
