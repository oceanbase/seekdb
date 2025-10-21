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

#include "ob_net_endpoint_ingress_rpc_struct.h"
#include "observer/ob_srv_network_frame.h"

#define USING_LOG_PREFIX RPC

namespace oceanbase
{
using namespace common;

namespace obrpc
{

OB_SERIALIZE_MEMBER(ObNetEndpointKey, addr_, group_id_);
OB_SERIALIZE_MEMBER(ObNetEndpointValue, predicted_bw_, assigned_bw_, expire_time_);

OB_SERIALIZE_MEMBER(ObNetEndpointRegisterArg, endpoint_key_, expire_time_);

OB_SERIALIZE_MEMBER(ObNetEndpointPredictIngressArg, endpoint_key_);
int ObNetEndpointPredictIngressArg::assign(const ObNetEndpointPredictIngressArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(endpoint_key_.assign(other.endpoint_key_))) {
    LOG_WARN("fail to assign endpoint_key", KR(ret));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObNetEndpointPredictIngressRes, predicted_bw_);

OB_SERIALIZE_MEMBER(ObNetEndpointSetIngressArg, endpoint_key_, assigned_bw_);
int ObNetEndpointSetIngressArg::assign(const ObNetEndpointSetIngressArg &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(endpoint_key_.assign(other.endpoint_key_))) {
    LOG_WARN("fail to assign endpoint_key", KR(ret));
  } else {
    assigned_bw_ = other.assigned_bw_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObNetEndpointSetIngressRes, res_);
}  // namespace obrpc
}  // namespace oceanbase
