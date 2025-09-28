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


