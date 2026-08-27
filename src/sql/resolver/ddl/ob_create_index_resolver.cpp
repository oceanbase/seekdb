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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ddl/ob_create_index_resolver.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/resolver/ddl/ob_vec_index_builder_util.h"
#include "sql/session/ob_local_session_var.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share::schema;
namespace sql
{
ObCreateIndexResolver::ObCreateIndexResolver(ObResolverParams &params)
   : ObDDLResolver(params), is_spec_block_size(false)
{
}

ObCreateIndexResolver::~ObCreateIndexResolver()
{
}

// child 0 of root node, resolve index name
int ObCreateIndexResolver::resolve_index_name_node(
    ParseNode *index_name_node,
    ObCreateIndexStmt *crt_idx_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == index_name_node)
      || OB_UNLIKELY(NULL == crt_idx_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(index_name_node), KP(crt_idx_stmt));
  } else if (index_name_node->num_child_ < 2) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(ret), "child_num", index_name_node->num_child_);
  } else if (NULL == index_name_node->children_[1]) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(ret));
  } else if (NULL != index_name_node->children_[0]) { // database name not null

    uint64_t database_id = OB_INVALID_ID;
    const ObString &database_name = crt_idx_stmt->get_database_name();
    uint64_t spec_database_id = OB_INVALID_ID;
    ObString spec_database_name(index_name_node->children_[0]->str_len_,
                                index_name_node->children_[0]->str_value_);
    if (OB_FAIL(schema_checker_->get_database_id(
            database_name, database_id))) {
    } else if (OB_FAIL(schema_checker_->get_database_id(
            spec_database_name, spec_database_id))) {
    } else if (spec_database_id != database_id) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("should specify the database name of data table for index",
               K(ret), K(spec_database_name), K(database_name));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "Index name including database name is");
    } else {} // no more to do
  }
  if (OB_SUCC(ret)) {
    int32_t len = static_cast<int32_t>(index_name_node->children_[1]->str_len_);
    ObString index_name(len, index_name_node->children_[1]->str_value_);
    ObCollationType cs_type = CS_TYPE_INVALID;
    if (OB_UNLIKELY(NULL == session_info_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session if NULL", K(ret));
    } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    } else if (OB_FAIL(ObSQLUtils::check_index_name(cs_type, index_name))) {
    } else {
      crt_idx_stmt->set_index_name(index_name);
      index_keyname_ = static_cast<INDEX_KEYNAME>(index_name_node->value_);
    }
  }
  return ret;
}

// child 1 of root node, resolve table name of this index
int ObCreateIndexResolver::resolve_index_table_name_node(
    ParseNode *index_table_name_node,
    ObCreateIndexStmt *crt_idx_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == index_table_name_node)
      || OB_UNLIKELY(NULL == crt_idx_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(index_table_name_node), KP(crt_idx_stmt));
  } else {
    ObString table_name;
    ObString database_name;
    if (OB_FAIL(resolve_table_relation_node(index_table_name_node, table_name, database_name))) {
    } else if (OB_FAIL(set_database_name(database_name))) {
    } else {
      crt_idx_stmt->set_database_name(database_name);
      crt_idx_stmt->set_table_name(table_name);
      crt_idx_stmt->set_name_generated_type(GENERATED_TYPE_USER);

    }
  }
  return ret;
}

