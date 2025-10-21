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

#ifndef OCEANBASE_SQL_RESOVLER_DCL_OB_SET_PASSWORD_RESOLVER_
#define OCEANBASE_SQL_RESOVLER_DCL_OB_SET_PASSWORD_RESOLVER_
#include "sql/resolver/dcl/ob_set_password_stmt.h"
#include "sql/resolver/dcl/ob_dcl_resolver.h"
namespace oceanbase
{
namespace sql
{
class ObSetPasswordResolver: public ObDCLResolver
{
public:
  explicit ObSetPasswordResolver(ObResolverParams &params);
  virtual ~ObSetPasswordResolver();

  virtual int resolve(const ParseNode &parse_tree);

  static bool is_hex_literal(const common::ObString &str);

  static bool is_valid_mysql41_passwd(const common::ObString &str);
private:
  int resolve_require_node(const ParseNode &require_info, const common::ObString &user_name,
                          const common::ObString &host_name, ObSSLType &ssl_type, ObString *infos);
  int resolve_resource_option_node(const ParseNode &resource_options,
                                   const common::ObString &user_name,
                                   const common::ObString &host_name,
                                   ObSSLType &ssl_type, ObString *infos);
  int check_role_as_user(ParseNode *user_hostname_node, bool &is_valid);
private:
  const static uint64_t MAX_CONNECTIONS = 4294967295;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObSetPasswordResolver);
};

} // end namespace sql
} // end namespace oceanbase
#endif
