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

#define USING_LOG_PREFIX SERVER
#include "ob_server_struct.h"
#include "ob_share_util.h"
namespace oceanbase
{
namespace share
{

void ObGlobalContext::init()
{
  server_role_ = share::ObServerRole::PRIMARY_ROLE;
  set_effective_mysql_port(0);
}

ObGlobalContext &ObGlobalContext::get_instance()
{
  static ObGlobalContext global_context;
  return global_context;
}
DEF_TO_STRING(ObGlobalContext)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(self_addr_seq),
       KP_(schema_service),
       KP_(config),
       KP_(config_mgr),
       KP_(tablet_operator),
       KP_(sql_proxy),
       KP_(bandwidth_throttle),
       K_(start_time),
       KP_(warm_up_start_time));
  J_COMMA();
  J_KV(K_(status),
       K_(start_service_time),
       KP_(diag),
       KP_(scramble_rand),
       KP_(schema_status_proxy),
       K_(ssl_key_expired_time),
       K_(inited),
       K_(in_bootstrap),
       K_(embedded));
  J_OBJ_END();
  return pos;
}


} // end of namespace observer
} // end of namespace oceanbase
