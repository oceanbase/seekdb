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

#ifndef OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_ACCESS_CONTEXT_H_
#define OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_ACCESS_CONTEXT_H_

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObILobAccessContext;
}
namespace data_plane
{

// Query may retain and pass this opaque handle, but the data plane owns its
// construction, destruction, and layout.
int create_lob_access_context(
    common::ObIAllocator &allocator,
    common::ObILobAccessContext *&context);
void destroy_lob_access_context(common::ObILobAccessContext *&context);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_ACCESS_CONTEXT_H_
