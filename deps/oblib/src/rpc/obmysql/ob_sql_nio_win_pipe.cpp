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

#ifdef _WIN32

#define USING_LOG_PREFIX RPC_OBMYSQL
#include "rpc/obmysql/ob_sql_nio_win_pipe.h"
#include "rpc/obmysql/ob_sql_nio.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace obmysql
{

ObWinNamedPipeServer::ObWinNamedPipeServer()
    : iocp_(NULL), pending_pipe_(INVALID_HANDLE_VALUE), nio_impl_(NULL), stopped_(true)
{
  memset(pipe_name_, 0, sizeof(pipe_name_));
  memset(&accept_ov_, 0, sizeof(accept_ov_));
}

ObWinNamedPipeServer::~ObWinNamedPipeServer()
{
  stop();
}

int ObWinNamedPipeServer::init(const char* pipe_name, ObSqlNioImpl* nio_impl)
{
  int ret = OB_SUCCESS;
  if (NULL == pipe_name || NULL == nio_impl) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(pipe_name), KP(nio_impl));
  } else {
    strncpy(pipe_name_, pipe_name, sizeof(pipe_name_) - 1);
    nio_impl_ = nio_impl;
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (NULL == iocp_) {
      ret = OB_IO_ERROR;
      LOG_WARN("CreateIoCompletionPort fail", K(ret), K(GetLastError()));
    } else {
      stopped_ = false;
      LOG_INFO("named pipe server init succ", K(pipe_name_));
    }
  }
  return ret;
}

void ObWinNamedPipeServer::stop()
{
  if (!stopped_) {
    stopped_ = true;
    // Cancel all pending I/O on the accept pipe
    if (pending_pipe_ != INVALID_HANDLE_VALUE) {
      CancelIoEx(pending_pipe_, &accept_ov_.ov);
    }
    // Cancel I/O on all bridge pipes
    for (auto* bridge : bridges_) {
      bridge->closing = true;
      CancelIoEx(bridge->pipe_handle, &bridge->ov_read.ov);
      CancelIoEx(bridge->pipe_handle, &bridge->ov_write.ov);
    }
    // Wait briefly for IOCP to drain, then clean up
    Sleep(200);
    for (auto* bridge : bridges_) {
      destroy_bridge(bridge);
    }
    bridges_.clear();
    if (pending_pipe_ != INVALID_HANDLE_VALUE) {
      CloseHandle(pending_pipe_);
      pending_pipe_ = INVALID_HANDLE_VALUE;
    }
    if (NULL != iocp_) {
      CloseHandle(iocp_);
      iocp_ = NULL;
    }
  }
}

void ObWinNamedPipeServer::thread_func()
{
  int ret = create_pipe_and_accept();
  if (OB_FAIL(ret)) {
    LOG_WARN("create pipe and accept fail", K(ret));
    return;
  }

  while (!stopped_) {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* ov = NULL;
    BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, 100);
    if (!ok && ov != NULL) {
      // Failed I/O completion
      auto* pov = reinterpret_cast<WinPipeOverlapped*>(ov);
      DWORD err = GetLastError();
      LOG_WARN("IOCP completion error", K(err), K(pov->op_type));
      if (pov->op_type == OP_PIPE_READ && pov->bridge != NULL) {
        destroy_bridge(pov->bridge);
      }
      continue;
    }
    if (ov != NULL) {
      auto* pov = reinterpret_cast<WinPipeOverlapped*>(ov);
      switch (pov->op_type) {
        case OP_ACCEPT:     on_accept(pov); break;
        case OP_PIPE_READ:  on_pipe_read(pov->bridge, bytes); break;
        case OP_PIPE_WRITE: on_pipe_write(pov->bridge, bytes); break;
      }
    }
    check_sockb_readable();
  }
}

