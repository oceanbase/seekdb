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

namespace oceanbase {
namespace rootserver {

class MockObDDLOperator : public ObDDLOperator {
 public:
  MOCK_METHOD3(create_tenant,
      int(share::schema::TenantSchema &tenant_schema, share::schema::ObTenantResource &tenant_resource, common::ObMySQLTransaction &trans));
  MOCK_METHOD2(drop_tenant,
      int(const uint64_t tenant_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD2(create_database,
      int(share::schema::DatabaseSchema &database_schema, common::ObMySQLTransaction &trans));
  MOCK_METHOD3(drop_database,
      int(const uint64_t tenant_id, const uint64_t database_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD2(create_tablegroup,
      int(share::schema::TablegroupSchema &tablegroup_schema, common::ObMySQLTransaction &trans));
  MOCK_METHOD3(drop_tablegroup,
      int(const uint64_t tenant_id, const uint64_t tablegroup_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD5(init_default_tenant_env,
      int(const uint64_t tenant_id, const uint64_t database_id, const uint64_t tablegroup_id, const uint64_t user_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD2(create_table,
      int(const share::schema::TableSchema &table_schema, common::ObMySQLTransaction &trans));
  MOCK_METHOD3(init_tenant_tablegroup,
      int(const uint64_t tenant_id, const uint64_t tablegroup_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD3(init_tenant_database,
      int(const uint64_t tenant_id, const uint64_t database_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD3(init_tenant_user,
      int(const uint64_t tenant_id, const uint64_t user_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD4(init_tenant_database_priv,
      int(const uint64_t tenant_id, const uint64_t user_id, const uint64_t database_id, common::ObMySQLTransaction &trans));
  MOCK_METHOD2(init_tenant_sys_params,
      int(const uint64_t tenant_id, common::ObMySQLTransaction &trans));
};

}  // namespace rootserver
}  // namespace oceanbase
