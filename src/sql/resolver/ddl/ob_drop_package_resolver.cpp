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
int ObDropPackageResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  const peg::Node &name_node = peg::package_name(parse_tree);
  ObString db_name;
  ObString package_name;
  ObDropPackageStmt *package_stmt = NULL;
  if (parse_tree.kind() != peg::NodeKind::DropPackage) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(schema_checker_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(ObResolverUtils::resolve_sp_name(*session_info_, peg::name_parts(name_node), *allocator_, db_name, package_name))) {
  } else if (OB_ISNULL(package_stmt = create_stmt<ObDropPackageStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create drop package stmt failed");
  } else {
    bool is_drop_body = peg::package_drop_body(parse_tree);
    obcall::ObDropPackageArg &package_arg = package_stmt->get_drop_package_arg();
    
    
    package_arg.db_name_ = db_name;
    package_arg.package_name_ = package_name;
    if (is_drop_body) {
      package_arg.package_type_ = share::schema::PACKAGE_BODY_TYPE;
    } else {
      package_arg.package_type_ = share::schema::PACKAGE_TYPE;
    }
  }
  return ret;
}
} //namespace sql
} //namespace oceanbase



