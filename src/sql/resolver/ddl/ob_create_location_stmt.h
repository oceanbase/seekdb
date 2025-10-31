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

 #ifndef OCEANBASE_SQL_OB_CREATE_LOCATION_STMT_H_
 #define OCEANBASE_SQL_OB_CREATE_LOCATION_STMT_H_

 #include "share/ob_rpc_struct.h"
 #include "sql/resolver/ddl/ob_ddl_stmt.h"

 namespace oceanbase
 {
 namespace sql
 {
 class ObCreateLocationStmt : public ObDDLStmt
 {
 public:
   ObCreateLocationStmt();
   explicit ObCreateLocationStmt(common::ObIAllocator *name_pool);
   virtual ~ObCreateLocationStmt();

   virtual obrpc::ObDDLArg &get_ddl_arg() { return arg_; }
   virtual bool cause_implicit_commit() const { return true; }

   obrpc::ObCreateLocationArg &get_create_location_arg() { return arg_; }

   void set_tenant_id(const uint64_t tenant_id) { arg_.schema_.set_tenant_id(tenant_id); }
   void set_user_id(const uint64_t user_id) { arg_.user_id_ = user_id; }
   void set_or_replace(bool or_replace) { arg_.or_replace_ = or_replace; }
   int set_location_name(const common::ObString &name) { return arg_.schema_.set_location_name(name); }
   int set_location_url(const common::ObString &url) { return arg_.schema_.set_location_url(url); }
   int set_location_access_info(const common::ObString &access_info) { return arg_.schema_.set_location_access_info(access_info); }
   void set_masked_sql(const common::ObString &masked_sql) { masked_sql_ = masked_sql; }
   const common::ObString& get_masked_sql() { return masked_sql_; }
   bool is_or_replace() const {return arg_.or_replace_;}

   TO_STRING_KV(K_(arg));
 private:
   obrpc::ObCreateLocationArg arg_;
   common::ObString masked_sql_;
 private:
   DISALLOW_COPY_AND_ASSIGN(ObCreateLocationStmt);
 };
 } // namespace sql
 } // namespace oceanbase

 #endif // OCEANBASE_SQL_OB_CREATE_LOCATION_STMT_H_
