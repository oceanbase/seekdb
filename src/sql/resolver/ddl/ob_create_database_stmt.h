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

#ifndef OCEANBASE_SQL_OB_CREATE_DATABASE_STMT_H_
#define OCEANBASE_SQL_OB_CREATE_DATABASE_STMT_H_

#include "share/ob_rpc_struct.h"
#include "sql/resolver/ddl/ob_ddl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObCreateDatabaseStmt : public ObDDLStmt
  {
  public:
    ObCreateDatabaseStmt();
    explicit ObCreateDatabaseStmt(common::ObIAllocator *name_pool);
    virtual ~ObCreateDatabaseStmt();
    void set_if_not_exists(bool if_not_exists);
    void set_tenant_id(const uint64_t tenant_id);
    void set_database_id(const uint64_t database_id);
    int set_database_name(const common::ObString &database_name);
    void set_collation_type(const common::ObCollationType type);
    void set_charset_type(const common::ObCharsetType type);
    common::ObCharsetType get_charset_type() const;
    common::ObCollationType get_collation_type() const;
    const common::ObString &get_database_name() const;
    int set_primary_zone(const common::ObString &zone);
    void set_read_only(const bool read_only);
    int set_default_tablegroup_name(const common::ObString &tablegroup_name);
    obrpc::ObCreateDatabaseArg &get_create_database_arg();
    virtual bool cause_implicit_commit() const { return true; }
    virtual obrpc::ObDDLArg &get_ddl_arg() { return create_database_arg_; }

    TO_STRING_KV(K_(create_database_arg));
  private:
    DISABLE_WARNING_GCC_PUSH
    DISABLE_WARNING_GCC_ATTRIBUTES
    bool is_charset_specify_ __maybe_unused;
    bool is_collation_specify_ __maybe_unused;
    DISABLE_WARNING_GCC_POP
    obrpc::ObCreateDatabaseArg create_database_arg_;
  };
}//namespace sql
}//namespace oceanbase
#endif //OCEANBASE_SQL_OB_CREATE_DATABASE_STMT_H_
