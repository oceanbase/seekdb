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

#include "ob_gts_msg.h"
#include "ob_gts_rpc.h"

namespace oceanbase
{
using namespace common;
using namespace obrpc;

namespace transaction
{
// ObGtsRequest
OB_SERIALIZE_MEMBER(ObGtsRequest, tenant_id_, srr_.mts_, range_size_, sender_);
// ObGtsErrResponse
OB_SERIALIZE_MEMBER(ObGtsErrResponse, tenant_id_, srr_.mts_, status_, sender_);

int ObGtsRequest::init(const uint64_t tenant_id, const MonotonicTs srr, const int64_t range_size,
    const ObAddr &sender)
{
  int ret = OB_SUCCESS;
  if (!is_valid_tenant_id(tenant_id) || !srr.is_valid() || 0 >= range_size || !sender.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(srr), K(range_size), K(sender));
  } else {
    tenant_id_ = tenant_id;
    srr_ = srr;
    range_size_ = range_size;
    sender_ = sender;
  }
  return ret;
}

bool ObGtsRequest::is_valid() const
{
  return is_valid_tenant_id(tenant_id_) && srr_.is_valid() && range_size_ > 0 &&
    sender_.is_valid();
}

//leader may be invalid, the validity check does not need to check this field
int ObGtsErrResponse::init(const uint64_t tenant_id, const MonotonicTs srr, const int status,
    const ObAddr &sender)
{
  int ret = OB_SUCCESS;
  if (!is_valid_tenant_id(tenant_id) || !srr.is_valid() || !sender.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tenant_id), K(srr), K(status), K(sender));
  } else {
    tenant_id_ = tenant_id;
    srr_ = srr;
    status_ = status;
    sender_ = sender;
  }
  return ret;
}

bool ObGtsErrResponse::is_valid() const
{
  return is_valid_tenant_id(tenant_id_) && srr_.is_valid() && OB_SUCCESS != status_ && sender_.is_valid();
}

} // transaction
} // oceanbase
