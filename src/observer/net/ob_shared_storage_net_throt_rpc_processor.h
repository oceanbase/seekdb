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

#ifndef OCEANBASE_SHARED_STORAGE_NET_THROT_RPC_PROCESSOR_H_
#define OCEANBASE_SHARED_STORAGE_NET_THROT_RPC_PROCESSOR_H_

#include "observer/net/ob_shared_storage_net_throt_rpc_proxy.h"
#include "observer/ob_rpc_processor_simple.h"
namespace oceanbase
{
namespace observer
{
OB_DEFINE_PROCESSOR_S(SSNT, OB_SHARED_STORAGE_NET_THROT_REGISTER, ObSharedStorageNetThrotRegisterP);
OB_DEFINE_PROCESSOR_S(Srv, OB_SHARED_STORAGE_NET_THROT_PREDICT, ObSharedStorageNetThrotPredictP);
OB_DEFINE_PROCESSOR_S(Srv, OB_SHARED_STORAGE_NET_THROT_SET, ObSharedStorageNetThrotSetP);

}  // namespace observer
}  // namespace oceanbase

#endif /* OCEANBASE_SHARED_STORAGE_NET_THROT_RPC_PROCESSOR_H_ */
