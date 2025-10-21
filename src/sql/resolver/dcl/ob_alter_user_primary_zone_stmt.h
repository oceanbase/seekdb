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

#ifndef OB_ALTER_USER_PRIMARY_ZONE_STMT_H_
#define OB_ALTER_USER_PRIMARY_ZONE_STMT_H_

#include "sql/resolver/ddl/ob_ddl_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObAlterUserPrimaryZoneStmt: public ObDDLStmt
{
public:
  ObAlterUserPrimaryZoneStmt();
  explicit ObAlterUserPrimaryZoneStmt(common::ObIAllocator *name_pool);
  virtual ~ObAlterUserPrimaryZoneStmt();
  virtual obrpc::ObDDLArg &get_ddl_arg() { return arg_; }
  void set_tenant_id(int64_t tenant_id) { arg_.database_schema_.set_tenant_id(tenant_id); } 
  int set_database_name(const ObString &database_name) 
      { return arg_.database_schema_.set_database_name(database_name); }
  int set_primary_zone(const ObString &primary_zone) 
      { return OB_SUCCESS; } // not supported
  int add_primary_zone_option() 
      { return arg_.alter_option_bitset_.add_member(obrpc::ObAlterDatabaseArg::PRIMARY_ZONE); }
  TO_STRING_KV(K_(stmt_type), K_(arg));
public:
  // data members
  obrpc::ObAlterDatabaseArg arg_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterUserPrimaryZoneStmt);
};
} // end namespace sql
} // end namespace oceanbase

#endif //OB_ALTER_USER_PROFILE_STMT_H_
