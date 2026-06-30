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
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
using namespace common;
using namespace observer;

namespace share
{
namespace schema {
int ObSchemaGetterGuard::get_ccl_rule_with_name(const common::ObString &name,
    const ObCCLRuleSchema *&ccl_rule_schema) {
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  ccl_rule_schema = NULL;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  const ObSimpleCCLRuleSchema *simple_ccl_rule_schema = nullptr;
  if (OB_UNLIKELY(!true || name.empty())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(name));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(get_tenant_name_case_mode(mode))) {
  } else if (OB_NAME_CASE_INVALID == mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid case mode", K(ret), K(mode));
  } else if (OB_FAIL(mgr->ccl_rule_mgr_.get_schema_by_name(mode, name, simple_ccl_rule_schema))) {
  } else if (NULL == simple_ccl_rule_schema) {
    LOG_INFO("ccl rule not exist", K(name));
  } else if (OB_FAIL(get_schema(
                 CCL_RULE_SCHEMA,
                 simple_ccl_rule_schema->get_ccl_rule_id(), ccl_rule_schema,
                 simple_ccl_rule_schema->get_schema_version()))) {
  } else if (OB_ISNULL(ccl_rule_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(ccl_rule_schema));
  } else {
    const_cast<ObCCLRuleSchema *>(ccl_rule_schema)
        ->set_name_case_mode(simple_ccl_rule_schema->get_name_case_mode());
  }
  return ret;
}

int ObSchemaGetterGuard::get_ccl_rule_with_ccl_rule_id(const uint64_t ccl_rule_id,
    const ObCCLRuleSchema *&ccl_rule_schema) {
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  ccl_rule_schema = NULL;
  const ObSimpleCCLRuleSchema *simple_ccl_rule_schema = nullptr;
  if (OB_UNLIKELY(!true ||
                         ccl_rule_id == common::OB_INVALID_ID)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(ccl_rule_id));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else if (OB_FAIL(mgr->ccl_rule_mgr_.get_schema_by_id(
                 ccl_rule_id, simple_ccl_rule_schema))) {
  } else if (NULL == simple_ccl_rule_schema) {
    LOG_INFO("ccl rule not exist", K(ccl_rule_id));
  } else if (OB_FAIL(get_schema(
                 CCL_RULE_SCHEMA,
                 simple_ccl_rule_schema->get_ccl_rule_id(), ccl_rule_schema,
                 simple_ccl_rule_schema->get_schema_version()))) {
  } else if (OB_ISNULL(ccl_rule_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", KR(ret), KP(ccl_rule_schema));
  } else {
    const_cast<ObCCLRuleSchema *>(ccl_rule_schema)
        ->set_name_case_mode(simple_ccl_rule_schema->get_name_case_mode());
  }
  return ret;
}

int ObSchemaGetterGuard::get_ccl_rule_infos(
    CclRuleContainsInfo contians_info,
    ObCCLRuleMgr::CCLRuleInfos *&ccl_rule_infos) {
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  ccl_rule_infos = NULL;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    ccl_rule_infos =
        const_cast<ObSchemaMgr *>(mgr)
            ->ccl_rule_mgr_.get_ccl_rule_belong_ccl_rule_infos(contians_info);
  }
  return ret;
}

int ObSchemaGetterGuard::get_ccl_rule_count(uint64_t &count) {
  int ret = OB_SUCCESS;
  const ObSchemaMgr *mgr = NULL;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(check_tenant_schema_guard())) {
  } else if (OB_FAIL(check_lazy_guard( mgr))) {
  } else {
    count = const_cast<ObSchemaMgr *>(mgr)->ccl_rule_mgr_.get_ccl_rule_count();
  }
  return ret;
}

} // end of namespace schema
} //end of namespace share
} //end of namespace oceanbase
