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

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#ifndef _WIN32
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include "lib/ob_running_mode.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/serialization.h"
#include "lib/net/ob_net_util.h"
#include "grpc/ob_grpc_context.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
//#include "grpc/newlogstorepb.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using grpc::StatusCode;

namespace oceanbase {
namespace obgrpc {


bool read_file_content(const std::string &path, std::string &content)
{
  bool ok = false;
  std::ifstream ifs(path);
  if (ifs.is_open()) {
    std::ostringstream oss;
    oss << ifs.rdbuf();
    content = oss.str();
    ok = !content.empty();
    if (!ok) {
      RPC_LOG_RET(WARN, OB_ERR_UNEXPECTED, "certificate file is empty", "path", path.c_str());
    }
  } else {
    RPC_LOG_RET(WARN, OB_IO_ERROR, "failed to open certificate file", "path", path.c_str());
  }
  return ok;
}

// Parse the notAfter field from a PEM certificate file and return the expiry
// time in microseconds (unix epoch), matching the convention of ssl_key_expired_time_.
// Returns 0 on failure.
static int64_t parse_cert_expire_time_us(const char *cert_path)
{
  int64_t expire_us = 0;
  BIO *bio = BIO_new_file(cert_path, "r");
  if (bio != nullptr) {
    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (cert != nullptr) {
      const ASN1_TIME *not_after = X509_get0_notAfter(cert);
      struct tm tm1;
      memset(&tm1, 0, sizeof(tm1));
      tm1.tm_year = (not_after->data[ 0] - '0') * 10 + (not_after->data[ 1] - '0') + 100;
      tm1.tm_mon  = (not_after->data[ 2] - '0') * 10 + (not_after->data[ 3] - '0') - 1;
      tm1.tm_mday = (not_after->data[ 4] - '0') * 10 + (not_after->data[ 5] - '0');
      tm1.tm_hour = (not_after->data[ 6] - '0') * 10 + (not_after->data[ 7] - '0');
      tm1.tm_min  = (not_after->data[ 8] - '0') * 10 + (not_after->data[ 9] - '0');
      tm1.tm_sec  = (not_after->data[10] - '0') * 10 + (not_after->data[11] - '0');
      expire_us = static_cast<int64_t>(mktime(&tm1)) * 1000000LL;
      X509_free(cert);
    } else {
      RPC_LOG_RET(WARN, OB_ERR_UNEXPECTED, "PEM_read_bio_X509 failed", "path", cert_path);
    }
    BIO_free(bio);
  } else {
    RPC_LOG_RET(WARN, OB_IO_ERROR, "BIO_new_file failed", "path", cert_path);
  }
  return expire_us;
}

int64_t get_rpc_cert_expire_time()
{
  return parse_cert_expire_time_us("wallet/cert.pem");
}

bool __attribute__((weak)) ob_grpc_is_rpc_tls_enabled()
{
  return false;
}

std::shared_ptr<grpc::ServerCredentials> create_server_credentials(
    std::shared_ptr<grpc::experimental::CertificateProviderInterface> &provider_out)
{
  std::shared_ptr<grpc::ServerCredentials> creds;
  std::string ca_cert, node_cert, node_key;
  if (read_file_content("wallet/ca.pem", ca_cert)
      && read_file_content("wallet/cert.pem", node_cert)
      && read_file_content("wallet/key.pem", node_key)) {
    auto provider = std::make_shared<grpc::experimental::FileWatcherCertificateProvider>(
        "wallet/key.pem", "wallet/cert.pem", "wallet/ca.pem", 3600u);
    grpc::experimental::TlsServerCredentialsOptions opts(provider);
    opts.watch_root_certs();
    opts.watch_identity_key_cert_pairs();
    opts.set_cert_request_type(
        GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    creds = grpc::experimental::TlsServerCredentials(opts);
    provider_out = provider;
    RPC_LOG(INFO, "gRPC mTLS server credentials created");
  }
  return creds;
}

std::shared_ptr<grpc::ChannelCredentials> create_client_credentials()
{
  std::shared_ptr<grpc::ChannelCredentials> creds;
  std::string ca_cert, node_cert, node_key;
  if (read_file_content("wallet/ca.pem", ca_cert)
      && read_file_content("wallet/cert.pem", node_cert)
      && read_file_content("wallet/key.pem", node_key)) {
    grpc::SslCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs = ca_cert;
    ssl_opts.pem_cert_chain = node_cert;
    ssl_opts.pem_private_key = node_key;
    creds = grpc::SslCredentials(ssl_opts);
  }
  return creds;
}

grpc::Status ob_error_to_grpc_status(int ob_ret)
{
  if (OB_SUCCESS == ob_ret) {
    return grpc::Status::OK;
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "OB_ERROR:%d", ob_ret);
  return grpc::Status(grpc::StatusCode::INTERNAL, buf);
}

int extract_error_from_grpc_status(const grpc::Status& status, bool* is_ob_error)
{
  if (status.ok()) {
    if (is_ob_error) {
      *is_ob_error = false;
    }
    return OB_SUCCESS;
  }
  const std::string& msg = status.error_message();
  if (msg.find("OB_ERROR:") == 0) {
    if (is_ob_error) {
      *is_ob_error = true;
    }
    int ob_ret = OB_RPC_SEND_ERROR;
    size_t pos = strlen("OB_ERROR:");
    if (pos < msg.length()) {
      char* endptr = nullptr;
      long parsed = strtol(msg.c_str() + pos, &endptr, 10);
      if (endptr != msg.c_str() + pos) {
        ob_ret = static_cast<int>(parsed);
      }
    }
    return ob_ret;
  }

  if (is_ob_error) {
    *is_ob_error = false;
  }
  StatusCode err_code = status.error_code();
  switch (err_code) {
    case grpc::StatusCode::DEADLINE_EXCEEDED:
      return OB_TIMEOUT;
    case grpc::StatusCode::UNIMPLEMENTED:
      return OB_NOT_SUPPORTED;
    case grpc::StatusCode::CANCELLED:
      return OB_CANCELED;
    case grpc::StatusCode::INVALID_ARGUMENT:
      return OB_INVALID_ARGUMENT;
    case grpc::StatusCode::NOT_FOUND:
      return OB_ENTRY_NOT_EXIST;
    default:
      return OB_RPC_SEND_ERROR;
  }
}

ObGrpcContext::ObGrpcContext() : dst_(), timeout_(MAX_RPC_TIMEOUT)
{}

int ObGrpcContext::init(const ObAddr &addr, int64_t timeout) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!addr.is_valid())) {
    RPC_LOG(WARN, "invalid addr", K(addr));
    ret = OB_INVALID_ARGUMENT;
  } else {
    dst_ = addr;
    timeout_ = timeout;
  }
  return ret;
}

