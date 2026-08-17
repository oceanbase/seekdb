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

#ifndef OCEANBASE_LIB_NET_OB_SQL_TLS_INFO_H_
#define OCEANBASE_LIB_NET_OB_SQL_TLS_INFO_H_

#include <stddef.h>
#include <stdint.h>

namespace oceanbase
{
namespace common
{

// Rust-normalized TLS information borrowed from the Rust SQL-NIO connection.
// All string pointers are owned by Rust and are valid only while the current
// SQL request is being processed; callers must not retain them after returning
// to the reactor. No DER/X509 object crosses this boundary.
struct ObSqlTlsInfo
{
  ObSqlTlsInfo()
      : tls_active_(false),
        peer_cert_present_(false),
        peer_cert_verified_(false),
        peer_cert_info_valid_(false),
        cipher_name_(NULL),
        cipher_name_len_(0),
        peer_cert_common_name_(NULL),
        peer_cert_common_name_len_(0),
        peer_cert_issuer_(NULL),
        peer_cert_issuer_len_(0),
        peer_cert_subject_(NULL),
        peer_cert_subject_len_(0)
  {}

  void reset()
  {
    tls_active_ = false;
    peer_cert_present_ = false;
    peer_cert_verified_ = false;
    peer_cert_info_valid_ = false;
    cipher_name_ = NULL;
    cipher_name_len_ = 0;
    peer_cert_common_name_ = NULL;
    peer_cert_common_name_len_ = 0;
    peer_cert_issuer_ = NULL;
    peer_cert_issuer_len_ = 0;
    peer_cert_subject_ = NULL;
    peer_cert_subject_len_ = 0;
  }

  bool tls_active_;
  bool peer_cert_present_;
  bool peer_cert_verified_;
  bool peer_cert_info_valid_;
  const char *cipher_name_;
  int64_t cipher_name_len_;
  const char *peer_cert_common_name_;
  int64_t peer_cert_common_name_len_;
  const char *peer_cert_issuer_;
  int64_t peer_cert_issuer_len_;
  const char *peer_cert_subject_;
  int64_t peer_cert_subject_len_;
};

} // namespace common
} // namespace oceanbase

#endif /* OCEANBASE_LIB_NET_OB_SQL_TLS_INFO_H_ */
