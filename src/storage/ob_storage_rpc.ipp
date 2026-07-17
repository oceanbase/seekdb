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

#ifndef OCEANBASE_STORAGE_RPC_IPP_
#define OCEANBASE_STORAGE_RPC_IPP_

// ObStorageStreamRpcReader<> impls and do_fetch_next_buffer_if_need<> helper
// deleted — they depended on the obcall stream-RPC framework (SSHandle) and are
// dead in seekdb (single-replica; HA/migration is gRPC).

#endif // OCEANBASE_STORAGE_RPC_IPP_
