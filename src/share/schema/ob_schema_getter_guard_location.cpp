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
int ObSchemaGetterGuard::get_location_schema_by_name(const common::ObString &name,
                                                     const ObLocationSchema *&schema)
{
  int ret = OB_SUCCESS;
  schema = nullptr;
  const ObSchemaMgr *mgr = NULL;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_UNLIKELY(!true)
             || OB_UNLIKELY(name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(name), KR(ret));
  } else if (OB_FAIL(get_tenant_name_case_mode(mode))) {
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret));
  } else if (OB_FAIL(mgr->location_mgr_.get_location_schema_by_name(mode, name, schema))) {
  }
  return ret;
}

int ObSchemaGetterGuard::get_location_schema_by_id(const uint64_t location_id,
                                                   const ObLocationSchema *&schema)
{
  int ret = OB_SUCCESS;
  schema = nullptr;
  const ObSchemaMgr *mgr = NULL;
  if (OB_UNLIKELY(!true)
             || OB_UNLIKELY(!is_valid_id(location_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(location_id), KR(ret));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(get_schema_mgr( mgr))) {
  } else if (OB_ISNULL(mgr)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("get simple schema in lazy mode not supported", KR(ret));
  } else if (OB_FAIL(mgr->get_location_schema( location_id, schema))) {
  }
  return ret;
}

}
}
}