// child 2 of root node, resolve index column
int ObCreateIndexResolver::resolve_index_column_node(
    ParseNode *index_column_node,
    const int64_t index_keyname_value,
    ParseNode *table_option_node,
    ObCreateIndexStmt *crt_idx_stmt,
    const ObTableSchema *tbl_schema)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObString, 8> input_index_columns_name;
  bool is_prefix_index = false;
  if (OB_ISNULL(index_column_node) || OB_ISNULL(crt_idx_stmt) || OB_ISNULL(tbl_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(index_column_node), KP(crt_idx_stmt), KP(tbl_schema));
  } else if (T_INDEX_COLUMN_LIST != index_column_node->type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to check node type", K(ret));
  } else {
    bool cnt_func_index = false;
    const bool is_vec_index = (index_keyname_ == INDEX_KEYNAME::VEC_KEY);
    if (is_vec_index && index_column_node->num_child_ >= 2) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("multi column of vector index is not support yet", K(ret), K(index_column_node->num_child_));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "multi vector index column is");
    }
    for (int32_t i = 0; OB_SUCC(ret) && i < index_column_node->num_child_; ++i) {
      ParseNode *col_node = index_column_node->children_[i];
      ObColumnSortItem sort_item;
      if (OB_UNLIKELY(NULL == col_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("column node is null", K(ret));
      } else if (T_SORT_COLUMN_KEY != col_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to check node type", K(ret));
      } else if (OB_ISNULL(col_node->children_) || 3 != col_node->num_child_
                 || OB_ISNULL(col_node->children_[0])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid sort column node", K(ret), K(col_node->num_child_));
      } else {
        // If this node type is not identifier, then it is considered a function index.
        if (col_node->children_[0]->type_ != T_IDENT) {
          sort_item.is_func_index_ = true;
          cnt_func_index = true;
        }
        sort_item.column_name_.assign_ptr(const_cast<char *>(col_node->children_[0]->str_value_),
                                          static_cast<int32_t>(col_node->children_[0]->str_len_));
        bool is_multi_value_index = false;
        if (OB_FAIL(share::ObMulValueIndexBuilderUtil::adjust_index_type(sort_item.column_name_,
                                                                  is_multi_value_index,
                                                                  reinterpret_cast<int*>(&index_keyname_)))) {
        } else if (is_multi_value_index
                   && NULL != col_node->children_[2]
                   && 1 != col_node->children_[2]->is_empty_) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("explicit order is not supported for multivalue index", K(ret));
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "ASC/DESC for multivalue index is");
        } else if (is_multi_value_index) {
          ObCreateIndexArg &index_arg =crt_idx_stmt->get_create_index_arg();
          if (index_keyname_ == MULTI_KEY) {
            index_arg.index_type_ = INDEX_TYPE_NORMAL_MULTIVALUE_LOCAL;
          }
        }
        if (OB_SUCC(ret)) {
          const ObColumnSchemaV2 *column_schema = NULL;
          if (OB_NOT_NULL(column_schema = tbl_schema->get_column_schema(sort_item.column_name_))) {
            if (ob_is_collection_sql_type(column_schema->get_data_type())) {
              bool is_sparse_vec_col = false;
              if (index_keyname_ != INDEX_KEYNAME::VEC_KEY) {
                ret = OB_NOT_SUPPORTED;
                LOG_WARN("not support index type create on vector or array column yet", K(ret), K(index_keyname_));
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "create index on vector or array column is");
              } else if (OB_FAIL(share::ObVectorIndexUtil::is_sparse_vec_col(column_schema->get_extended_type_info(), is_sparse_vec_col))) {
              }
            }
          }
        }
      }
      // Prefix index prefix length
      if (OB_FAIL(ret)) {
      } else if (NULL != col_node->children_[1]) {
        sort_item.prefix_len_ = static_cast<int32_t>(col_node->children_[1]->value_);
        if (0 == sort_item.prefix_len_) {
          ret = OB_KEY_PART_0;
          LOG_WARN("index prefix len invalid", K(ret), "prefix_len", sort_item.prefix_len_);
          LOG_USER_ERROR(OB_KEY_PART_0, sort_item.column_name_.length(), sort_item.column_name_.ptr());
        }
        is_prefix_index = true;
      } else {
        sort_item.prefix_len_ = 0;
      }
      // not support fts or vec index in same table
      if (OB_SUCC(ret)) {
        bool has_fts_index = false;
        bool has_vec_index = false;
        if (OB_ISNULL(session_info_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret));
        } else if (OB_FAIL(share::ObVectorIndexUtil::check_table_has_vector_of_fts_index(
            *tbl_schema, *(schema_checker_->get_schema_guard()), has_fts_index, has_vec_index))) {
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (index_keyname_ == MULTI_KEY || index_keyname_ == MULTI_UNIQUE_KEY) {
        if (!GCONF._enable_add_fulltext_index_to_existing_table) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("experimental feature: build multivalue index afterward is experimental feature", K(ret));
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "build multivalue index afterward");
        }
      } else if (index_keyname_ == FTS_KEY) {
        if (OB_ISNULL(session_info_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret));
        } else if (OB_FAIL(resolve_fts_index_constraint(*tbl_schema,
                                                        sort_item.column_name_,
                                                        index_keyname_value))) {
        }
      } else if (index_keyname_ == INDEX_KEYNAME::VEC_KEY) {
        if (sort_item.is_func_index_) {
          ret = OB_ERR_FUNCTIONAL_INDEX_ON_FIELD;
          LOG_WARN("Functional index for vector index is not supported.", K(ret), K(sort_item));
        } else if (OB_FAIL(resolve_vec_index_constraint(*tbl_schema,
                                                        *schema_checker_,
                                                        sort_item.column_name_,
                                                        index_keyname_value,
                                                        table_option_node))) {
        }
      } else { // spatial index, NOTE resolve_spatial_index_constraint() will set index_keyname
        bool is_explicit_order = (NULL != col_node->children_[2]
            && 1 != col_node->children_[2]->is_empty_);
        if (OB_FAIL(resolve_spatial_index_constraint(*tbl_schema, sort_item.column_name_,
            index_column_node->num_child_, index_keyname_value, is_explicit_order, sort_item.is_func_index_, NULL, is_prefix_index))) {
        }
      }
      // Index sorting method
      if (OB_FAIL(ret)) {
      } else {
        sort_item.order_type_ = common::ObOrderType::ASC;
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(add_sort_column(sort_item))) {
      } else { /*do nothing*/ }
    }
    ObCreateIndexArg &index_arg = crt_idx_stmt->get_create_index_arg();
    if (OB_SUCC(ret) && is_vec_index && index_arg.index_columns_.count() > 0) {
      const ObColumnSortItem &sort_item = index_arg.index_columns_.at(0);
      if (OB_FAIL(set_vec_column_name(sort_item.column_name_))) {
      } else if (OB_FAIL(set_table_name(tbl_schema->get_table_name()))) {
      }
    }

    // Check whether the new index is on the same columns as old indexes.
  }
  return ret;
}

