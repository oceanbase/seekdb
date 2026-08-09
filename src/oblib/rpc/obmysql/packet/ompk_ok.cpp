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

#define USING_LOG_PREFIX RPC_OBMYSQL

#include "rpc/obmysql/packet/ompk_ok.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;

OMPKOK::OMPKOK()
    : affected_rows_(0),
      last_insert_id_(0),
      server_status_(0x22),
      warnings_(0),
      message_(),
      changed_schema_(),
      state_changed_(false),
      system_vars_(),
      capability_(),
      is_schema_changed_(false),
      use_standard_serialize_(false)
{
}

int OMPKOK::set_message(const ObString &message)
{
  int ret = OB_SUCCESS;
  if (!message.empty()) {
    message_ = message;
  }
  return ret;
}

void OMPKOK::set_state_changed(const bool state_changed)
{
  state_changed_ = state_changed;
  // If the CLIENT_SESSION_TRACK capability is not enabled
  // the Server should not set the SERVER_SESSION_STATE_CHANGED Flag
  if (capability_.cap_flags_.OB_CLIENT_SESSION_TRACK) {
    server_status_.status_flags_.OB_SERVER_SESSION_STATE_CHANGED = 1;
  }
}

void OMPKOK::set_changed_schema(const common::ObString &schema)
{
  changed_schema_ = schema;
  is_schema_changed_ = true;
}

void OMPKOK::set_use_standard_serialize(const bool value)
{
  use_standard_serialize_ = value;
}

int OMPKOK::add_system_var(const ObStringKV &system_var)
{
  int ret = OB_SUCCESS;
  if (system_var.key_.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input value", K(system_var), K(ret));
  } else if (OB_FAIL(system_vars_.push_back(system_var))) {
  }
  return ret;
}

int64_t OMPKOK::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(affected_rows), K_(last_insert_id), K_(server_status_.flags),
       K_(warnings), K_(message), K_(changed_schema), K_(state_changed),
       K_(system_vars), K_(capability_.capability),
       K_(use_standard_serialize));
  J_OBJ_END();
  return pos;
}
