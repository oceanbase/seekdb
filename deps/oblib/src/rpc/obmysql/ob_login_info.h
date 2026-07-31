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

#ifndef _OB_LOGIN_INFO_H_
#define _OB_LOGIN_INFO_H_

#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "rpc/obmysql/ob_mysql_packet.h"   // ObMySQLCapabilityFlags, ObStringKV
#include "nio.h"

namespace oceanbase
{
namespace obmysql
{

// A read-only view over the login that Rust parsed. It replaces the packet
// decoder OMPKHandshakeResponse: the wire parse now lives in the crate, and
// load() maps the Rust offsets onto the generation-scoped Rust body referenced
// by the request packet, so the string fields keep the same lifetime the old
// zero-copy decode gave them.
class ObHandshakeResponse
{
public:
  ObHandshakeResponse()
    : capability_(), character_set_(0), username_(),
      auth_response_(), database_(), auth_plugin_name_(), connect_attrs_() {}

  int load(void *sess, const uint64_t generation, const char *body,
           const int64_t body_len) {
    int ret = OB_SUCCESS;
    nio_login_view view = {};
    if (NULL == sess || NULL == body || body_len < 0) {
      ret = OB_INVALID_ARGUMENT;
    } else if (0 == generation ||
               0 != nio_get_login_view(sess, generation, &view)) {
      ret = OB_NOT_SUPPORTED; // Rust refused the login (short / pre-4.1)
    } else {
      capability_.capability_ = view.capabilities;
      character_set_ = view.charset;
      if (OB_FAIL(map_field(view.username, body, body_len, username_))) {
      } else if (OB_FAIL(map_field(view.auth_response, body, body_len, auth_response_))) {
      } else if (OB_FAIL(map_field(view.database, body, body_len, database_))) {
      } else if (OB_FAIL(map_field(view.auth_plugin_name, body, body_len, auth_plugin_name_))) {
      }
      if (OB_SUCC(ret) && (view.attr_count < 0 || view.attr_count > body_len ||
                           (view.attr_count > 0 && NULL == view.attrs))) {
        ret = OB_INVALID_DATA;
      }
      for (int i = 0; OB_SUCC(ret) && i < view.attr_count; ++i) {
        const nio_login_attr &attr = view.attrs[i];
        if (!valid_range(attr.key_off, attr.key_len, body_len) ||
            !valid_range(attr.value_off, attr.value_len, body_len)) {
          ret = OB_INVALID_DATA;
        } else {
          ObStringKV kv;
          kv.key_.assign_ptr(body + attr.key_off, attr.key_len);
          kv.value_.assign_ptr(body + attr.value_off, attr.value_len);
          ret = connect_attrs_.push_back(kv);
        }
      }
    }
    // Do not release the Rust descriptors here. Tenant routing loads this view
    // before ObMPConnect loads it a second time; the final consumer owns the
    // release after it has mapped every descriptor onto the active body.
    return ret;
  }

  const ObMySQLCapabilityFlags &get_capability_flags() const { return capability_; }
  void set_capability_flags(const ObMySQLCapabilityFlags &cap) { capability_ = cap; }
  uint8_t get_char_set() const { return character_set_; }
  const common::ObString &get_username() const { return username_; }
  const common::ObString &get_auth_response() const { return auth_response_; }
  const common::ObString &get_database() const { return database_; }
  const common::ObString &get_auth_plugin_name() const { return auth_plugin_name_; }
  const common::ObIArray<ObStringKV> &get_connect_attrs() const { return connect_attrs_; }

private:
  static bool valid_range(const int32_t off, const int32_t len, const int64_t body_len)
  {
    return off >= 0 && len >= 0 && static_cast<int64_t>(off) <= body_len &&
           static_cast<int64_t>(len) <= body_len - static_cast<int64_t>(off);
  }

  int map_field(const nio_login_field &field, const char *body, const int64_t body_len,
                common::ObString &out)
  {
    int ret = OB_SUCCESS;
    if (field.len < 0) {
      // Absent optional field.
    } else if (!valid_range(field.off, field.len, body_len)) {
      ret = OB_INVALID_DATA;
    } else {
      out.assign_ptr(body + field.off, field.len);
    }
    return ret;
  }

private:
  ObMySQLCapabilityFlags capability_;
  uint8_t character_set_;
  common::ObString username_;
  common::ObString auth_response_;
  common::ObString database_;
  common::ObString auth_plugin_name_;
  common::ObSEArray<ObStringKV, 8> connect_attrs_;
};

} // namespace obmysql
} // namespace oceanbase

#endif /* _OB_LOGIN_INFO_H_ */