// child 3 of root node, resolve index option node
int ObCreateIndexResolver::resolve_index_option_node(
    ParseNode *index_option_node,
    ObCreateIndexStmt *crt_idx_stmt,
    const ObTableSchema *tbl_schema,
    bool is_partitioned)
{
  int ret = OB_SUCCESS;
  const bool is_index = true;
  if (OB_ISNULL(crt_idx_stmt) || OB_ISNULL(tbl_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KP(crt_idx_stmt), KP(tbl_schema), K(ret));
  } else if (NULL != index_option_node) {
    if (OB_FAIL(resolve_table_options(index_option_node, is_index))) {
    }

    // index table dop
    if (OB_SUCC(ret)) {
      // If dop is not specified, the default value of table_dop_ is 1
      crt_idx_stmt->set_index_dop(table_dop_);
    }

    // block_size
    if (OB_SUCC(ret)) {
      if(T_TABLE_OPTION_LIST != index_option_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid parse node", K(ret));
      } else {
        int64_t num = index_option_node->num_child_;
        for (int64_t i = 0; OB_SUCC(ret) && i < num; ++i) {
          ParseNode *option_node = NULL;
          if (OB_ISNULL(option_node = index_option_node->children_[i])) {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "node is null", K(ret));
          } else if (T_BLOCK_SIZE == option_node->type_) {
            is_spec_block_size = true;
            break;
          }
        }
      }
    }
  }

  // storing column
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < store_column_names_.count(); ++i) {
      if (OB_FAIL(crt_idx_stmt->add_storing_column(store_column_names_.at(i)))) {
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < hidden_store_column_names_.count(); ++i) {
      if (OB_FAIL(crt_idx_stmt->add_hidden_storing_column(hidden_store_column_names_.at(i)))) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (has_index_using_type_) {
      crt_idx_stmt->set_index_using_type(index_using_type_);
    }
    if (OB_FAIL(set_table_option_to_stmt(*tbl_schema, tbl_schema->get_table_id(), is_partitioned))) {
    } else if (tbl_schema->is_partitioned_table()
        && INDEX_TYPE_SPATIAL_GLOBAL == crt_idx_stmt->get_create_index_arg().index_type_) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "spatial global index");
    }
  }
  return ret;
}

