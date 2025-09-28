/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX  SQL_RESV

#include "sql/resolver/ddl/ob_drop_index_resolver.h"
#include "sql/resolver/ddl/ob_drop_index_stmt.h"
namespace oceanbase
{
using namespace common;
using namespace obrpc;
using namespace share;
using namespace share::schema;
namespace sql
{
ObDropIndexResolver::ObDropIndexResolver(ObResolverParams &params) : ObDDLResolver(params)
{
}

ObDropIndexResolver::~ObDropIndexResolver()
{
}

int ObDropIndexResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *index_node = NULL;

  if (OB_UNLIKELY((parse_tree.type_ != T_DROP_INDEX || 2 != parse_tree.num_child_))
      || OB_ISNULL(parse_tree.children_)) { // mysql mode
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree type or invalid children number", K(parse_tree.type_),
             K(parse_tree.num_child_), K(parse_tree.children_), K(ret));
  }
  
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(session_info_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session info should not be null", K(ret));
    } else if (is_external_catalog_id(session_info_->get_current_default_catalog())) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "drop index in catalog is");
    }
  }
  
  if (OB_SUCC(ret)) {
    ObDropIndexStmt *drop_index_stmt = NULL;
    if (OB_UNLIKELY(NULL == (drop_index_stmt = create_stmt<ObDropIndexStmt>()))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create index stmt failed", K(ret));
    } else {
      stmt_ = drop_index_stmt;
    }

    if (OB_SUCC(ret) && lib::is_mysql_mode()) {
      index_node = parse_tree.children_[0];
      ParseNode *relation_node = parse_tree.children_[1];
      ObString table_name;
      ObString database_name;
      if (OB_ISNULL(relation_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("relation_node is NULL", K(ret));
      } else if (OB_FAIL(resolve_table_relation_node(relation_node, table_name, database_name))) {
        LOG_WARN("failed to resolve table relation node!", K(ret));
      } else {
        drop_index_stmt->set_table_name(table_name);
        drop_index_stmt->set_database_name(database_name);
        drop_index_stmt->set_tenant_id(session_info_->get_effective_tenant_id());
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(index_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index_node is NULL", K(ret));
      } else {
        int32_t len = static_cast<int32_t>(index_node->str_len_);
        ObString index_name(len, len, index_node->str_value_);
        // 检查索引是否建立在外键列上，如果是的话，则不允许删除该索引
        const ObTableSchema *table_schema = NULL;
        if (OB_FAIL(schema_checker_->get_table_schema(session_info_->get_effective_tenant_id(),
            drop_index_stmt->get_database_name(),
            drop_index_stmt->get_table_name(),
            false /* not index table */,
            table_schema))) {
          if (OB_TABLE_NOT_EXIST == ret) {
            ObCStringHelper helper;
            LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(drop_index_stmt->get_database_name()),
                helper.convert(drop_index_stmt->get_table_name()));
          }
          LOG_WARN("fail to get table schema", K(ret));
        } else if (OB_ISNULL(table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("table schema is NULL", K(ret));
        } else if (table_schema->is_materialized_view()) {
          const uint64_t tenant_id = session_info_->get_effective_tenant_id();
          const uint64_t mv_container_table_id = table_schema->get_data_table_id();
          const ObTableSchema *mv_container_table_schema = nullptr;
          ObString mv_container_table_name;
          if (OB_FAIL(get_mv_container_table(tenant_id,
                                             mv_container_table_id,
                                             mv_container_table_schema,
                                             mv_container_table_name))) {
            LOG_WARN("fail to get mv container table", KR(ret), K(tenant_id), K(mv_container_table_id));
            if (OB_TABLE_NOT_EXIST == ret) {
              ret = OB_ERR_UNEXPECTED; // rewrite errno
            }
          } else {
            drop_index_stmt->set_table_name(mv_container_table_name);
            table_schema = mv_container_table_schema;
          }
        }
        if (OB_FAIL(ret)) {
        } else if (table_schema->is_parent_table() || table_schema->is_child_table()) {
          const ObTableSchema *index_table_schema = NULL;
          ObString index_table_name;
          ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
          bool has_other_indexes_on_same_cols = false;
          if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
              table_schema->get_table_id(),
              index_name,
              index_table_name))) {
            LOG_WARN("build_index_table_name failed", K(table_schema->get_table_id()), K(index_name), K(ret));
          } else if (OB_FAIL(schema_checker_->get_table_schema(session_info_->get_effective_tenant_id(),
              drop_index_stmt->get_database_name(),
              index_table_name,
              true /* index table */,
              index_table_schema))) {
            LOG_WARN("fail to get index table schema", K(ret));
          } else if (OB_ISNULL(index_table_schema)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("table schema is NULL", K(ret));
          } else if (OB_FAIL(check_indexes_on_same_cols(*table_schema,
                                                        *index_table_schema,
                                                        *schema_checker_,
                                                        has_other_indexes_on_same_cols))) {
            LOG_WARN("check indexes on same cols failed", K(ret));
          } else if (!has_other_indexes_on_same_cols && lib::is_mysql_mode()) {
            if (OB_FAIL(check_index_columns_equal_foreign_key(*table_schema, *index_table_schema))) {
              LOG_WARN("failed to check_index_columns_equal_foreign_key", K(ret), K(index_table_name));
            }
          }
        }
        // 外键列对删除索引的影响至此检查结束
        if (OB_SUCC(ret)) {
          drop_index_stmt->set_index_name(index_name);
          obrpc::ObDropIndexArg &drop_index_arg = drop_index_stmt->get_drop_index_arg();
        }
      }
    }
  }

  return ret;
}

}  // namespace sql
}  // namespace oceanbase
