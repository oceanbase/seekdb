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

#ifndef _OB_STMT_RESOLVER_H
#define _OB_STMT_RESOLVER_H 1
#include "sql/parser/parse_node.h"
#include "sql/resolver/ob_stmt.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/oblog/ob_log.h"
#include "share/schema/ob_column_schema.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/session/ob_basic_session_info.h"

namespace oceanbase
{
namespace common
{
class ObMySQLProxy;
}
namespace share
{
}
namespace sql
{

/// base class of all statement resolver
class ObStmtResolver
{
public:
  explicit ObStmtResolver(ObResolverParams &params)
      : allocator_(params.allocator_),
        schema_checker_(params.schema_checker_),
        session_info_(params.session_info_),
        params_(params),
        stmt_(NULL)
  {
  }
  virtual ~ObStmtResolver() {}

  virtual int resolve(const ParseNode &parse_tree) = 0;
  inline ObStmt *get_basic_stmt() { return stmt_; }
  inline void set_basic_stmt(ObStmt *stmt) { stmt_ = stmt; }
  int resolve_table_relation_factor(const ParseNode *node, uint64_t tenant_id, uint64_t &database_id,
                                    common::ObString &table_name, common::ObString &synonym_name, common::ObString &db_name);
  int resolve_table_relation_factor(const ParseNode *node, uint64_t &database_id,
                                    common::ObString &table_name, common::ObString &synonym_name, common::ObString &db_name);

  /// @param org If org is true, means get original db name.
  /// Else, when db node is NULL, get session db name.
  int resolve_table_relation_node_v2(const ParseNode *node,
                                     common::ObString &table_name,
                                     common::ObString &db_name,
                                     common::ObString &catalog_name,
                                     bool &is_db_explicit,
                                     bool org = false,
                                     bool is_oracle_sys_view = false,
                                     char **dblink_name_ptr = NULL,
                                     int32_t *dblink_name_len = NULL,
                                     bool *has_dblink_node = NULL);

  int resolve_table_relation_node(const ParseNode *node,
                                common::ObString &table_name,
                                common::ObString &db_name,
                                common::ObString &catalog_name,
                                bool org = false,
                                bool is_oracle_sys_view = false,
                                char **dblink_name_ptr = NULL,
                                int32_t *dblink_name_len = NULL,
                                bool *has_dblink_node = NULL);

  int resolve_table_relation_node(const ParseNode *node,
                                  common::ObString &table_name,
                                  common::ObString &db_name,
                                  bool org = false,
                                  bool is_oracle_sys_view = false,
                                  char **dblink_name_ptr = NULL,
                                  int32_t *dblink_name_len = NULL,
                                  bool *has_dblink_node = NULL);
  /**
   * @brief  Parse a T_REF_FACTOR node to get database name and table name
   * @param [in] node  - syntax node
   * @param [in] session_info  - session information
   * @param [out] table_name  - table name
   * @param [out] db_name  - database name
   * @param [in] dblink_name_ptr  - dblink name, used to determine if certain ddl references dblink, if referenced, this ddl should return error ORA-02021
   * @param [in] dblink_name_len  - dblink name length, used for partition "drop table t1@;" and "drop table t1@q;", should return different errors
   * @retval OB_SUCCESS execute success
   * @retval OB_SOME_ERROR special errno need to handle
   *
   */
  static int resolve_ref_factor(const ParseNode *node, ObSQLSessionInfo *session_info, common::ObString &table_name, common::ObString &db_name);
  int resolve_database_factor(const ParseNode *node,
                              const uint64_t tenant_id,
                              const uint64_t catalog_id,
                              uint64_t &database_id,
                              common::ObString &db_name);

  virtual bool is_select_resolver() const { return false; }
  inline uint64_t generate_query_id() { return params_.new_gen_qid_++; }
  inline uint64_t generate_column_id() { return params_.new_gen_cid_--; }
  uint64_t generate_table_id();
  uint64_t generate_link_table_id();
  inline int64_t generate_when_number() { return params_.new_gen_wid_++; }
  inline uint64_t generate_database_id() { return params_.new_gen_did_--; }
  inline uint64_t generate_range_column_id()
  {
    uint64_t ret_cid = params_.new_gen_cid_;
    params_.new_gen_cid_ -= 10;
    return ret_cid;
  }
  inline uint64_t generate_cte_table_id() { return params_.new_cte_tid_++;}
  inline uint64_t generate_cte_column_base_id() { return common::OB_APP_MIN_COLUMN_ID;}
  template<class T>
  T *create_stmt()
  {
    int ret = common::OB_SUCCESS;
    T *stmt = NULL;
    if (OB_ISNULL(params_.stmt_factory_)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(ERROR, "stmt_factory_ is null, not be init");
    } else if (OB_ISNULL(params_.stmt_factory_->get_query_ctx())) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "query ctx is null", K(ret));
    } else if (OB_FAIL(params_.stmt_factory_->create_stmt(stmt))) {
      stmt = NULL;
      SQL_RESV_LOG(WARN, "create stmt failed", K(ret));
    } else if (OB_ISNULL(stmt)) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "create stmt success, but stmt is null");
    } else {
      stmt_ = stmt;
      stmt_->set_query_ctx(params_.query_ctx_);
      //mark prepare stmt
      stmt_->get_query_ctx()->set_is_prepare_stmt(params_.is_prepare_protocol_ && params_.is_prepare_stage_);
      stmt_->get_query_ctx()->set_timezone_info(get_timezone_info(params_.session_info_));		
      stmt_->get_query_ctx()->set_sql_stmt_coll_type(get_obj_print_params(params_.session_info_).cs_type_);
      if (OB_FAIL(stmt_->set_stmt_id())) {
        stmt = NULL;
        stmt_ = NULL;
        SQL_RESV_LOG(WARN, "fail to set stmt id", K(ret));
      }
    }
    return stmt;
  }

  // wrap ObSchemaChecker::get_column_schema with inner sql get hidden column logic.
  int get_column_schema(const uint64_t table_id,
                        const common::ObString &column_name,
                        const share::schema::ObColumnSchemaV2 *&column_schema,
                        const bool get_hidden = false,
                        bool is_link = false);
  // wrap ObSchemaChecker::get_column_schema with inner sql get hidden column logic.
  int get_column_schema(const uint64_t table_id,
                        const uint64_t column_id,
                        const share::schema::ObColumnSchemaV2 *&column_schema,
                        const bool get_hidden = false,
                        bool is_link = false);

  int check_table_name_equal(const ObString &name1, const ObString &name2, bool &equal);
protected:
  int normalize_table_or_database_names(common::ObString &name);
  int resolve_catalog_node(const ParseNode *catalog_node, uint64_t &catalog_id, common::ObString &catalog_name);

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObStmtResolver);
  bool is_catalog_supported_stmt_();
public:
  // data members
  common::ObIAllocator *allocator_;
  ObSchemaChecker *schema_checker_;
  ObSQLSessionInfo *session_info_;
  ObResolverParams &params_;
protected:
  ObStmt *stmt_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_STMT_RESOLVER_H */
