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
namespace oceanbase
{
namespace share
{

void ObGlobalContext::init()
{
  server_status_ = share::OBSERVER_INVALID_STATUS;
}

ObGlobalContext &ObGlobalContext::get_instance()
{
  static ObGlobalContext global_context;
  return global_context;
}




uint64_t ObGlobalContext::get_server_index() const
{
  uint64_t server_index = 0;
  uint64_t server_id = ATOMIC_LOAD(&server_id_);
  if (OB_UNLIKELY(!is_valid_server_id(server_id))) {
    // return 0;
  } else {
    server_index = ObShareUtil::compute_server_index(server_id);
  }
  return server_index;
}

DEF_TO_STRING(ObGlobalContext)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(self_addr_seq),
       KP_(root_service),
       KP_(ob_service),
       KP_(schema_service),
       KP_(config),
       KP_(config_mgr),
       KP_(lst_operator),
       KP_(tablet_operator),
       KP_(srv_rpc_proxy),
       KP_(storage_rpc_proxy),
       KP_(rs_rpc_proxy),
       KP_(load_data_proxy),
       KP_(executor_rpc),
       KP_(sql_proxy),
       KP_(rs_mgr),
       KP_(bandwidth_throttle),
       KP_(vt_par_ser),
       KP_(session_mgr),
       KP_(sql_engine),
       KP_(omt),
       KP_(vt_iter_creator),
       KP_(batch_rpc),
       KP_(server_tracer),
       K_(start_time),
       KP_(warm_up_start_time));
  J_COMMA();
  J_KV(K_(server_id),
       K_(status),
       K_(start_service_time),
       KP_(diag),
       KP_(scramble_rand),
       KP_(weak_read_service),
       KP_(schema_status_proxy),
       K_(ssl_key_expired_time),
       K_(inited),
       K_(in_bootstrap));
  J_OBJ_END();
  return pos;
}


} // end of namespace observer
} // end of namespace oceanbase
