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

#ifndef OB_ALTER_USER_PROFILE_RESOLVER_H
#define OB_ALTER_USER_PROFILE_RESOLVER_H

#include "sql/resolver/dcl/ob_alter_user_profile_stmt.h"
#include "sql/resolver/dcl/ob_dcl_resolver.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace sql
{
class ObAlterUserProfileResolver: public ObDCLResolver
{
public:
  explicit ObAlterUserProfileResolver(ObResolverParams &params);
  virtual ~ObAlterUserProfileResolver();
  virtual int resolve(const ParseNode &parse_tree);
  
private:
  int resolve_set_role(const ParseNode &parse_tree);
  int resolve_default_role(const ParseNode &parse_tree);
  int resolve_default_role_clause(
      const ParseNode *parse_tree, 
      obrpc::ObAlterUserProfileArg &arg,
      const ObIArray<uint64_t> &role_id_array,
      bool for_default_role_stmt);
  int resolve_role_list(
      const ParseNode *role_list, 
      obrpc::ObAlterUserProfileArg &arg,
      const ObIArray<uint64_t> &role_id_array,
      bool for_default_role_stmt);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObAlterUserProfileResolver);
};

} // end namespace sql
} // end namespace oceanbase



#endif // OB_ALTER_USER_PROFILE_RESOLVER_H
