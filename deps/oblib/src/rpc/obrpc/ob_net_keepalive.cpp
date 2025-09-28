/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