void ObGrpcContext::set_grpc_context(ClientContext &context)
{
  set_grpc_context_(context, timeout_);
}

void ObGrpcContext::set_grpc_context(ClientContext &context, const int64_t timeout)
{
  set_grpc_context_(context, timeout);
}

int ObGrpcContext::translate_error(const Status &status)
{
  int ret = OB_SUCCESS;
  StatusCode err_code = status.error_code();
  int64_t send_cnt = ATOMIC_AAF(&statistics_info.send_cnt_, 1);
  if (OB_UNLIKELY(StatusCode::OK != err_code)) {
    bool is_ob_error = false;
    ret = extract_error_from_grpc_status(status, &is_ob_error);

    ATOMIC_INC(&statistics_info.failed_cnt_);
    RPC_LOG(WARN, "grpc call failed", K(err_code), K(ret),
        K(is_ob_error), "error_msg", status.error_message().c_str(), K(dst_), K(timeout_));
  }
  if (OB_UNLIKELY(send_cnt % REPORT_COUNT_INTERVAL == 0)) {
    int64_t failed_cnt = ATOMIC_LOAD(&statistics_info.failed_cnt_);
    RPC_LOG(INFO, "[grpc report]", KP(this), K(dst_), K(send_cnt), K(failed_cnt));
  }
  return ret;
}

void ObGrpcContext::set_grpc_context_(ClientContext &context, const int64_t timeout)
{
  char str_buf[512] = {0};
  char trace_id_buf[OB_MAX_TRACE_ID_BUFFER_SIZE] = {'\0'};
  const char* trace_id_str = NULL;
  const uint64_t* trace_id = common::ObCurTraceId::get();
  if (0 == trace_id[0]) {
    common::ObCurTraceId::TraceId temp;
    temp.init(obrpc::ObRpcProxy::myaddr_);
    temp.to_string(trace_id_buf, sizeof(trace_id_buf));
    trace_id_str = trace_id_buf;
  } else {
    trace_id_str = common::ObCurTraceId::get_trace_id_str(trace_id_buf, sizeof(trace_id_buf));
  }
  snprintf(str_buf, sizeof(str_buf), "%X,%s", VERSION, trace_id_str);
  context.AddMetadata("custom-header", str_buf);
  int64_t abs_timeout_us = oceanbase::ObTimeUtility::current_time() + timeout;
  std::chrono::microseconds ts(abs_timeout_us);
  std::chrono::time_point<std::chrono::system_clock> tp(ts);
  context.set_deadline(tp);
}

} // end of namespace obgrpc
} // end of namespace oceanbase
