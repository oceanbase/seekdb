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
#ifndef OCEANBASE_ROOTSERVER_OB_ALTER_TABLE_CONSTRAINT_CHECKER_H_
#define OCEANBASE_ROOTSERVER_OB_ALTER_TABLE_CONSTRAINT_CHECKER_H_
#include "lib/ob_define.h"
#include "share/ob_ddl_common.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
class AlterTableSchema; 
}
}
namespace obrpc
{
class ObAlterTableArg;
}

namespace rootserver
{
class ObDDLService;
class ObAlterTableConstraintChecker {
public:

  static int check_can_change_cst_column_name(
      const obrpc::ObAlterTableArg &alter_table_arg,
      const share::schema::ObTableSchema &orig_table_schema,
      bool &can_modify_column_name_and_constraint);

  static int check_can_add_cst_on_two_column(
      const obrpc::ObAlterTableArg &alter_table_arg,
      bool &can_add_cst_on_two_column);
  static int check_is_change_cst_column_name(
        const share::schema::ObTableSchema &table_schema,
        const share::schema::AlterTableSchema &alter_table_schema,
        bool &change_cst_column_name);
  static int check_alter_table_constraint(
      rootserver::ObDDLService &ddl_service,
      const obrpc::ObAlterTableArg &alter_table_arg,
      const share::schema::ObTableSchema &orig_table_schema,
      share::ObDDLType &ddl_type);
  static int need_modify_not_null_constraint_validate(
      rootserver::ObDDLService &ddl_service, 
      const obrpc::ObAlterTableArg &alter_table_arg,
      bool &is_add_not_null_col,
      bool &need_modify);
  static int modify_not_null_constraint_validate(
      const obrpc::ObAlterTableArg &alter_table_arg,
      AlterTableSchema &alter_table_schema);
};

} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_ALTER_TABLE_CONSTRAINT_CHECKER_H_
