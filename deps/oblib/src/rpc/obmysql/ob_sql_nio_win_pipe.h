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

#ifndef OCEANBASE_OBMYSQL_OB_SQL_NIO_WIN_PIPE_H_
#define OCEANBASE_OBMYSQL_OB_SQL_NIO_WIN_PIPE_H_

#ifdef _WIN32

#include <windows.h>
#include <winsock2.h>
#include <vector>
#include <algorithm>

namespace oceanbase
{
namespace obmysql
{

class ObSqlNioImpl;

enum WinPipeOpType {
  OP_ACCEPT,      // ConnectNamedPipe complete -- new client connection
  OP_PIPE_READ,   // ReadFile(pipe) complete -- pipe data arrived
  OP_PIPE_WRITE,  // WriteFile(pipe) complete -- data written to pipe
};

struct WinPipeBridge;

struct WinPipeOverlapped {
  OVERLAPPED ov;                // must be first field for OVERLAPPED pointer interop
  WinPipeOpType op_type;
  struct WinPipeBridge* bridge; // owning connection (NULL for OP_ACCEPT)
};

struct WinPipeBridge {
  HANDLE pipe_handle;           // Named Pipe handle
  SOCKET sock_a;                // end injected into WSAPoll (held by ObSqlSock)
  SOCKET sock_b;                // end read/written by IOCP thread
  WinPipeOverlapped ov_read;    // pipe read overlapped
  WinPipeOverlapped ov_write;   // pipe write overlapped
  char read_buf[32768];         // pipe -> sockB read buffer
  char* write_buf;              // pending write buffer (freed on write completion)
  bool closing;
  int pending_io;
};

class ObWinNamedPipeServer
{
public:
  ObWinNamedPipeServer();
  ~ObWinNamedPipeServer();

  int init(const char* pipe_name, ObSqlNioImpl* nio_impl);
  void stop();
  void thread_func();

private:
  int create_pipe_and_accept();
  void on_accept(WinPipeOverlapped* ov);
  void on_pipe_read(WinPipeBridge* bridge, DWORD bytes);
  void on_pipe_write(WinPipeBridge* bridge, DWORD bytes);
  void check_sockb_readable();
  void destroy_bridge(WinPipeBridge* bridge);
  void reap_bridge(WinPipeBridge* bridge);
  void reap_pending();

  HANDLE iocp_;
  char pipe_name_[256];
  HANDLE pending_pipe_;         // current pipe instance waiting for accept
  WinPipeOverlapped accept_ov_; // overlapped for accept
  ObSqlNioImpl* nio_impl_;     // for inject_accepted_fd
  std::vector<WinPipeBridge*> bridges_;
  std::vector<WinPipeBridge*> pending_reaps_;
  bool stopped_;
};

} // end namespace obmysql
} // end namespace oceanbase

#endif // _WIN32

#endif /* OCEANBASE_OBMYSQL_OB_SQL_NIO_WIN_PIPE_H_ */