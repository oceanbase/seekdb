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

#ifndef OCEANBASE_SHARE_VECTOR_OB_VECTOR_INDEX_MODE_H_
#define OCEANBASE_SHARE_VECTOR_OB_VECTOR_INDEX_MODE_H_

namespace oceanbase
{
namespace common
{
class ObString;
}
namespace share
{

// Interprets the stable SYNC_MODE portion of a vector index parameter string.
bool is_vector_index_sync_mode_async(
    const common::ObString &index_params,
    bool is_hnsw_heap_table = false);

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_VECTOR_OB_VECTOR_INDEX_MODE_H_