// child 4 of root node, resolve index method node
int ObCreateIndexResolver::resolve_index_method_node(
    ParseNode *index_method_node,
    ObCreateIndexStmt *crt_idx_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == index_method_node)
      || OB_UNLIKELY(NULL == crt_idx_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(index_method_node), KP(crt_idx_stmt));
  } else {
    if (T_USING_HASH == index_method_node->type_) {
      crt_idx_stmt->set_index_using_type(USING_HASH);
    } else {
      crt_idx_stmt->set_index_using_type(USING_BTREE);
    }
  }
  return ret;
}

/**
 * @brief Add some information from the session to arg
 * @param session Current session
 * @param crt_idx_stmt stmt
 * @return ret
 */
int ObCreateIndexResolver::fill_session_info_into_arg(const sql::ObSQLSessionInfo *session,
                                                      ObCreateIndexStmt *crt_idx_stmt)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(session));
  CK (OB_NOT_NULL(crt_idx_stmt));
  if (OB_SUCC(ret)) {
    ObCreateIndexArg &arg = crt_idx_stmt->get_create_index_arg();
    if (OB_FAIL(ObLocalSessionVarHelper::load_session_vars(session, arg.local_session_var_))) {
    }
  }
  return ret;
}

int ObCreateIndexResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObCreateIndexStmt *crt_idx_stmt = NULL;
  ParseNode &parse_node = const_cast<ParseNode &>(parse_tree);
  ParseNode *if_not_exist_node = NULL;
  const ObTableSchema *tbl_schema = NULL;
  const ObTableSchema *data_tbl_schema = NULL;

  if (OB_UNLIKELY(T_CREATE_INDEX != parse_tree.type_)
      || OB_UNLIKELY(CREATE_INDEX_CHILD_NUM != parse_tree.num_child_)
      || OB_UNLIKELY(NULL == parse_tree.children_[0])
      || OB_UNLIKELY(NULL == parse_tree.children_[1])
      || OB_UNLIKELY(NULL == parse_tree.children_[2])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(ret), K(parse_tree.type_), K(parse_tree.num_child_),
             "index_name_node", parse_tree.children_[0],
             "table_name_node", parse_tree.children_[1],
             "index_column_node", parse_tree.children_[2]);
  } else if (OB_UNLIKELY(NULL == (crt_idx_stmt = create_stmt<ObCreateIndexStmt>()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("create index stmt failed", K(ret));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session_info_ is null", K(ret));
  } else {
    stmt_ = crt_idx_stmt;
    if_not_exist_node = parse_tree.children_[7];
  }
  // Write the information from session to stmt's arg
  // includes nls_xx_format
  if (OB_SUCC(ret)) {
    if (OB_FAIL(fill_session_info_into_arg(session_info_, crt_idx_stmt))) {
    }
  }

  if (FAILEDx(resolve_index_table_name_node(parse_node.children_[1], crt_idx_stmt))) {
    LOG_WARN("fail to resolve index table name node", K(ret));
  } else if (OB_FAIL(schema_checker_->get_table_schema(crt_idx_stmt->get_database_name(),
                                                       crt_idx_stmt->get_table_name(),
                                                       false/*not index table*/,
                                                       tbl_schema))) {
    if (OB_TABLE_NOT_EXIST == ret) {
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(crt_idx_stmt->get_create_index_arg().database_name_),
          helper.convert(crt_idx_stmt->get_create_index_arg().table_name_));
      LOG_WARN("table not exist", K(ret),
          "database_name", crt_idx_stmt->get_create_index_arg().database_name_,
          "table_name", crt_idx_stmt->get_create_index_arg().table_name_);
    } else {
      LOG_WARN("fail to get table schema", K(ret));
    }
  } else if (OB_ISNULL(tbl_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table schema is NULL", K(ret));
  } else {
    crt_idx_stmt->set_table_id(tbl_schema->get_table_id());
    data_tbl_schema = tbl_schema;
  }
  if (FAILEDx(resolve_index_name_node(parse_node.children_[0], crt_idx_stmt))) {
    LOG_WARN("fail to resolve index name node", K(ret));
  } else if (OB_FAIL(resolve_index_column_node(parse_node.children_[2],
                                               parse_tree.children_[0]->value_,
                                               parse_tree.children_[3],
                                               crt_idx_stmt,
                                               data_tbl_schema))) {
  } else if (NULL != parse_node.children_[4]
      && OB_FAIL(resolve_index_method_node(parse_node.children_[4], crt_idx_stmt))) {
    LOG_WARN("fail to resolve index method node", K(ret));
  } else if (OB_FAIL(resolve_index_option_node(parse_node.children_[3],
                                               crt_idx_stmt,
                                               data_tbl_schema,
                                               NULL != parse_node.children_[5]))) {
  } else if (global_ && OB_FAIL(generate_global_index_schema(crt_idx_stmt))) {
    LOG_WARN("fail to generate index schema", K(ret));
  } else {
    if (NULL != parse_node.children_[5]) {
      if (1 != parse_node.children_[5]->num_child_
          || T_PARTITION_OPTION != parse_node.children_[5]->type_) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "invalid partition option for index table");
        LOG_WARN("node is invalid", K(ret));
      } else if (NULL == parse_node.children_[5]->children_[0]) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("node is null", K(ret));
      } else {
        ParseNode *index_partition_node = parse_node.children_[5]->children_[0]; // ordinary partition node
        if (OB_FAIL(resolve_index_partition_node(index_partition_node, crt_idx_stmt))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      crt_idx_stmt->set_if_not_exists(NULL != if_not_exist_node);
      // Set block size, if block size is not specified, then use the main table block size
      // Otherwise use default block_size
      if (!is_spec_block_size) {
        ObCreateIndexArg &index_arg = crt_idx_stmt->get_create_index_arg();
        index_arg.index_option_.block_size_ = tbl_schema->get_block_size();
      }
    }
  }

  if (OB_SUCC(ret)) {
    const ParseNode *parallel_node = parse_tree.children_[8];
    if (OB_FAIL(resolve_hints(parse_tree.children_[8], *crt_idx_stmt, *tbl_schema))) {
    }
  }
  if (OB_SUCC(ret)) {
    ObCreateIndexArg &index_arg = crt_idx_stmt->get_create_index_arg();
    char* buf = nullptr;
    int64_t pos = 0;
    if (is_vec_index(index_arg.index_type_)) {
      if (!is_vec_index(vec_index_type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected index type", KR(ret), K(vec_index_type_));
      } else if (FALSE_IT(index_arg.index_type_ = vec_index_type_)) {
      } else if (FALSE_IT(index_arg.index_schema_.set_index_params(index_params_))) {
      } else if (tbl_schema->is_view_table()) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("create vector index on view table is not supported",
            KR(ret), K(tbl_schema->get_table_name()));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "create vector index on view table is");
      } else if (OB_ISNULL(buf = reinterpret_cast<char*>(allocator_->alloc(sizeof(char) * OB_MAX_PROC_ENV_LENGTH)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc buffer", KR(ret), K(OB_MAX_PROC_ENV_LENGTH));
      } else if (OB_FAIL(ObExecEnv::gen_exec_env(*session_info_, buf, OB_MAX_PROC_ENV_LENGTH, pos))) {
      } else {
        index_arg.vidx_refresh_info_.exec_env_.assign_ptr(buf, pos);
      }
    }
  }


if (OB_SUCC(ret) &&
      OB_FAIL(share::ObFtsIndexBuilderUtil::check_supportability_for_building_index(
          data_tbl_schema, &crt_idx_stmt->get_create_index_arg()))) {
    LOG_WARN("fail to check supportability for building index",
        K(data_tbl_schema), K(crt_idx_stmt->get_create_index_arg()));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(crt_idx_stmt->get_create_index_arg().
                based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(
                    tbl_schema->get_table_id(),
                    TABLE_SCHEMA,
                    tbl_schema->get_schema_version())))) {
    } else if (OB_FAIL(add_based_udt_info(*tbl_schema))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(data_tbl_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null data table schema", K(ret));
    }
  }

  DEBUG_SYNC(HANG_BEFORE_RESOLVER_FINISH);

  return ret;
}