int ObWinNamedPipeServer::create_pipe_and_accept()
{
  int ret = OB_SUCCESS;
  HANDLE hPipe = CreateNamedPipeA(
      pipe_name_,
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      PIPE_UNLIMITED_INSTANCES,
      32768,  // output buffer size
      32768,  // input buffer size
      0,      // default timeout
      NULL);  // default security
  if (INVALID_HANDLE_VALUE == hPipe) {
    ret = OB_IO_ERROR;
    LOG_WARN("CreateNamedPipeA fail", K(ret), K(GetLastError()), K(pipe_name_));
  } else {
    // Associate the pipe handle with IOCP BEFORE ConnectNamedPipe,
    // otherwise completion notifications won't be delivered.
    if (NULL == CreateIoCompletionPort(hPipe, iocp_, 0, 0)) {
      ret = OB_IO_ERROR;
      LOG_WARN("CreateIoCompletionPort associate fail for accept pipe", K(ret), K(GetLastError()));
      CloseHandle(hPipe);
    } else {
      pending_pipe_ = hPipe;
      memset(&accept_ov_, 0, sizeof(accept_ov_));
      accept_ov_.op_type = OP_ACCEPT;
      accept_ov_.bridge = NULL;
      BOOL ok = ConnectNamedPipe(hPipe, &accept_ov_.ov);
      if (!ok) {
        DWORD err = GetLastError();
        if (ERROR_PIPE_CONNECTED == err) {
          // Client already connected before ConnectNamedPipe was called.
          // Post a completion packet manually so on_accept gets called.
          PostQueuedCompletionStatus(iocp_, 0, 0, &accept_ov_.ov);
        } else if (ERROR_IO_PENDING != err) {
          ret = OB_IO_ERROR;
          LOG_WARN("ConnectNamedPipe fail", K(ret), K(err));
          CloseHandle(hPipe);
          pending_pipe_ = INVALID_HANDLE_VALUE;
        }
      }
    }
  }
  return ret;
}

