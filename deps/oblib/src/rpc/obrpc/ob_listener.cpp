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

#include "rpc/obrpc/ob_listener.h"
#include "lib/net/ob_net_util.h"
#include <sys/epoll.h>

using namespace oceanbase::common;
using namespace oceanbase::obrpc;
using namespace oceanbase::common::serialization;

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

ObListener::ObListener()
{
  listen_fd_ = -1;
  memset(&io_wrpipefd_map_, 0, sizeof(io_wrpipefd_map_));
}

ObListener::~ObListener()
{
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
}

int ObListener::ob_listener_set_tcp_opt(int fd, int option, int value)
{
    return setsockopt(fd, IPPROTO_TCP, option, (const void *) &value, sizeof(value));
}

int ObListener::ob_listener_set_opt(int fd, int option, int value)
{
    return setsockopt(fd, SOL_SOCKET, option, (void *)&value, sizeof(value));
}



void ObListener::run(int64_t idx)
{
  UNUSED(idx);
  listen_start();
}

int ObListener::listen_start()
{
  int ret = OB_SUCCESS;
  this->do_work();
  return ret;
}



/*
* easy negotiation packet format
PACKET HEADER:
+------------------------------------------------------------------------+
|         negotiation packet header magic(8B)  | msg body len (2B)
+------------------------------------------------------------------------+

PACKET MSG BODY:
+------------------------------------------------------------------------+
|   io thread corresponding eio magic(8B) |  io thread index (1B)
+------------------------------------------------------------------------+
*/




void ObListener::do_work()
{
  return;
}


