/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
