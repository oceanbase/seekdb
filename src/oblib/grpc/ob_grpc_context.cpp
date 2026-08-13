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

#include "grpc/ob_grpc_context.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/tls_credentials_options.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include "lib/net/ob_net_util.h"
#include "lib/ob_running_mode.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/profile/ob_trace_id.h"

namespace oceanbase
{
namespace obgrpc
{

bool read_file_content(const std::string &path, std::string &content)
{
  bool ok = false;
  std::ifstream ifs(path.c_str());
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

static int64_t parse_cert_expire_time_us(const char *cert_path)
{
  int64_t expire_us = 0;
  BIO *bio = BIO_new_file(cert_path, "r");
  if (NULL != bio) {
    X509 *cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
    if (NULL != cert) {
      const ASN1_TIME *not_after = X509_get0_notAfter(cert);
      if (NULL != not_after && not_after->length >= 12) {
        struct tm tm1;
        memset(&tm1, 0, sizeof(tm1));
        tm1.tm_year = (not_after->data[0] - '0') * 10 + (not_after->data[1] - '0') + 100;
        tm1.tm_mon  = (not_after->data[2] - '0') * 10 + (not_after->data[3] - '0') - 1;
        tm1.tm_mday = (not_after->data[4] - '0') * 10 + (not_after->data[5] - '0');
        tm1.tm_hour = (not_after->data[6] - '0') * 10 + (not_after->data[7] - '0');
        tm1.tm_min  = (not_after->data[8] - '0') * 10 + (not_after->data[9] - '0');
        tm1.tm_sec  = (not_after->data[10] - '0') * 10 + (not_after->data[11] - '0');
        expire_us = static_cast<int64_t>(mktime(&tm1)) * 1000000LL;
      }
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

std::shared_ptr<grpc::ServerCredentials> create_server_credentials(
    std::shared_ptr<grpc::experimental::CertificateProviderInterface> &provider_out)
{
  std::shared_ptr<grpc::ServerCredentials> creds;
  std::string ca_cert;
  std::string node_cert;
  std::string node_key;
  if (read_file_content("wallet/ca.pem", ca_cert)
      && read_file_content("wallet/cert.pem", node_cert)
      && read_file_content("wallet/key.pem", node_key)) {
    std::shared_ptr<grpc::experimental::FileWatcherCertificateProvider> provider(
        new grpc::experimental::FileWatcherCertificateProvider(
            "wallet/key.pem", "wallet/cert.pem", "wallet/ca.pem", 3600u));
    grpc::experimental::TlsServerCredentialsOptions opts(provider);
    opts.watch_root_certs();
    opts.watch_identity_key_cert_pairs();
    opts.set_cert_request_type(GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY);
    creds = grpc::experimental::TlsServerCredentials(opts);
    provider_out = provider;
    RPC_LOG(INFO, "gRPC TLS server credentials created");
  }
  return creds;
}

std::shared_ptr<grpc::ChannelCredentials> create_client_credentials()
{
  std::shared_ptr<grpc::ChannelCredentials> creds;
  std::string ca_cert;
  std::string node_cert;
  std::string node_key;
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
  char buf[64] = {0};
  snprintf(buf, sizeof(buf), "OB_ERROR:%d", ob_ret);
  return grpc::Status(grpc::StatusCode::INTERNAL, buf);
}

int extract_error_from_grpc_status(const grpc::Status &status, bool *is_ob_error)
{
  int ret = OB_SUCCESS;
  if (status.ok()) {
    if (NULL != is_ob_error) {
      *is_ob_error = false;
    }
  } else {
    const std::string &msg = status.error_message();
    if (0 == msg.find("OB_ERROR:")) {
      if (NULL != is_ob_error) {
        *is_ob_error = true;
      }
      ret = OB_RPC_SEND_ERROR;
      const size_t pos = strlen("OB_ERROR:");
      if (pos < msg.length()) {
        char *endptr = NULL;
        long parsed = strtol(msg.c_str() + pos, &endptr, 10);
        if (endptr != msg.c_str() + pos) {
          ret = static_cast<int>(parsed);
        }
      }
    } else {
      if (NULL != is_ob_error) {
        *is_ob_error = false;
      }
      switch (status.error_code()) {
        case grpc::StatusCode::DEADLINE_EXCEEDED:
          ret = OB_TIMEOUT;
          break;
        case grpc::StatusCode::UNIMPLEMENTED:
          ret = OB_NOT_SUPPORTED;
          break;
        case grpc::StatusCode::CANCELLED:
          ret = OB_CANCELED;
          break;
        case grpc::StatusCode::INVALID_ARGUMENT:
          ret = OB_INVALID_ARGUMENT;
          break;
        case grpc::StatusCode::NOT_FOUND:
          ret = OB_ENTRY_NOT_EXIST;
          break;
        default:
          ret = OB_RPC_SEND_ERROR;
          break;
      }
    }
  }
  return ret;
}

ObGrpcContext::ObGrpcContext() : dst_(), timeout_(MAX_RPC_TIMEOUT)
{
}

int ObGrpcContext::init(const common::ObAddr &addr, int64_t timeout)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!addr.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    RPC_LOG(WARN, "invalid addr", K(ret), K(addr));
  } else {
    dst_ = addr;
    timeout_ = timeout;
  }
  return ret;
}

void ObGrpcContext::set_grpc_context(grpc::ClientContext &context)
{
  set_grpc_context_(context, timeout_);
}

void ObGrpcContext::set_grpc_context(grpc::ClientContext &context, const int64_t timeout)
{
  set_grpc_context_(context, timeout);
}

int ObGrpcContext::translate_error(const grpc::Status &status)
{
  int ret = OB_SUCCESS;
  const grpc::StatusCode err_code = status.error_code();
  const int64_t send_cnt = ATOMIC_AAF(&statistics_info.send_cnt_, 1);
  if (OB_UNLIKELY(grpc::StatusCode::OK != err_code)) {
    bool is_ob_error = false;
    ret = extract_error_from_grpc_status(status, &is_ob_error);
    ATOMIC_INC(&statistics_info.failed_cnt_);
    RPC_LOG(WARN, "grpc call failed", K(err_code), K(ret),
        K(is_ob_error), "error_msg", status.error_message().c_str(), K(dst_), K(timeout_));
  }
  if (OB_UNLIKELY(0 == send_cnt % REPORT_COUNT_INTERVAL)) {
    const int64_t failed_cnt = ATOMIC_LOAD(&statistics_info.failed_cnt_);
    RPC_LOG(INFO, "[grpc report]", KP(this), K(dst_), K(send_cnt), K(failed_cnt));
  }
  return ret;
}

void ObGrpcContext::set_grpc_context_(grpc::ClientContext &context, const int64_t timeout)
{
  char str_buf[512] = {0};
  char trace_id_buf[OB_MAX_TRACE_ID_BUFFER_SIZE] = {'\0'};
  const char *trace_id_str = NULL;
  const uint64_t *trace_id = common::ObCurTraceId::get();
  if (0 == trace_id[0]) {
    common::ObCurTraceId::TraceId temp;
    temp.init(dst_);
    temp.to_string(trace_id_buf, sizeof(trace_id_buf));
    trace_id_str = trace_id_buf;
  } else {
    trace_id_str = common::ObCurTraceId::get_trace_id_str(trace_id_buf, sizeof(trace_id_buf));
  }
  snprintf(str_buf, sizeof(str_buf), "%X,%s", VERSION, trace_id_str);
  context.AddMetadata("custom-header", str_buf);

  const int64_t abs_timeout_us = oceanbase::ObTimeUtility::current_time() + timeout;
  const std::chrono::microseconds ts(abs_timeout_us);
  const std::chrono::time_point<std::chrono::system_clock> tp(ts);
  context.set_deadline(tp);
}

} // end namespace obgrpc
} // end namespace oceanbase
