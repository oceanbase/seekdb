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

#include "ob_blacklist_proxy.h"

namespace oceanbase
{
using namespace common;

namespace obcall
{
OB_SERIALIZE_MEMBER(ObBlacklistReq, sender_, send_timestamp_);
OB_SERIALIZE_MEMBER(ObBlacklistResp, sender_, req_send_timestamp_, req_recv_timestamp_, server_start_time_);

void ObBlacklistReq::reset()
{
  sender_.reset();
  send_timestamp_ = OB_INVALID_TIMESTAMP;
}

void ObBlacklistResp::reset()
{
  sender_.reset();
  req_send_timestamp_ = OB_INVALID_TIMESTAMP;
  req_recv_timestamp_ = OB_INVALID_TIMESTAMP;
  server_start_time_ = 0;
}

};
};
