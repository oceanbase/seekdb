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

#ifndef OCEANBASE_SHARE_OB_SERVER_ROLE_H_
#define OCEANBASE_SHARE_OB_SERVER_ROLE_H_

#include "lib/string/ob_string.h" // ObString
#include "lib/utility/ob_print_utils.h"             // TO_STRING_KV
#include "lib/utility/ob_unify_serialize.h"   // serialize
#include "lib/oblog/ob_log_module.h"      // LOG*

namespace oceanbase {
namespace share {

class ObServerRole
{
  OB_UNIS_VERSION(1);
public:
  enum Role
  {
    INVALID_ROLE = 0,
    PRIMARY_ROLE = 1,
    STANDBY_ROLE = 2,
    RESTORE_ROLE = 3,
    MAX_ROLE = 4,
  };
public:
  ObServerRole() : value_(INVALID_ROLE) {}
  explicit ObServerRole(const Role value) : value_(value) {}
  explicit ObServerRole(const ObString &str);
  ~ObServerRole() { reset(); }

public:
  void reset() { value_ = INVALID_ROLE; }
  bool is_valid() const { return INVALID_ROLE != value_; }
  Role value() const { return value_; }
  const char* to_str() const;

  // assignment
  ObServerRole &operator=(const Role value) { value_ = value; return *this; }

  // compare operator
  bool operator == (const ObServerRole &other) const { return value_ == other.value_; }
  bool operator != (const ObServerRole &other) const { return value_ != other.value_; }

  // ObServerRole attribute interface
  bool is_invalid() const { return INVALID_ROLE == value_; }
  bool is_primary() const { return PRIMARY_ROLE == value_; }
  bool is_standby() const { return STANDBY_ROLE == value_; }
  bool is_restore() const { return RESTORE_ROLE == value_; }

  TO_STRING_KV("server_role", to_str(), K_(value));
  DECLARE_TO_YSON_KV;
private:
  Role value_;
};

#define GEN_IS_SERVER_ROLE_DECLARE(ROLE_VALUE, ROLE_NAME) \
  bool is_##ROLE_NAME##_role(const ObServerRole::Role value);

GEN_IS_SERVER_ROLE_DECLARE(ObServerRole::Role::INVALID_ROLE, invalid)
GEN_IS_SERVER_ROLE_DECLARE(ObServerRole::Role::PRIMARY_ROLE, primary)
GEN_IS_SERVER_ROLE_DECLARE(ObServerRole::Role::STANDBY_ROLE, standby)
GEN_IS_SERVER_ROLE_DECLARE(ObServerRole::Role::RESTORE_ROLE, restore)
#undef GEN_IS_SERVER_ROLE_DECLARE

static const ObServerRole INVALID_SERVER_ROLE(ObServerRole::INVALID_ROLE);
static const ObServerRole PRIMARY_SERVER_ROLE(ObServerRole::PRIMARY_ROLE);
static const ObServerRole STANDBY_SERVER_ROLE(ObServerRole::STANDBY_ROLE);
static const ObServerRole RESTORE_SERVER_ROLE(ObServerRole::RESTORE_ROLE);

}  // share
}  // oceanbase

#endif /* OCEANBASE_SHARE_OB_SERVER_ROLE_H_ */
