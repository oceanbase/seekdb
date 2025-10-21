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

#ifndef OCEANBASE_SQL_RESOLVER_DCL_OB_DCL_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_DCL_OB_DCL_RESOLVER_
#include "sql/resolver/ob_stmt_resolver.h"
#include "share/schema/ob_table_schema.h"
namespace oceanbase
{
namespace sql
{
class ObDCLResolver : public ObStmtResolver
{
public:
  explicit ObDCLResolver(ObResolverParams &params) :
      ObStmtResolver(params)
  {
  }
  virtual ~ObDCLResolver()
  {
  }
  static int mask_password_for_passwd_node(ObIAllocator *allocator,
                                           const common::ObString &src,
                                           const ParseNode *passwd_node,
                                           common::ObString &masked_sql,
                                           bool skip_enclosed_char = false);
  int resolve_user_list_node(ParseNode *user_node,
                             ParseNode *top_node,
                             common::ObString &user_name,
                             common::ObString &host_name);

  int resolve_user_host(const ParseNode *user_pass,
                        common::ObString &user_name,
                        common::ObString &host_name);

protected:
  int check_and_convert_name(common::ObString &db, common::ObString &table);
  int check_password_strength(common::ObString &password);
  int check_user_name(common::ObString &password, const common::ObString &user_name);
  int check_dcl_on_inner_user(const ObItemType &type,
                              const uint64_t &session_user_id,
                              const ObString &user_name,
                              const ObString &host_name);
  int check_dcl_on_inner_user(const ObItemType &type,
                              const uint64_t &session_user_id,
                              const uint64_t &user_id);

  static int mask_password_for_single_user(ObIAllocator *allocator,
                                           const common::ObString &src,
                                           const ParseNode *user_node,
                                           int64_t pwd_idx,
                                           common::ObString &masked_sql);
  static int mask_password_for_users(ObIAllocator *allocator,
                                     const common::ObString &src,
                                     const ParseNode *users,
                                     int64_t pwd_idx,
                                     common::ObString &masked_sql);
  enum ObPasswordPolicy {LOW = 0, MEDIUM};
  static const char password_mask_ = '*';
private:
  DISALLOW_COPY_AND_ASSIGN(ObDCLResolver);
};
}  // namespace sql
}  // namespace oceanbase
#endif //OCEANBASE_SQL_RESOLVER_DCL_OB_DCL_RESOLVER_
