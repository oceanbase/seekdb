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

#ifndef OCEANBASE_OBRPC_OB_LISTENER_H_
#define OCEANBASE_OBRPC_OB_LISTENER_H_
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lib/thread/threads.h"

using namespace oceanbase::lib;

namespace oceanbase
{
namespace obrpc
{
#define NEGOTIATION_PACKET_HEADER_MAGIC_EASY (0x1234567877668833)

#define MAX_PROTOCOL_TYPE_SIZE (5)
#define OB_LISTENER_MAX_THREAD_CNT         64

typedef struct io_threads_pipefd_pool_t{
    int count;
    int pipefd[OB_LISTENER_MAX_THREAD_CNT];
}io_threads_pipefd_pool_t;

typedef struct io_wrpipefd_map_t{
  uint8_t  used;
  uint64_t magic;
  io_threads_pipefd_pool_t ioth_wrpipefd_pool;
}io_wrpipefd_map_t;

class ObListener : public lib::Threads
{
public:
  ObListener();
  ~ObListener();
  void run(int64_t idx);
  int ob_listener_set_tcp_opt(int fd, int option, int value);
  int ob_listener_set_opt(int fd, int option, int value);
  int listen_start();
  void set_port(int port) {port_ = port;}

private:
  void do_work();
private:
  int listen_fd_;
  int port_;
  io_wrpipefd_map_t io_wrpipefd_map_[MAX_PROTOCOL_TYPE_SIZE];
};

}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* OCEANBASE_OBRPC_OB_LISTENER_H_ */
