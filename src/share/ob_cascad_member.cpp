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

#include "ob_cascad_member.h"

namespace oceanbase
{
using namespace common;

namespace share
{
ObCascadMember::ObCascadMember() : server_(),
                                   cluster_id_(common::INVALID_CLUSTER_ID)
{}

ObCascadMember::ObCascadMember(const common::ObAddr &server,
                               const int64_t cluster_id)
    : server_(server),
      cluster_id_(cluster_id)
{}

OB_SERIALIZE_MEMBER(ObCascadMember, server_, cluster_id_);

void ObCascadMember::reset()
{
  server_.reset();
  cluster_id_ = common::INVALID_CLUSTER_ID;
}

ObCascadMember &ObCascadMember::operator=(const ObCascadMember &rhs)
{
  server_ = rhs.server_;
  cluster_id_ = rhs.cluster_id_;
  return *this;
}

bool ObCascadMember::is_valid() const
{
  return server_.is_valid();
}


int64_t ObCascadMember::hash() const
{
  return (server_.hash() | (cluster_id_ << 32));
}

} // namespace share
} // namespace oceanbase
