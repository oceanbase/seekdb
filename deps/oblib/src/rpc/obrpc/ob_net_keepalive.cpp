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

#define USING_LOG_PREFIX RPC_OBRPC
#include "ob_net_keepalive.h"
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include "lib/thread/ob_thread_name.h"
#include "lib/utility/utility.h"
#include "lib/ash/ob_active_session_guard.h"

using namespace oceanbase::common;
using namespace oceanbase::common::serialization;
using namespace oceanbase::lib;
namespace oceanbase
{
namespace obrpc
{
void __attribute__((weak)) keepalive_make_data(ObNetKeepAliveData &ka_data)
{
  // do-nothing
}

ObNetKeepAlive &ObNetKeepAlive::get_instance()
{
  static ObNetKeepAlive the_one;
  return the_one;
}

int ObNetKeepAlive::in_black(const common::ObAddr &addr, bool &in_blacklist, ObNetKeepAliveData *ka_data)
{
  int ret = OB_SUCCESS;
  in_blacklist = false;
  if (ka_data != nullptr) {
    keepalive_make_data(*ka_data);
  }
  return ret;
}

int ObNetKeepAlive::get_last_resp_ts(const common::ObAddr &addr, int64_t &last_resp_ts)
{
  int ret = OB_SUCCESS;
  last_resp_ts = ObTimeUtility::current_time();
  return ret;
}

}//end of namespace obrpc
}//end of namespace oceanbase