int ObCreateIndexResolver::add_sort_column(const ObColumnSortItem &sort_column)
{
  int ret = OB_SUCCESS;
  ObCreateIndexStmt *create_index_stmt = NULL;
  ObColumnNameWrapper column_key(sort_column.column_name_, sort_column.prefix_len_);
  bool check_prefix_len = false;
  if (OB_ISNULL(stmt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema checker or stmt is NULL", K(ret));
  } else {
    create_index_stmt = static_cast<ObCreateIndexStmt*>(stmt_);
  }
  if (OB_FAIL(ret)) {
    //empty
  } else if (is_column_exists(sort_column_array_, column_key, check_prefix_len)) {
    ret = OB_ERR_COLUMN_DUPLICATE;
    LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, sort_column.column_name_.length(), sort_column.column_name_.ptr());
    LOG_WARN("Duplicate sort column name", K(sort_column), K(ret));
   } else if (OB_FAIL(sort_column_array_.push_back(column_key))) {
  } else if (OB_FAIL(create_index_stmt->add_sort_column(sort_column))) {
  }
  return ret;
}

int ObCreateIndexResolver::set_table_option_to_stmt(
    const share::schema::ObTableSchema &tbl_schema,
    const uint64_t data_table_id,
    bool is_partitioned)
{
  int ret = OB_SUCCESS;
  ObCreateIndexStmt *create_index_stmt = static_cast<ObCreateIndexStmt*>(stmt_);
  if (OB_UNLIKELY(OB_INVALID_ID == data_table_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(data_table_id));
  } else if (OB_ISNULL(create_index_stmt) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("create_index_stmt can not be null", K(ret));
  } else {
    ObCreateIndexArg &index_arg = create_index_stmt->get_create_index_arg();
    index_arg.index_key_ = static_cast<int64_t>(index_keyname_);

    index_arg.index_option_.index_status_= INDEX_STATUS_UNAVAILABLE;
    if (NOT_SPECIFIED == index_scope_) {
      // Partitioned index must be global; otherwise the default index mode is local.
      global_ = is_partitioned;
    } else {
      global_ = (GLOBAL_INDEX == index_scope_);
    }
    if (UNIQUE_KEY == index_keyname_) {
      if (global_) {
        index_arg.index_type_ = INDEX_TYPE_UNIQUE_GLOBAL;
      } else {
        index_arg.index_type_ = INDEX_TYPE_UNIQUE_LOCAL;
      }
    } else if (NORMAL_KEY == index_keyname_) {
      if (global_) {
        index_arg.index_type_ = INDEX_TYPE_NORMAL_GLOBAL;
      } else {
        index_arg.index_type_ = INDEX_TYPE_NORMAL_LOCAL;
      }
    } else if (SPATIAL_KEY == index_keyname_) {
      if (global_) {
        index_arg.index_type_ = INDEX_TYPE_SPATIAL_GLOBAL;
      } else {
        index_arg.index_type_ = INDEX_TYPE_SPATIAL_LOCAL;
      }
    } else if (FTS_KEY == index_keyname_) {
      if (global_) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support global fts index now", K(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "global fulltext index is");
      } else {
        // set type to fts_index_aux first, append other fts arg later
        index_arg.index_type_ = INDEX_TYPE_FTS_INDEX_LOCAL;
      }
    } else if (MULTI_KEY == index_keyname_ || MULTI_UNIQUE_KEY == index_keyname_) {
      if (global_) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("not support global multivalue index now", K(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "not support global multivalue index");
      } else if (MULTI_KEY == index_keyname_) {
        index_arg.index_type_ = INDEX_TYPE_NORMAL_MULTIVALUE_LOCAL;
      } else {
        index_arg.index_type_ = INDEX_TYPE_UNIQUE_MULTIVALUE_LOCAL;
      }
    } else if (INDEX_KEYNAME::VEC_KEY == index_keyname_) {
      if (global_) {
        // TODO @lhd support global index?
        ret = OB_NOT_SUPPORTED;
      } else {
        index_arg.index_type_ = INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL;
      }
    }
    index_arg.index_option_.block_size_ = block_size_;
    index_arg.index_option_.progressive_merge_num_ = progressive_merge_num_;
    index_arg.index_option_.index_attributes_set_ = index_attributes_set_;
    index_arg.index_option_.parser_name_ = parser_name_;
    index_arg.index_option_.parser_properties_ = parser_properties_;
    index_arg.with_rowid_ = with_rowid_;
    index_arg.sql_mode_ = session_info_->get_sql_mode();
    index_arg.is_index_scope_specified_ = !(NOT_SPECIFIED == index_scope_);
    create_index_stmt->set_comment(comment_);
    if (OB_FAIL(ret)) {
    } else if (INDEX_KEYNAME::VEC_KEY == index_keyname_ &&
               OB_FAIL(share::ObVecIndexBuilderUtil::generate_vec_index_name(allocator_, index_arg.index_type_, index_arg.index_name_, index_arg.index_name_))) {
      LOG_WARN("generate vec parser name failed", K(ret), K(index_arg));
    } else if (FTS_KEY == index_keyname_ &&
               OB_FAIL(share::ObFtsIndexBuilderUtil::generate_fts_parser_name_and_property(tbl_schema, index_arg,
                                                                       allocator_))) {
      LOG_WARN("generate fts parser name failed", K(ret), K(index_arg));
    }
  }
  return ret;
}

int ObCreateIndexResolver::add_based_udt_info(const share::schema::ObTableSchema &tbl_schema)
{
  int ret = OB_SUCCESS;
  ObCreateIndexStmt *create_index_stmt = static_cast<ObCreateIndexStmt*>(stmt_);
  if (OB_ISNULL(create_index_stmt) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("create index stmt is nullptr", KR(ret));
  } else {
    ObTableSchema::const_column_iterator begin = tbl_schema.column_begin();
    ObTableSchema::const_column_iterator end = tbl_schema.column_end();
    ObCreateIndexArg &arg = create_index_stmt->get_create_index_arg();
    for (; OB_SUCC(ret) && begin != end; begin++) {
      ObColumnSchemaV2 *col = (*begin);
      if (OB_ISNULL(col)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get column schema failed", KR(ret));
      } else if (col->is_extend()) {
      }
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
