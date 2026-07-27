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

#define USING_LOG_PREFIX SHARE_LOCATION

#include "share/location_cache/ob_location_struct.h"

namespace oceanbase
{
using namespace common;
namespace share
{

OB_SERIALIZE_MEMBER(ObLSLocation,
                    ls_id_,
                    server_,
                    renew_time_);

ObLSLocation::ObLSLocation()
  : ls_id_(),
    server_(),
    renew_time_(0)
{
}

int ObLSLocation::init(const ObLSID &ls_id,
                       const ObAddr &server,
                       const int64_t renew_time)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!ls_id.is_valid()
                  || !server.is_valid()
                  || renew_time <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid LS location", KR(ret), K(ls_id), K(server), K(renew_time));
  } else {
    ls_id_ = ls_id;
    server_ = server;
    renew_time_ = renew_time;
  }
  return ret;
}

void ObLSLocation::reset()
{
  ls_id_.reset();
  server_.reset();
  renew_time_ = 0;
}

bool ObLSLocation::is_valid() const
{
  return ls_id_.is_valid() && server_.is_valid() && renew_time_ > 0;
}

bool ObLSLocation::operator==(const ObLSLocation &other) const
{
  return ls_id_ == other.ls_id_
      && server_ == other.server_
      && renew_time_ == other.renew_time_;
}

} // end namespace share
} // end namespace oceanbase
