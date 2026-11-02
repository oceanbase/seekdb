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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include "ob_tenant_schema_service.h"
#include "share/schema/ob_multi_version_schema_service.h"


namespace oceanbase
{
namespace share
{
namespace schema
{

int ObTenantSchemaService::mtl_init(ObTenantSchemaService *&tenant_schema_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tenant_schema_service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tenant_schema_service is null", K(ret));
  } else {
    tenant_schema_service->schema_service_ = &GSCHEMASERVICE;
  }
  return ret;
}

void ObTenantSchemaService::destroy()
{
  schema_service_ = nullptr;
}

}
}
}
