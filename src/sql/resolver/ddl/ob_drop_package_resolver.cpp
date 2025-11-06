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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_drop_package_resolver.h"
#include "ob_drop_package_stmt.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{
int ObDropPackageResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  const ParseNode *name_node = NULL;
  ObString db_name;
  ObString package_name;
  ObDropPackageStmt *package_stmt = NULL;
  if (OB_UNLIKELY(parse_tree.type_ != T_PACKAGE_DROP)
      || OB_ISNULL(parse_tree.children_)
      || OB_UNLIKELY(parse_tree.num_child_ != DROP_PACKAGE_NODE_CHILD_COUNT)
      || OB_ISNULL(name_node = parse_tree.children_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse tree is invalid", "type", get_type_name(parse_tree.type_),
             K_(parse_tree.children), K_(parse_tree.num_child), K(name_node));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else if (OB_ISNULL(schema_checker_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema checker is null");
  } else if (OB_FAIL(ObResolverUtils::resolve_sp_name(*session_info_, *name_node, db_name, package_name))) {
    LOG_WARN("resolve package name failed", K(ret));
  } else if (OB_ISNULL(package_stmt = create_stmt<ObDropPackageStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create drop package stmt failed");
  } else {
    bool is_drop_body = static_cast<bool>(parse_tree.value_);
    obrpc::ObDropPackageArg &package_arg = package_stmt->get_drop_package_arg();
    package_arg.tenant_id_ = session_info_->get_effective_tenant_id();
    package_arg.db_name_ = db_name;
    package_arg.package_name_ = package_name;
    if (is_drop_body) {
      package_arg.package_type_ = share::schema::PACKAGE_BODY_TYPE;
    } else {
      package_arg.package_type_ = share::schema::PACKAGE_TYPE;
    }
    package_arg.compatible_mode_ = COMPATIBLE_MYSQL_MODE;
  }
  return ret;
}
} //namespace sql
} //namespace oceanbase




