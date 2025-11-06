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

#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_mgr.h"
#include "sql/resolver/ob_schema_checker.h"

namespace oceanbase
{
using namespace common;
using namespace observer;

namespace share
{
namespace schema
{
int ObSchemaGetterGuard::get_location_schema_by_name(const uint64_t tenant_id,
                                                     const common::ObString &name,
                                                     const ObLocationSchema *&schema)
{
  int ret = OB_SUCCESS;
  schema = nullptr;
  const ObSchemaMgr *mgr = NULL;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))
             || OB_UNLIKELY(name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(tenant_id), K(name), KR(ret));
  } else if (OB_FAIL(get_tenant_name_case_mode(tenant_id, mode))) {
    LOG_WARN("fail to get_tenant_name_case_mode", K(ret), K(tenant_id));
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  } else if (OB_FAIL(check_tenant_schema_guard(tenant_id))) {
    LOG_WARN("fail to check tenant schema guard", KR(ret), K(tenant_id), K_(tenant_id));
  } else if (OB_FAIL(get_schema_mgr(tenant_id, mgr))) {
    LOG_WARN("fail to get schema mgr", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(mgr)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret), K(tenant_id));
  } else if (OB_FAIL(mgr->location_mgr_.get_location_schema_by_name(tenant_id, mode, name, schema))) {
    LOG_WARN("get location schema failed", K(name), KR(ret));
  }
  return ret;
}

int ObSchemaGetterGuard::get_location_schema_by_id(const uint64_t tenant_id,
                                                   const uint64_t location_id,
                                                   const ObLocationSchema *&schema)
{
  int ret = OB_SUCCESS;
  schema = nullptr;
  const ObSchemaMgr *mgr = NULL;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))
             || OB_UNLIKELY(!is_valid_id(location_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(tenant_id), K(location_id), KR(ret));
  } else if (OB_FAIL(check_tenant_schema_guard(tenant_id))) {
    LOG_WARN("fail to check tenant schema guard", KR(ret), K(tenant_id), K_(tenant_id));
  } else if (OB_FAIL(get_schema_mgr(tenant_id, mgr))) {
    LOG_WARN("fail to get schema mgr", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(mgr)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret), K(tenant_id));
  } else if (OB_FAIL(mgr->get_location_schema(tenant_id, location_id, schema))) {
    LOG_WARN("get schema failed", K(tenant_id), K(location_id), KR(ret));
  }
  return ret;
}

}
}
}
