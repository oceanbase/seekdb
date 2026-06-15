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

#include "ob_net_keepalive_adapter.h"
#include "lib/net/ob_addr.h"
#include "lib/time/ob_time_utility.h"

// Single-replica seekdb: the obcall net-keepalive blacklist is gone. The only
// peer is the local server, which is always alive and never blacklisted, so
// these checks are trivially "never blacklisted / never stopped".
namespace oceanbase
{
namespace logservice
{
ObNetKeepAliveAdapter::ObNetKeepAliveAdapter()
{
}

ObNetKeepAliveAdapter::~ObNetKeepAliveAdapter()
{
}

int ObNetKeepAliveAdapter::in_black_or_stopped_(const common::ObAddr &server,
                                              bool &in_blacklist,
                                              bool &is_server_stopped)
{
  int ret = OB_SUCCESS;
  in_blacklist = false;
  is_server_stopped = false;
  if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

bool ObNetKeepAliveAdapter::in_black_or_stopped(const common::ObAddr &server)
{
  UNUSED(server);
  return false;
}

bool ObNetKeepAliveAdapter::is_server_stopped(const common::ObAddr &server)
{
  UNUSED(server);
  return false;
}

bool ObNetKeepAliveAdapter::in_black(const common::ObAddr &server)
{
  UNUSED(server);
  return false;
}

int ObNetKeepAliveAdapter::get_last_resp_ts(const common::ObAddr &server,
                                            int64_t &last_resp_ts)
{
  int ret = OB_SUCCESS;
  if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    last_resp_ts = common::ObTimeUtility::current_time();
  }
  return ret;
}
} // end namespace logservice
} // end namespace oceanbase
