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

#include "rootserver/freeze/ob_major_freeze_rpc_define.h"
#include "rootserver/freeze/ob_major_freeze_service.h"

namespace oceanbase
{
namespace obcall
{

OB_SERIALIZE_MEMBER(ObSimpleFreezeInfo);

OB_SERIALIZE_MEMBER(ObMajorFreezeRequest, info_, freeze_reason_);

OB_SERIALIZE_MEMBER(ObMajorFreezeResponse, err_code_);

OB_SERIALIZE_MEMBER(ObTenantAdminMergeRequest, type_);

OB_SERIALIZE_MEMBER(ObTenantAdminMergeResponse, err_code_);

} // namespace obcall
} // namespace oceanbase
