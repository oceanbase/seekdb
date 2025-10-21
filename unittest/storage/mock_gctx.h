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

#ifndef OCEANBASE_STORAGE_MOCK_GCTX_H_
#define OCEANBASE_STORAGE_MOCK_GCTX_H_
#include "observer/ob_server.h"
#include "sql/optimizer/mock_locality_manger.h"
namespace oceanbase
{
namespace storage
{

void init_global_context()
{
  static obrpc::ObSrvRpcProxy srv_rpc_proxy_;
  static common::ObMySQLProxy sql_proxy_;
  static test::MockLocalityManager locality_manager_;

  GCTX.srv_rpc_proxy_ = &srv_rpc_proxy_;
  GCTX.sql_proxy_ = &sql_proxy_;
  GCTX.locality_manager_ = &locality_manager_;

  GCONF._enable_ha_gts_full_service = false;
}

}//namespace storage
}//namespace oceanbase
#endif // OCEANBASE_STORAGE_MOCK_OB_PARTITION_H_
