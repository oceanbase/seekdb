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

#include "ob_schema_getter_guard.h"

#include "ob_schema_mgr.h"

namespace oceanbase
{

using namespace common;
using namespace observer;

namespace share
{

namespace schema
{

int ObSchemaGetterGuard::get_ai_model_schema(
                                             const ObString &ai_model_name,
                                             const ObAiModelSchema *&ai_model_schema)
{
  int ret = OB_SUCCESS;
  
  const ObSchemaMgr *mgr = nullptr;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!true
              || ai_model_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
             K(ai_model_name), KR(ret));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(get_tenant_name_case_mode(mode))) {
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  } else if (OB_FAIL(mgr->get_ai_model_schema( ai_model_name, mode, ai_model_schema))){
  }
  
  return ret;
}

int ObSchemaGetterGuard::get_ai_model_schema(
                                             const uint64_t ai_model_id,
                                             const ObAiModelSchema *&ai_model_schema)
{
  int ret = OB_SUCCESS;
  
  const ObSchemaMgr *mgr = nullptr;
  if (!check_inner_stat()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", KR(ret));
  } else if (!true
              || (OB_INVALID_ID == ai_model_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",
             K(ai_model_id), KR(ret));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->get_ai_model_schema( ai_model_id, ai_model_schema))){
  }
  
  return ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
 
