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

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_LIFECYCLE_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_LIFECYCLE_H_

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace transaction
{
class ObTxDesc;
}
namespace data_plane
{

// Transaction descriptors are owned by the data plane. Query may keep a
// borrowed/duplicated handle, but must not reach into ObTransService to manage
// that handle's lifetime.
int clone_tx_desc(common::ObIAllocator &allocator,
                  transaction::ObTxDesc *source,
                  transaction::ObTxDesc *&clone);
void release_tx_desc(transaction::ObTxDesc *&desc);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_LIFECYCLE_H_