void ObWinNamedPipeServer::on_accept(WinPipeOverlapped* ov)
{
  int ret = OB_SUCCESS;
  HANDLE connected_pipe = pending_pipe_;
  pending_pipe_ = INVALID_HANDLE_VALUE;

  LOG_INFO("named pipe on_accept", KP(connected_pipe));

  // IOCP association was already done in create_pipe_and_accept()

  // Create loopback socket pair (sockA, sockB)
  SOCKET sock_a = INVALID_SOCKET;
  SOCKET sock_b = INVALID_SOCKET;
  SOCKET lstn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (INVALID_SOCKET == lstn) {
    LOG_WARN("socket fail", K(WSAGetLastError()));
    CloseHandle(connected_pipe);
    create_pipe_and_accept();
    return;
  }
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(lstn, (sockaddr*)&addr, sizeof(addr)) != 0 ||
      listen(lstn, 1) != 0) {
    LOG_WARN("bind/listen fail", K(WSAGetLastError()));
    closesocket(lstn);
    CloseHandle(connected_pipe);
    create_pipe_and_accept();
    return;
  }
  int alen = sizeof(addr);
  getsockname(lstn, (sockaddr*)&addr, &alen);
  sock_b = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (INVALID_SOCKET == sock_b) {
    LOG_WARN("socket fail", K(WSAGetLastError()));
    closesocket(lstn);
    CloseHandle(connected_pipe);
    create_pipe_and_accept();
    return;
  }
  if (connect(sock_b, (sockaddr*)&addr, sizeof(addr)) != 0) {
    LOG_WARN("connect fail", K(WSAGetLastError()));
    closesocket(sock_b);
    closesocket(lstn);
    CloseHandle(connected_pipe);
    create_pipe_and_accept();
    return;
  }
  sock_a = accept(lstn, NULL, NULL);
  closesocket(lstn);
  if (INVALID_SOCKET == sock_a) {
    LOG_WARN("accept fail", K(WSAGetLastError()));
    closesocket(sock_b);
    CloseHandle(connected_pipe);
    create_pipe_and_accept();
    return;
  }

  // Set sock_a non-blocking (will be managed by WSAPoll)
  u_long mode = 1;
  ioctlsocket(sock_a, FIONBIO, &mode);
  // Set sock_b non-blocking (IOCP thread reads via non-blocking recv)
  ioctlsocket(sock_b, FIONBIO, &mode);

  // Allocate bridge
  WinPipeBridge* bridge = (WinPipeBridge*)ob_malloc(sizeof(WinPipeBridge), "WinPipeBridge");
  if (NULL == bridge) {
    LOG_WARN("alloc bridge fail");
    closesocket(sock_a);
    closesocket(sock_b);
    CloseHandle(connected_pipe);
    create_pipe_and_accept();
    return;
  }
  new (bridge) WinPipeBridge();
  bridge->pipe_handle = connected_pipe;
  bridge->sock_a = sock_a;
  bridge->sock_b = sock_b;
  bridge->closing = false;
  bridge->write_buf = NULL;
  memset(&bridge->ov_read, 0, sizeof(bridge->ov_read));
  bridge->ov_read.op_type = OP_PIPE_READ;
  bridge->ov_read.bridge = bridge;
  memset(&bridge->ov_write, 0, sizeof(bridge->ov_write));
  bridge->ov_write.op_type = OP_PIPE_WRITE;
  bridge->ov_write.bridge = bridge;
  memset(bridge->read_buf, 0, sizeof(bridge->read_buf));

  bridges_.push_back(bridge);

  // Inject sock_a into the existing WSAPoll event loop as a unix-socket connection
  nio_impl_->inject_accepted_fd((int)sock_a, true);
  LOG_INFO("named pipe bridge created", K(bridge->sock_a), K(bridge->sock_b));

  // Start overlapped read on the pipe
  BOOL ok = ReadFile(bridge->pipe_handle, bridge->read_buf, sizeof(bridge->read_buf), NULL, &bridge->ov_read.ov);
  if (!ok) {
    DWORD err = GetLastError();
    if (ERROR_IO_PENDING != err) {
      LOG_WARN("ReadFile fail on pipe", K(err));
      bridges_.erase(std::remove(bridges_.begin(), bridges_.end(), bridge), bridges_.end());
      destroy_bridge(bridge);
    }
  }

  // Create next pipe instance for the next client
  if (OB_FAIL(create_pipe_and_accept())) {
    LOG_WARN("create_pipe_and_accept fail after on_accept", K(ret));
  }
}

void ObWinNamedPipeServer::on_pipe_read(WinPipeBridge* bridge, DWORD bytes)
{
  int ret = OB_SUCCESS;
  if (NULL == bridge || bridge->closing) return;
  if (0 == bytes) {
    // Client disconnected
    bridges_.erase(std::remove(bridges_.begin(), bridges_.end(), bridge), bridges_.end());
    destroy_bridge(bridge);
    return;
  }
  // Forward pipe data to sockB -> sockA becomes readable -> WSAPoll notifies ObSqlSock
  int sent = send(bridge->sock_b, bridge->read_buf, bytes, 0);
  if (sent < 0) {
    int wsa_err = WSAGetLastError();
    if (WSAEWOULDBLOCK != wsa_err) {
      LOG_WARN("send to sockB fail", K(wsa_err));
      bridges_.erase(std::remove(bridges_.begin(), bridges_.end(), bridge), bridges_.end());
      destroy_bridge(bridge);
      return;
    }
    // WSAEWOULDBLOCK: sockB buffer full, retry later - drop data for now
    LOG_WARN("sockB send would block, data may be lost");
  }
  // Start next overlapped read
  memset(&bridge->ov_read.ov, 0, sizeof(OVERLAPPED));
  bridge->ov_read.op_type = OP_PIPE_READ;
  BOOL ok = ReadFile(bridge->pipe_handle, bridge->read_buf, sizeof(bridge->read_buf), NULL, &bridge->ov_read.ov);
  if (!ok) {
    DWORD err = GetLastError();
    if (ERROR_IO_PENDING != err) {
      LOG_WARN("ReadFile fail on pipe", K(err));
      bridges_.erase(std::remove(bridges_.begin(), bridges_.end(), bridge), bridges_.end());
      destroy_bridge(bridge);
    }
  }
}

