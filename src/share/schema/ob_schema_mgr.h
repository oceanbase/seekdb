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

#ifndef OB_OCEANBASE_SCHEMA_OB_SCHEMA_MGR_H_
#define OB_OCEANBASE_SCHEMA_OB_SCHEMA_MGR_H_

#include <stdint.h>
#include "share/ob_define.h"
#include "lib/container/ob_vector.h"
#include "lib/allocator/page_arena.h"
#include "lib/hash/ob_pointer_hashmap.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_priv_mgr.h"
#include "share/schema/ob_outline_mgr.h"
#include "share/schema/ob_package_mgr.h"
#include "share/schema/ob_routine_mgr.h"
#include "share/schema/ob_trigger_mgr.h"
#include "share/schema/ob_sys_variable_mgr.h"
#include "share/schema/ob_mock_fk_parent_table_mgr.h"
#include "share/schema/ob_ai_model_mgr.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace share
{
namespace schema
{
class ObServerSchemaService;
class ObSchemaGetterGuard;

class ObSimpleServerRuntimeSchema : public ObSchema
{
public:
  ObSimpleServerRuntimeSchema();
  explicit ObSimpleServerRuntimeSchema(common::ObIAllocator *allocator);
  ObSimpleServerRuntimeSchema(const ObSimpleServerRuntimeSchema &src_schema);
  virtual ~ObSimpleServerRuntimeSchema();
  ObSimpleServerRuntimeSchema &operator =(const ObSimpleServerRuntimeSchema &other);
  TO_STRING_KV(
               K_(schema_version),
               K_(runtime_name),
               K_(name_case_mode),
               K_(read_only),
               K_(gmt_modified),
               K_(status),
               K_(in_recyclebin));
  virtual void reset();
  bool is_valid() const;
  inline int64_t get_convert_size() const;
  
  
  inline void set_schema_version(const int64_t schema_version) { schema_version_ = schema_version; }
  inline int64_t get_schema_version() const { return schema_version_; }
  inline int set_runtime_name(const common::ObString &runtime_name)
  { return deep_copy_str(runtime_name, runtime_name_); }
  inline const char *get_runtime_name() const { return extract_str(runtime_name_); }
  inline const common::ObString &get_runtime_name_str() const { return runtime_name_; }
  inline void set_name_case_mode(const common::ObNameCaseMode cmp_mode) { name_case_mode_ = cmp_mode; }
  inline common::ObNameCaseMode get_name_case_mode() const { return name_case_mode_; }
  inline void set_read_only(const bool read_only) { read_only_ = read_only; }
  inline bool get_read_only() const { return read_only_; }

  inline void set_gmt_modified(const int64_t gmt_modified) { gmt_modified_ = gmt_modified; }
  inline int64_t get_gmt_modified() const { return gmt_modified_; }

  inline bool is_dropping() const { return SERVER_RUNTIME_STATUS_DROPPING == status_; }
  inline bool is_in_recyclebin() const { return in_recyclebin_; }
  inline bool is_creating() const { return SERVER_RUNTIME_STATUS_CREATING == status_;}
  inline bool is_restore() const { return SERVER_RUNTIME_STATUS_RESTORE == status_
                                          || SERVER_RUNTIME_STATUS_CREATING_STANDBY == status_;}
  inline bool is_normal() const { return SERVER_RUNTIME_STATUS_NORMAL == status_; }
  inline bool is_creating_standby_server_status() const { return SERVER_RUNTIME_STATUS_CREATING_STANDBY == status_; }
  inline void set_status(const ObServerRuntimeStatus status) { status_ = status; }
  inline ObServerRuntimeStatus get_status() const { return status_; }
  inline void set_in_recyclebin(const bool in_recyclebin) { in_recyclebin_ = in_recyclebin; }
private:
  
  int64_t schema_version_;
  common::ObString runtime_name_;
  common::ObNameCaseMode name_case_mode_; //deprecated
  bool read_only_;  // Subject to the value of the system variable
  int64_t gmt_modified_;
  ObServerRuntimeStatus status_;
  bool in_recyclebin_;
};

class ObSimpleUserSchema : public ObSchema
{
public:
  ObSimpleUserSchema();
  explicit ObSimpleUserSchema(common::ObIAllocator *allocator);
  ObSimpleUserSchema(const ObSimpleUserSchema &src_schema);
  virtual ~ObSimpleUserSchema();
  ObSimpleUserSchema &operator =(const ObSimpleUserSchema &other);
  TO_STRING_KV(
               K_(user_id),
               K_(schema_version),
               K_(user_name),
               K_(host_name),
               K_(type));
  virtual void reset();
  inline bool is_valid() const;
  inline int64_t get_convert_size() const;
  
  
  inline void set_user_id(const uint64_t user_id) { user_id_ = user_id; }
  inline uint64_t get_user_id() const { return user_id_; }
  inline void set_schema_version(const int64_t schema_version) { schema_version_ = schema_version; }
  inline int64_t get_schema_version() const { return schema_version_; }
  inline int set_user_name(const common::ObString &user_name)
  { return deep_copy_str(user_name, user_name_); }
  inline int set_host(const common::ObString &host_name)
  { return deep_copy_str(host_name, host_name_); }
  inline const char *get_user_name() const { return extract_str(user_name_); }
  inline const char *get_host_name() const { return extract_str(host_name_); }
  inline const common::ObString &get_user_name_str() const { return user_name_; }
  inline const common::ObString &get_host_name_str() const { return host_name_; }
  inline void set_type(const uint64_t type) { type_ = type; }
  inline uint64_t get_type() const { return type_; }
  inline bool is_role() const { return OB_ROLE == type_; }

private:
  
  uint64_t user_id_;
  int64_t schema_version_;
  common::ObString user_name_;
  common::ObString host_name_;
  uint64_t type_;
};

class ObSimpleDatabaseSchema : public ObSchema
{
public:
  ObSimpleDatabaseSchema();
  explicit ObSimpleDatabaseSchema(common::ObIAllocator *allocator);
  ObSimpleDatabaseSchema(const ObSimpleDatabaseSchema &src_schema);
  virtual ~ObSimpleDatabaseSchema();
  ObSimpleDatabaseSchema &operator =(const ObSimpleDatabaseSchema &other);
  TO_STRING_KV(K_(database_id),
               K_(schema_version),
               K_(database_name),
               K_(name_case_mode));
  virtual void reset();
  inline bool is_valid() const;
  inline int64_t get_convert_size() const;
  
  
  inline void set_database_id(const uint64_t database_id) { database_id_ = database_id; }
  inline uint64_t get_database_id() const { return database_id_; }
  inline void set_schema_version(const int64_t schema_version) { schema_version_ = schema_version; }
  inline int64_t get_schema_version() const { return schema_version_; }
  inline int set_database_name(const common::ObString &database_name)
  { return deep_copy_str(database_name, database_name_); }
  inline const char *get_database_name() const { return extract_str(database_name_); }
  inline const common::ObString &get_database_name_str() const { return database_name_; }
  inline void set_name_case_mode(const common::ObNameCaseMode cmp_mode) { name_case_mode_ = cmp_mode; }
  inline common::ObNameCaseMode get_name_case_mode() const { return name_case_mode_; }
private:
  
  uint64_t database_id_;
  int64_t schema_version_;
  common::ObString database_name_;
  common::ObNameCaseMode name_case_mode_;
};

template<class K, class V>
struct GetTableKeyV2
{
  void operator()(const K &k, const V &v)
  {
    UNUSED(k);
    UNUSED(v);
  }
};
template<>
struct GetTableKeyV2<uint64_t, ObSimpleTableSchemaV2 *>
{
  uint64_t operator()(const ObSimpleTableSchemaV2 *table_schema) const
  {
    return NULL != table_schema ?
      table_schema->get_table_id() :
      common::OB_INVALID_ID;
  }
};
template<>
struct GetTableKeyV2<uint64_t, ObSimpleDatabaseSchema *>
{
  uint64_t operator()(const ObSimpleDatabaseSchema *database_schema) const
  {
    return NULL != database_schema ?
      database_schema->get_database_id() :
      common::OB_INVALID_ID;
  }
};

template<>
struct GetTableKeyV2<ObDatabaseSchemaHashWrapper, ObSimpleDatabaseSchema *>
{
  ObDatabaseSchemaHashWrapper operator()(const ObSimpleDatabaseSchema *database_schema) const
  {
    if (!OB_ISNULL(database_schema)) {
      ObDatabaseSchemaHashWrapper database_schema_hash_wrapper(
          database_schema->get_name_case_mode(),
          database_schema->get_database_name_str());
      return database_schema_hash_wrapper;
    } else {
      ObDatabaseSchemaHashWrapper null_wrap;
      return null_wrap;
    }
  }
};
template<>
struct GetTableKeyV2<ObTableSchemaHashWrapper, ObSimpleTableSchemaV2 *>
{
  ObTableSchemaHashWrapper operator()(const ObSimpleTableSchemaV2 *table_schema) const
  {
    if (!OB_ISNULL(table_schema)) {
      ObTableSchemaHashWrapper table_schema_hash_wrapper(
          table_schema->get_database_id(),
          table_schema->get_session_id(),
          table_schema->get_name_case_mode(),
          table_schema->get_table_name_str());
      return table_schema_hash_wrapper;
    } else {
      ObTableSchemaHashWrapper null_wrap;
      return null_wrap;
    }
  }
};
template<>
struct GetTableKeyV2<ObIndexSchemaHashWrapper, ObSimpleTableSchemaV2 *>
{
  ObIndexSchemaHashWrapper operator()(const ObSimpleTableSchemaV2 *index_schema) const
  {
    if (!OB_ISNULL(index_schema)) {
      if (index_schema->is_in_recyclebin()) { // index is in recyclebin
        ObIndexSchemaHashWrapper index_schema_hash_wrapper(
            index_schema->get_database_id(),
            common::OB_INVALID_ID,
            index_schema->get_table_name_str());
        return index_schema_hash_wrapper;
      } else {
        ObIndexSchemaHashWrapper index_schema_hash_wrapper(
            index_schema->get_database_id(),
            index_schema->get_data_table_id(),
            index_schema->get_origin_index_name_str());
        return index_schema_hash_wrapper;
      }
    } else {
      ObIndexSchemaHashWrapper null_wrap;
      return null_wrap;
    }
  }
};

template<>
struct GetTableKeyV2<ObAuxVPSchemaHashWrapper, ObSimpleTableSchemaV2 *>
{
  ObAuxVPSchemaHashWrapper operator()(const ObSimpleTableSchemaV2 *aux_vp_schema) const
  {
    if (!OB_ISNULL(aux_vp_schema)) {
      ObAuxVPSchemaHashWrapper aux_vp_schema_hash_wrapper(
          aux_vp_schema->get_database_id(),
          aux_vp_schema->get_table_name_str());
      return aux_vp_schema_hash_wrapper;
    } else {
      ObAuxVPSchemaHashWrapper null_wrap;
      return null_wrap;
    }
  }
};

template<>
struct GetTableKeyV2<ObForeignKeyInfoHashWrapper, ObSimpleForeignKeyInfo *>
{
  ObForeignKeyInfoHashWrapper operator()(const ObSimpleForeignKeyInfo *simple_foreign_key_info) const
  {
    if (OB_NOT_NULL(simple_foreign_key_info)) {
      ObForeignKeyInfoHashWrapper fk_info_hash_wrapper(simple_foreign_key_info->database_id_,
                                                       simple_foreign_key_info->foreign_key_name_);
      return fk_info_hash_wrapper;
    } else {
      ObForeignKeyInfoHashWrapper null_wrap;
      return null_wrap;
    }
  }
};

template<>
struct GetTableKeyV2<ObConstraintInfoHashWrapper, ObSimpleConstraintInfo *>
{
  ObConstraintInfoHashWrapper operator()(const ObSimpleConstraintInfo *simple_constraint_info) const
  {
    if (OB_NOT_NULL(simple_constraint_info)) {
      ObConstraintInfoHashWrapper cst_info_hash_wrapper(simple_constraint_info->database_id_,
                                                        simple_constraint_info->constraint_name_);
      return cst_info_hash_wrapper;
    } else {
      ObConstraintInfoHashWrapper null_wrap;
      return null_wrap;
    }
  }
};

class ObSchemaMgr
{
friend class ObServerSchemaService;
friend class ObSchemaGetterGuard;
friend class ObSchemaMgrCache;
friend class MockSchemaService;
typedef common::ObSortedVector<ObSimpleUserSchema *> UserInfos;
typedef common::ObSortedVector<ObSimpleDatabaseSchema *> DatabaseInfos;
typedef common::ObSortedVector<ObSimpleTableSchemaV2 *> TableInfos;
typedef UserInfos::iterator UserIterator;
typedef UserInfos::const_iterator ConstUserIterator;
typedef DatabaseInfos::iterator DatabaseIterator;
typedef DatabaseInfos::const_iterator ConstDatabaseIterator;
typedef TableInfos::iterator TableIterator;
typedef TableInfos::const_iterator ConstTableIterator;
typedef common::hash::ObPointerHashMap<ObDatabaseSchemaHashWrapper, ObSimpleDatabaseSchema *, GetTableKeyV2, 128> DatabaseNameMap;
typedef common::hash::ObPointerHashMap<uint64_t, ObSimpleTableSchemaV2 *, GetTableKeyV2, 1024> TableIdMap;
typedef common::hash::ObPointerHashMap<uint64_t, ObSimpleDatabaseSchema *, GetTableKeyV2, 128> DatabaseIdMap;
typedef common::hash::ObPointerHashMap<ObTableSchemaHashWrapper, ObSimpleTableSchemaV2 *, GetTableKeyV2, 1024> TableNameMap;
typedef common::hash::ObPointerHashMap<ObIndexSchemaHashWrapper, ObSimpleTableSchemaV2 *, GetTableKeyV2, 1024> IndexNameMap;
typedef common::hash::ObPointerHashMap<ObAuxVPSchemaHashWrapper, ObSimpleTableSchemaV2 *, GetTableKeyV2, 128> AuxVPNameMap;
typedef common::hash::ObPointerHashMap<ObAuxVPSchemaHashWrapper, ObSimpleTableSchemaV2 *, GetTableKeyV2, 128> LobMetaNameMap;
typedef common::hash::ObPointerHashMap<ObAuxVPSchemaHashWrapper, ObSimpleTableSchemaV2 *, GetTableKeyV2, 128> LobPieceNameMap;
typedef common::hash::ObPointerHashMap<ObForeignKeyInfoHashWrapper, ObSimpleForeignKeyInfo *, GetTableKeyV2, 128> ForeignKeyNameMap;
typedef common::hash::ObPointerHashMap<ObConstraintInfoHashWrapper, ObSimpleConstraintInfo *, GetTableKeyV2, 128> ConstraintNameMap;
public:
  ObSchemaMgr();
  explicit ObSchemaMgr(common::ObIAllocator &allocator);
  virtual ~ObSchemaMgr();
  int init();
  void reset();
  int assign(const ObSchemaMgr &other);
  int deep_copy(const ObSchemaMgr &other);
  void dump() const;
  inline void set_schema_version(const int64_t schema_version)
  { schema_version_ = schema_version; }
  inline int64_t get_schema_version() const { return schema_version_; }
  inline bool get_is_consistent() const { return is_consistent_; }
  // server runtime
  int add_runtime_schemas(const common::ObIArray<ObSimpleServerRuntimeSchema> &runtime_schemas);
  int add_runtime_schema(const ObSimpleServerRuntimeSchema &runtime_schema);
  int get_server_runtime_schema(
                        const ObSimpleServerRuntimeSchema *&runtime_schema) const;
  int get_server_runtime_schema(const common::ObString &runtime_name,
                        const ObSimpleServerRuntimeSchema *&runtime_schema) const;

  int get_runtime_name_case_mode(common::ObNameCaseMode &mode) const;
  int get_runtime_read_only(bool &read_only) const;

  // user
  int add_users(const common::ObIArray<ObSimpleUserSchema> &user_schemas);
  int add_user(const ObSimpleUserSchema &user_schema);
  int del_user(const ObUserId user);
  int get_user_schema(
                      const uint64_t user_id,
                      const ObSimpleUserSchema *&user_schema) const;
  int get_user_schema(
                      const common::ObString &user_name,
                      const common::ObString &host_name,
                      const ObSimpleUserSchema *&user_schema) const;
  int get_user_schema(
                      const common::ObString &user_name,
                      common::ObIArray<const ObSimpleUserSchema *> &users_schema) const;
  // database
  int add_databases(const common::ObIArray<ObSimpleDatabaseSchema> &database_schemas);
  int add_database(const ObSimpleDatabaseSchema &database_schema);
  int del_database(const ObDatabaseId database);
  int get_database_schema(
                          const uint64_t database_id,
                          const ObSimpleDatabaseSchema *&database_schema) const;
  int get_database_schema(
                          const common::ObString &database_name,
                          const ObSimpleDatabaseSchema *&database_schema) const;
  // table
  int add_tables(const common::ObIArray<ObSimpleTableSchemaV2 *> &table_schemas,
                 const bool refresh_full_schema = false);
  int add_table(const ObSimpleTableSchemaV2 &table_schema,
                common::ObArrayWrap<int64_t> *cost_array = NULL);
  int del_table(const ObTableId table);
  int remove_aux_table(const ObSimpleTableSchemaV2 &schema_to_del);
  int get_table_schema(
                       const uint64_t table_id,
                       const ObSimpleTableSchemaV2 *&table_schema) const;
  int get_table_schema(
                       const uint64_t database_id,
                       const uint64_t session_id,
                       const common::ObString &table_name,
                       const bool is_index,
                       const ObSimpleTableSchemaV2 *&table_schema,
                       const bool with_hidden_flag = false,
                       const bool is_built_in_index = false) const;
  int get_table_schema(
      const uint64_t database_id,
      const uint64_t session_id,
      const common::ObString &table_name,
      const ObSimpleTableSchemaV2 *&table_schema) const;
  int get_hidden_table_schema(
                              const uint64_t database_id,
                              const common::ObString &table_name,
                              const ObSimpleTableSchemaV2 *&table_schema) const;
  int get_index_schema(
      const uint64_t database_id,
      const common::ObString &table_name,
      const ObSimpleTableSchemaV2 *&table_schema,
      const bool is_built_in = false) const;
  int get_idx_schema_by_origin_idx_name(const uint64_t database_id,
                                      const common::ObString &index_name,
                                      const ObSimpleTableSchemaV2 *&table_schema) const;
  // foreign key
  int get_foreign_key_id(const uint64_t database_id,
                         const common::ObString &foreign_key_name,
                         uint64_t &foreign_key_id) const;
  int get_foreign_key_info(
                          const uint64_t database_id,
                          const ObString &foreign_key_name,
                          ObSimpleForeignKeyInfo &foreign_key_info) const;
  // constraint
  int get_constraint_id(const uint64_t database_id,
                        const common::ObString &constraint_name,
                        uint64_t &constraint_id) const;
  int get_constraint_info(
                        const uint64_t database_id,
                        const common::ObString &constraint_name,
                        ObSimpleConstraintInfo &constraint_info) const;

  int get_package_schema(
      const uint64_t package_id,
      const ObSimplePackageSchema *&package_schema) const;

  int get_routine_schema(
      const uint64_t routine_id,
      const ObSimpleRoutineSchema *&routine_schema) const;

  int get_trigger_schema(
      const uint64_t trigger_id,
      const ObSimpleTriggerSchema *&trigger_schema) const;

  // ai model
  int get_ai_model_schema(
      const uint64_t &ai_model_id,
      const ObAiModelSchema *&ai_model_schema) const;
  int get_ai_model_schema(
      const ObString &ai_model_name,
      const common::ObNameCaseMode &case_mode,
      const ObAiModelSchema *&ai_model_schema) const;
  // other
  int get_runtime_schemas(common::ObIArray<const ObSimpleServerRuntimeSchema *> &runtime_schemas) const;
  #define GET_SCHEMAS_IN_RUNTIME_FUNC_DECLARE(SCHEMA, SCHEMA_TYPE)     \
    int get_##SCHEMA##_schemas_in_runtime(    \
        common::ObIArray<const SCHEMA_TYPE *> &schema_array) const;
  GET_SCHEMAS_IN_RUNTIME_FUNC_DECLARE(user, ObSimpleUserSchema);
  GET_SCHEMAS_IN_RUNTIME_FUNC_DECLARE(database, ObSimpleDatabaseSchema);
  #undef GET_SCHEMAS_IN_RUNTIME_FUNC_DECLARE
  #define GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DECLARE(DST_SCHEMA)    \
  int get_table_schemas_in_##DST_SCHEMA(                              \
      const uint64_t dst_schema_id,                                   \
      common::ObIArray<const ObSimpleTableSchemaV2 *> &schema_array) const;
  GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DECLARE(database);
  int get_table_schemas_in_runtime(common::ObIArray<const ObSimpleTableSchemaV2 *> &schema_array) const;
  #undef GET_TABLE_SCHEMAS_IN_DST_SCHEMA_FUNC_DECLARE
  int get_vector_index_schemas_in_runtime(
      common::ObIArray<const ObSimpleTableSchemaV2 *> &schema_array) const;
  int get_aux_schemas(
                      const uint64_t data_table_id,
                      common::ObIArray<const ObSimpleTableSchemaV2 *> &aux_vp_schemas,
                      const share::schema::ObTableType table_type) const;

  
  


  /*schema statistics*/
  int get_schema_size(int64_t &total_size) const;
  int get_schema_count(int64_t &schema_count) const;
  int get_schema_statistics(common::ObIArray<ObSchemaStatisticsInfo> &schema_infos) const;

  // get virtual table id or sys view id
  int get_non_sys_table_ids(ObIArray<uint64_t> &non_sys_table_ids) const;

  int64_t get_timestamp_in_slot() const { return timestamp_in_slot_; };
  void set_timestamp_in_slot(const int64_t timestamp) { timestamp_in_slot_ = timestamp; }
  int64_t get_allocator_idx() const { return allocator_idx_; }
  void set_allocator_idx(const int64_t allocator_idx) { allocator_idx_ = allocator_idx; }

  int deep_copy_index_name_map(
      common::ObIAllocator &allocator,
      ObIndexNameMap &index_name_cache);
private:
  inline bool check_inner_stat() const;

  int remove_aux_table(const ObSimpleTableSchemaV2 &schema_to_del, const bool is_aux_vp);

  int add_foreign_keys_in_table(const common::ObIArray<ObSimpleForeignKeyInfo> &fk_info_array,
                                const int over_write);
  int delete_given_fk_from_mgr(const ObSimpleForeignKeyInfo &fk_info);
  int delete_foreign_keys_in_table(const ObSimpleTableSchemaV2 &table_schema);
  int check_and_delete_given_fk_in_table(const ObSimpleTableSchemaV2 *replaced_table, const ObSimpleTableSchemaV2 *new_table);

  int add_constraints_in_table(const ObSimpleTableSchemaV2 *new_table_schema,
                               const int over_write);
  int delete_given_cst_from_mgr(const ObSimpleConstraintInfo &cst_info);
  int delete_constraints_in_table(const ObSimpleTableSchemaV2 &table_schema);
  int check_and_delete_given_cst_in_table(const ObSimpleTableSchemaV2 *replaced_table, const ObSimpleTableSchemaV2 *new_table);

  inline static bool compare_user(const ObSimpleUserSchema *lhs,
                                      const ObSimpleUserSchema *rhs);
  inline static bool equal_user(const ObSimpleUserSchema *lhs,
                                    const ObSimpleUserSchema *rhs);
  inline static bool compare_with_user_id(const ObSimpleUserSchema *lhs,
                                                     const ObUserId &user_id);
  inline static bool equal_with_user_id(const ObSimpleUserSchema *lhs,
                                                   const ObUserId &user_id);
  inline static bool compare_database(const ObSimpleDatabaseSchema *lhs,
                                      const ObSimpleDatabaseSchema *rhs);
  inline static bool equal_database(const ObSimpleDatabaseSchema *lhs,
                                    const ObSimpleDatabaseSchema *rhs);
  inline static bool compare_with_database_id(const ObSimpleDatabaseSchema *lhs,
                                                     const ObDatabaseId &database_id);
  inline static bool equal_with_database_id(const ObSimpleDatabaseSchema *lhs,
                                                   const ObDatabaseId &database_id);
  inline static bool compare_table(const ObSimpleTableSchemaV2 *lhs,
                                   const ObSimpleTableSchemaV2 *rhs);
  inline static bool compare_aux_table(const ObSimpleTableSchemaV2 *lhs,
                                       const ObSimpleTableSchemaV2 *rhs);
  //inline static bool compare_table_with_data_table_id(const ObSimpleTableSchemaV2 *lhs,
  //                                                    const ObSimpleTableSchemaV2 *rhs);
  inline static bool equal_table(const ObSimpleTableSchemaV2 *lhs,
                                 const ObSimpleTableSchemaV2 *rhs);
  inline static bool compare_with_table_id(const ObSimpleTableSchemaV2 *lhs,
                                                  const ObTableId &table_id);
  inline static bool compare_with_data_table_id(const ObSimpleTableSchemaV2 *lhs,
                                                const ObTableId &table_id);
  inline static bool equal_with_table_id(const ObSimpleTableSchemaV2 *lhs,
                                                const ObTableId &table_id);
  int deal_with_table_rename(const ObSimpleTableSchemaV2 &old_table_schema,
                             const ObSimpleTableSchemaV2 &new_table_schema);
  int deal_with_db_rename(const ObSimpleDatabaseSchema &old_db_schema,
                          const ObSimpleDatabaseSchema &new_db_schema);
  // 1. hidden table to non-hidden table, you need to remove it from the hidden_table_name_map_
  // 2. non-hidden table to hidden table, you need to remove it from the normal map
  int deal_with_change_table_state(const ObSimpleTableSchemaV2 &old_table_schema,
                                   const ObSimpleTableSchemaV2 &new_table_schema);

  // schema meta consistent related
  bool check_schema_meta_consistent();
  int rebuild_schema_meta_if_not_consistent();
  int rebuild_table_hashmap(uint64_t &fk_cnt, uint64_t &cst_cnt);
  int rebuild_db_hashmap();

  /*schema statistics*/
  int get_runtime_statistics(ObSchemaStatisticsInfo &schema_info) const;
  int get_user_statistics(ObSchemaStatisticsInfo &schema_info) const;
  int get_database_statistics(ObSchemaStatisticsInfo &schema_info) const;
  int get_table_statistics(ObSchemaStatisticsInfo &schema_info) const;

  int reserved_mem_for_tables_(
      const common::ObIArray<share::schema::ObSimpleTableSchemaV2*> &table_schemas);
  IndexNameMap &get_index_name_map_(const bool is_built_in)
  {
    return is_built_in ? built_in_index_name_map_ : normal_index_name_map_;
  }
  const IndexNameMap &get_index_name_map_(const bool is_built_in) const
  {
    return is_built_in ? built_in_index_name_map_ : normal_index_name_map_;
  }
  // ai model
  int add_ai_models(const common::ObIArray<ObAiModelSchema> &ai_model_schemas);
  int add_ai_model(const ObAiModelSchema &ai_model_schema);
  int del_ai_model(const ObAiModelId &ai_model_id);
private:
  common::ObArenaAllocator local_allocator_;
  common::ObIAllocator &allocator_;
  int64_t schema_version_;
  
  bool is_consistent_;
  ObSimpleServerRuntimeSchema *runtime_info_ = nullptr;
  UserInfos user_infos_;
  DatabaseInfos database_infos_;
  DatabaseNameMap database_name_map_;
  TableInfos table_infos_;
  TableInfos index_infos_;
  TableInfos aux_vp_infos_;
  TableInfos lob_meta_infos_;
  TableInfos lob_piece_infos_;
  TableIdMap table_id_map_;
  TableNameMap table_name_map_;
  IndexNameMap normal_index_name_map_;
  AuxVPNameMap aux_vp_name_map_;
  ObOutlineMgr outline_mgr_;
  ObRoutineMgr routine_mgr_;
  ObPrivMgr priv_mgr_;
  ObPackageMgr package_mgr_;
  ObTriggerMgr trigger_mgr_;
  ForeignKeyNameMap foreign_key_name_map_;
  ConstraintNameMap constraint_name_map_;
  ObSysVariableMgr sys_variable_mgr_;
  // Map of tables with HIDDEN flag (is_user_hidden_table())
  TableNameMap hidden_table_name_map_;
  // Map of index tables with following attributes:
  // 1. with no HIDDEN flag：is_user_hidden_table() == false
  // 2. system built-in index tables when creating index
  // 3. they are not visible to users, and their names are not in normal index name space. Their names
  //    are not conflicted with normal index names
  IndexNameMap built_in_index_name_map_;
  ObMockFKParentTableMgr mock_fk_parent_table_mgr_;
  int64_t timestamp_in_slot_; // when schema mgr put in slot, we will set the timestamp
  int64_t allocator_idx_;
  ObAiModelMgr ai_model_mgr_;
};

}//end of namespace schema
}//end of namespace share
}//end of namespace oceanbase
#endif //OB_OCEANBASE_SCHEMA_OB_SCHEMA_MGR_H_
