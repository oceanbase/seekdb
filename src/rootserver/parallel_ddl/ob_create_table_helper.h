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
#ifndef OCEANBASE_ROOTSERVER_OB_CREATE_TABLE_HELPER_H_
#define OCEANBASE_ROOTSERVER_OB_CREATE_TABLE_HELPER_H_

#include "rootserver/parallel_ddl/ob_ddl_helper.h"
#include "lib/hash/ob_hashmap.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
class ObMockFKParentTableSchema;
}
}
namespace obrpc
{
class ObCreateTableArg;
class ObCreateTableRes;
}
namespace rootserver
{
class ObCreateTableHelper : public ObDDLHelper
{
public:

class MockFKParentTableNameWrapper {
public:
  MockFKParentTableNameWrapper()
   : parent_database_(),
     parent_table_() {}
  MockFKParentTableNameWrapper(
     const common::ObString &parent_database,
     const common::ObString &parent_table)
   : parent_database_(parent_database),
     parent_table_(parent_table) {}
  ~MockFKParentTableNameWrapper() {}
  uint64_t hash() const;
  int hash(uint64_t &hash_val) const;
  bool operator==(const MockFKParentTableNameWrapper &rv) const;
  TO_STRING_KV(K_(parent_database), K_(parent_table));
private:
  common::ObString parent_database_;
  common::ObString parent_table_;
};

public:
  ObCreateTableHelper(
    share::schema::ObMultiVersionSchemaService *schema_service,
    const uint64_t tenant_id,
    const obrpc::ObCreateTableArg &arg,
    obrpc::ObCreateTableRes &res,
    ObDDLSQLTransaction *external_trans = nullptr);
  virtual ~ObCreateTableHelper();
  TO_STRING_KV(K_(arg),
               K_(res),
               K_(replace_mock_fk_parent_table_id),
               K_(new_tables),
               K_(new_mock_fk_parent_tables),
               K_(new_sequences),
               K_(has_index));
private:
  virtual int init_() override;

  virtual int lock_objects_() override;
  virtual int generate_schemas_() override;
  virtual int operate_schemas_() override;
  virtual int calc_schema_version_cnt_() override;
  virtual int operation_before_commit_() override;
  virtual int clean_on_fail_commit_() override;
  virtual int construct_and_adjust_result_(int &return_ret) override;
  int create_schemas_();
  int create_tablets_();
  int add_index_name_to_cache_();

  int lock_database_by_obj_name_();
  int lock_objects_by_name_();
  int lock_objects_by_id_();
  int post_lock_objects_by_id_();
  int check_ddl_conflict_();

  int prefetch_schemas_();
  int check_and_set_database_id_();
  int check_table_name_();
  int set_tablegroup_id_();
  int check_and_set_parent_table_id_();

  int generate_table_schema_();
  int generate_aux_table_schemas_();
  int generate_foreign_keys_();
  int generate_sequence_object_();
  int get_mock_fk_parent_table_info_(
      const obrpc::ObCreateForeignKeyArg &foreign_key_arg,
      share::schema::ObForeignKeyInfo &foreign_key_info,
      share::schema::ObMockFKParentTableSchema *&new_mock_fk_parent_table_schema);
  int try_replace_mock_fk_parent_table_(
      share::schema::ObMockFKParentTableSchema *&new_mock_fk_parent_table);
  int check_fk_columns_type_for_replacing_mock_fk_parent_table_(
      const share::schema::ObTableSchema &parent_table_schema,
      const share::schema::ObMockFKParentTableSchema &mock_parent_table_schema);

  int create_sequence_();
  int create_tables_();
  int deal_with_mock_fk_parent_tables_();
private:
  const obrpc::ObCreateTableArg &arg_;
  obrpc::ObCreateTableRes &res_;
  // replace_mock_fk_parent_table_id_ is valid if table name is same with existed mock fk parent table
  uint64_t replace_mock_fk_parent_table_id_;
  // new table schema for data/index/lob tables
  common::ObArray<ObTableSchema> new_tables_;
  common::ObArray<ObMockFKParentTableSchema *> new_mock_fk_parent_tables_;
  common::hash::ObHashMap<MockFKParentTableNameWrapper, share::schema::ObMockFKParentTableSchema*> new_mock_fk_parent_table_map_;
  common::ObArray<ObSequenceSchema *> new_sequences_;
  bool has_index_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCreateTableHelper);
};

} // end namespace rootserver
} // end namespace oceanbase


#endif//OCEANBASE_ROOTSERVER_OB_CREATE_TABLE_HELPER_H_