void ObWinNamedPipeServer::on_pipe_write(WinPipeBridge* bridge, DWORD bytes)
{
  if (NULL != bridge && NULL != bridge->write_buf) {
    ob_free(bridge->write_buf);
    bridge->write_buf = NULL;
  }
}

void ObWinNamedPipeServer::check_sockb_readable()
{
  int ret = OB_SUCCESS;
  char buf[32768];
  for (auto it = bridges_.begin(); it != bridges_.end(); ) {
    WinPipeBridge* bridge = *it;
    if (bridge->closing) {
      ++it;
      continue;
    }
    // Skip if a write is already pending
    if (NULL != bridge->write_buf) {
      ++it;
      continue;
    }
    // Non-blocking recv from sockB to check if ObSqlSock wrote data
    int n = recv(bridge->sock_b, buf, sizeof(buf), 0);
    if (n > 0) {
      // Write to pipe via overlapped WriteFile
      memset(&bridge->ov_write.ov, 0, sizeof(OVERLAPPED));
      bridge->ov_write.op_type = OP_PIPE_WRITE;
      // Copy data to a heap buffer since overlapped write needs stable buffer
      bridge->write_buf = (char*)ob_malloc(n, "WinPipeWrite");
      if (NULL != bridge->write_buf) {
        memcpy(bridge->write_buf, buf, n);
        BOOL ok = WriteFile(bridge->pipe_handle, bridge->write_buf, n, NULL, &bridge->ov_write.ov);
        if (!ok) {
          DWORD err = GetLastError();
          if (ERROR_IO_PENDING != err) {
            LOG_WARN("WriteFile fail on pipe", K(err));
            ob_free(bridge->write_buf);
            bridge->write_buf = NULL;
            destroy_bridge(bridge);
            it = bridges_.erase(it);
            continue;
          }
        }
      }
    } else if (n == 0) {
      // sockB closed -> ObSqlSock disconnected
      destroy_bridge(bridge);
      it = bridges_.erase(it);
      continue;
    } else {
      int wsa_err = WSAGetLastError();
      if (WSAEWOULDBLOCK != wsa_err) {
        LOG_WARN("recv from sockB fail", K(wsa_err));
        destroy_bridge(bridge);
        it = bridges_.erase(it);
        continue;
      }
      // WSAEWOULDBLOCK: no data, normal
    }
    ++it;
  }
}

void ObWinNamedPipeServer::destroy_bridge(WinPipeBridge* bridge)
{
  if (NULL == bridge) return;
  if (NULL != bridge->write_buf) {
    ob_free(bridge->write_buf);
    bridge->write_buf = NULL;
  }
  if (INVALID_HANDLE_VALUE != bridge->pipe_handle) {
    DisconnectNamedPipe(bridge->pipe_handle);
    CloseHandle(bridge->pipe_handle);
    bridge->pipe_handle = INVALID_HANDLE_VALUE;
  }
  if (INVALID_SOCKET != bridge->sock_b) {
    closesocket(bridge->sock_b);
    bridge->sock_b = INVALID_SOCKET;
  }
  // sock_a is owned by ObSqlSock; it will be closed when ObSqlSock detects
  // the peer (sock_b) has closed, triggering EPOLLHUP in WSAPoll.
  bridge->~WinPipeBridge();
  ob_free(bridge);
}

} // end namespace obmysql
} // end namespace oceanbase

#endif // _WIN32