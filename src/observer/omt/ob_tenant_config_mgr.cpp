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

#define USING_LOG_PREFIX SERVER_OMT
#include "ob_tenant_config_mgr.h"
#include "observer/ob_sql_client_decorator.h"
#include "observer/ob_server_struct.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase {
namespace omt {

ObTenantConfigGuard::ObTenantConfigGuard() : ObTenantConfigGuard(nullptr)
{
}

ObTenantConfigGuard::ObTenantConfigGuard(ObServerConfig *config)
{
  config_ = config;
}

ObTenantConfigGuard::~ObTenantConfigGuard()
{
}

void ObTenantConfigGuard::set_config(ObServerConfig *config)
{
  config_ = config;
}


} // omt
} // oceanbase
