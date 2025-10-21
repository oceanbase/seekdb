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
 
#ifndef OB_SSL_CONFIG_H_
#define OB_SSL_CONFIG_H_

#include <stdint.h>
#include <openssl/ssl.h>

namespace oceanbase {
namespace common {

struct ssl_state_st {
  ssl_state_st() { ssl = NULL; hand_shake_done = 0; }
  SSL* ssl;
  int  hand_shake_done;
};

typedef struct ObSSLConfig {
  ObSSLConfig(int is_from_file, int is_sm, const char* ca_cert, const char* sign_cert, 
              const char* sign_private_key, const char* enc_cert, const char* enc_private_key):
              is_from_file_(is_from_file), is_sm_(is_sm), ca_cert_(ca_cert), sign_cert_(sign_cert),
              sign_private_key_(sign_private_key), enc_cert_(enc_cert), enc_private_key_(enc_cert) {}
  int is_from_file_;
  int is_sm_;
  const char* ca_cert_;
  const char* sign_cert_;
  const char* sign_private_key_;
  const char* enc_cert_;
  const char* enc_private_key_;
} ObSSLConfig;

enum OB_SSL_CTX_ID {
  OB_SSL_CTX_ID_SQL_NIO,
  OB_SSL_CTX_ID_MAX
};

enum OB_SSL_ROLE {
  OB_SSL_ROLE_CLIENT,
  OB_SSL_ROLE_SERVER,
  OB_SSL_ROLE_MAX
};

int  ob_ssl_load_config(int ctx_id, const ObSSLConfig& ssl_config);
int  ob_fd_enable_ssl_for_server(int fd, int ctx_id, uint64_t tls_option, ssl_state_st &ssl_st);
void  ob_fd_disable_ssl(ssl_state_st &ssl);
ssize_t ob_read_regard_ssl(int fd, void *buf, size_t nbytes, ssl_state_st &ssl_st);
ssize_t ob_write_regard_ssl(int fd, const void *buf, size_t nbytes, ssl_state_st &ssl_st);
}
}

#endif
