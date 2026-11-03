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
#include "rust/config.h"
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

int ObCreateIndexResolver::resolve_index_name_node(
    const peg::Node &name,
    ObCreateIndexStmt *crt_idx_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(crt_idx_stmt) || OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (peg::relation_has_database(name)) {
    uint64_t database_id = OB_INVALID_ID;
    uint64_t spec_database_id = OB_INVALID_ID;
    const auto &database = peg::relation_database(name);
    const auto database_text = database.text();
    const ObString spec_database_name(syntax_text(database_text));
    if (OB_FAIL(schema_checker_->get_database_id(crt_idx_stmt->get_database_name(), database_id))) {
    } else if (OB_FAIL(schema_checker_->get_database_id(spec_database_name, spec_database_id))) {
    } else if (spec_database_id != database_id) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "Index name including database name is");
    }
  }
  if (OB_SUCC(ret)) {
    const auto &identifier = peg::relation_name(name);
    const auto identifier_text = identifier.text();
    ObString index_name;
    ObCollationType cs_type = CS_TYPE_INVALID;
    if (OB_FAIL(ob_write_string(*allocator_,
        syntax_text(identifier_text), index_name))) {
    } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    } else if (OB_FAIL(ObSQLUtils::check_index_name(cs_type, index_name))) {
    } else {
      crt_idx_stmt->set_index_name(index_name);
    }
  }
  return ret;
}

int ObCreateIndexResolver::resolve_index_table_name_node(
    const peg::Node &relation,
    ObCreateIndexStmt *crt_idx_stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(crt_idx_stmt)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObString table_name;
    ObString database_name;
    if (OB_FAIL(resolve_table_relation_node(&relation, table_name, database_name))) {
    } else if (OB_FAIL(set_database_name(database_name))) {
    } else {
      crt_idx_stmt->set_database_name(database_name);
      crt_idx_stmt->set_table_name(table_name);
      crt_idx_stmt->set_name_generated_type(GENERATED_TYPE_USER);
      
    }
  }
  return ret;
}

int ObCreateIndexResolver::resolve_index_column_node(
    rust::Slice<const peg::Node> columns,
    const int64_t index_keyname_value,
    rust::Slice<const peg::Node> options,
    ObCreateIndexStmt *crt_idx_stmt,
    const ObTableSchema *tbl_schema)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObString, 8> input_index_columns_name;
  bool is_prefix_index = false;
  if (OB_ISNULL(crt_idx_stmt) || OB_ISNULL(tbl_schema) || OB_ISNULL(params_.syntax_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    bool cnt_func_index = false;
    const bool is_vec_index = (index_keyname_ == INDEX_KEYNAME::VEC_KEY);
    if (is_vec_index && columns.size() >= 2) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "multi vector index column is");
    }
    for (int32_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
      const auto &column = columns[i];
      const auto &expression = peg::index_column_expression(column);
      ObColumnSortItem sort_item;
      {
        sort_item.is_func_index_ = expression.kind() != peg::NodeKind::Ident;
        cnt_func_index |= sort_item.is_func_index_;
        const auto expression_text = expression.text();
        const ObString spelling = sort_item.is_func_index_ ? params_.syntax_->source(expression)
            : syntax_text(expression_text);
        if (OB_FAIL(ob_write_string(*allocator_, spelling, sort_item.column_name_))) {
        }
        bool is_multi_value_index = false;
        if (OB_SUCC(ret) && OB_FAIL(share::ObMulValueIndexBuilderUtil::adjust_index_type(sort_item.column_name_,
                                                                  is_multi_value_index,
                                                                  reinterpret_cast<int*>(&index_keyname_)))) {
        } else if (is_multi_value_index
                   && peg::index_column_has_direction(column)) {
          ret = OB_NOT_SUPPORTED;
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
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "create index on vector or array column is");
              } else if (OB_FAIL(share::ObVectorIndexUtil::is_sparse_vec_col(column_schema->get_extended_type_info(), is_sparse_vec_col))) {
              }
            }
          }
        }
      }
      // Prefix index prefix length
      if (OB_FAIL(ret)) {
      } else if (peg::index_column_has_prefix(column)) {
        sort_item.prefix_len_ = static_cast<int32_t>(peg::index_column_prefix(column));
        if (0 == sort_item.prefix_len_) {
          ret = OB_KEY_PART_0;
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
        } else if (OB_FAIL(share::ObVectorIndexUtil::check_table_has_vector_of_fts_index(
            *tbl_schema, *(schema_checker_->get_schema_guard()), has_fts_index, has_vec_index))) {
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (index_keyname_ == MULTI_KEY || index_keyname_ == MULTI_UNIQUE_KEY) {
        if (!config::_enable_add_fulltext_index_to_existing_table()) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "build multivalue index afterward");
        }
      } else if (index_keyname_ == FTS_KEY) {
        if (OB_ISNULL(session_info_)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(resolve_fts_index_constraint(*tbl_schema,
                                                        sort_item.column_name_,
                                                        index_keyname_value))) {
        }
      } else if (index_keyname_ == INDEX_KEYNAME::VEC_KEY) {
        if (sort_item.is_func_index_) {
          ret = OB_ERR_FUNCTIONAL_INDEX_ON_FIELD;
        } else if (OB_FAIL(resolve_vec_index_constraint(*tbl_schema,
                                                        *schema_checker_,
                                                        sort_item.column_name_,
                                                        index_keyname_value,
                                                        options))) {
        }
      } else { // spatial index, NOTE resolve_spatial_index_constraint() will set index_keyname
        const bool is_explicit_order = peg::index_column_has_direction(column);
        if (OB_FAIL(resolve_spatial_index_constraint(*tbl_schema, sort_item.column_name_,
            columns.size(), index_keyname_value, is_explicit_order, sort_item.is_func_index_ ? &expression : nullptr, NULL, is_prefix_index))) {
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

int ObCreateIndexResolver::resolve_index_option_node(
    rust::Slice<const peg::Node> options,
    ObCreateIndexStmt *crt_idx_stmt,
    const ObTableSchema *tbl_schema,
    bool is_partitioned)
{
  int ret = OB_SUCCESS;
  const bool is_index = true;
  if (OB_ISNULL(crt_idx_stmt) || OB_ISNULL(tbl_schema)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!options.empty()) {
    if (OB_FAIL(resolve_table_options(options, is_index))) {
    } else {
      crt_idx_stmt->set_index_dop(table_dop_);
      for (const auto &option : options) {
        if (option.kind() == peg::NodeKind::BlockSize) {
          is_spec_block_size = true;
          break;
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

int ObCreateIndexResolver::resolve(const peg::Node &statement)
{
  if (statement.kind() != peg::NodeKind::CreateIndex) { return OB_ERR_UNEXPECTED; }
  int ret = OB_SUCCESS;
  ObCreateIndexStmt *crt_idx_stmt = NULL;
  const auto definition = peg::index_definition(statement);
  const ObTableSchema *tbl_schema = NULL;
  const ObTableSchema *data_tbl_schema = NULL;

  if (OB_UNLIKELY(NULL == (crt_idx_stmt = create_stmt<ObCreateIndexStmt>()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("create index stmt failed", K(ret));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    stmt_ = crt_idx_stmt;
    index_keyname_ = index_keyname(definition.kind);
  }
  // Write the information from session to stmt's arg
  // includes nls_xx_format
  if (OB_SUCC(ret)) {
    if (OB_FAIL(fill_session_info_into_arg(session_info_, crt_idx_stmt))) {
    }
  }

  if (FAILEDx(resolve_index_table_name_node(peg::index_definition_table(statement), crt_idx_stmt))) {
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
    }
  } else if (OB_ISNULL(tbl_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    crt_idx_stmt->set_table_id(tbl_schema->get_table_id());
    data_tbl_schema = tbl_schema;
  }
  if (FAILEDx(resolve_index_name_node(peg::index_definition_name(statement), crt_idx_stmt))) {
  } else if (OB_FAIL(resolve_index_column_node(definition.columns,
                                               index_keyname(definition.kind),
                                               definition.options,
                                               crt_idx_stmt,
                                               data_tbl_schema))) {
  } else if (FALSE_IT(crt_idx_stmt->set_index_using_type(
      definition.method == peg::IndexMethod::Hash ? USING_HASH : USING_BTREE))) {
  } else if (OB_FAIL(resolve_index_option_node(definition.options,
                                               crt_idx_stmt,
                                               data_tbl_schema,
                                               definition.has_partition))) {
  } else if (global_ && OB_FAIL(generate_global_index_schema(crt_idx_stmt))) {
  } else {
    if (definition.has_partition) {
      ret = resolve_index_partition_node(peg::index_definition_partition(statement), crt_idx_stmt);
    }

    if (OB_SUCC(ret)) {
      crt_idx_stmt->set_if_not_exists(definition.if_not_exists);
      // Set block size, if block size is not specified, then use the main table block size
      // Otherwise use default block_size
      if (!is_spec_block_size) {
        ObCreateIndexArg &index_arg = crt_idx_stmt->get_create_index_arg();
        index_arg.index_option_.block_size_ = tbl_schema->get_block_size();
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(resolve_hints(peg::index_hints(statement), *crt_idx_stmt, *tbl_schema))) {
    }
  }
  if (OB_SUCC(ret)) {
    ObCreateIndexArg &index_arg = crt_idx_stmt->get_create_index_arg();
    char* buf = nullptr;
    int64_t pos = 0;
    if (is_vec_index(index_arg.index_type_)) {
      if (!is_vec_index(vec_index_type_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (FALSE_IT(index_arg.index_type_ = vec_index_type_)) {
      } else if (FALSE_IT(index_arg.index_schema_.set_index_params(index_params_))) {
      } else if (tbl_schema->is_view_table()) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "create vector index on view table is");
      } else if (OB_ISNULL(buf = reinterpret_cast<char*>(allocator_->alloc(sizeof(char) * OB_MAX_PROC_ENV_LENGTH)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
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
  } else {
    create_index_stmt = static_cast<ObCreateIndexStmt*>(stmt_);
  }
  if (OB_FAIL(ret)) {
    //empty
  } else if (is_column_exists(sort_column_array_, column_key, check_prefix_len)) {
    ret = OB_ERR_COLUMN_DUPLICATE;
    LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, sort_column.column_name_.length(), sort_column.column_name_.ptr());
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
  } else if (OB_ISNULL(create_index_stmt) || OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
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
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "global fulltext index is");
      } else {
        // set type to fts_index_aux first, append other fts arg later
        index_arg.index_type_ = INDEX_TYPE_FTS_INDEX_LOCAL;
      }
    } else if (MULTI_KEY == index_keyname_ || MULTI_UNIQUE_KEY == index_keyname_) {
      if (global_) {
        ret = OB_NOT_SUPPORTED;
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
    } else if (FTS_KEY == index_keyname_ &&
               OB_FAIL(share::ObFtsIndexBuilderUtil::generate_fts_parser_name_and_property(tbl_schema, index_arg,
                                                                       allocator_))) {
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
  } else {
    ObTableSchema::const_column_iterator begin = tbl_schema.column_begin();
    ObTableSchema::const_column_iterator end = tbl_schema.column_end();
    ObCreateIndexArg &arg = create_index_stmt->get_create_index_arg();
    for (; OB_SUCC(ret) && begin != end; begin++) {
      ObColumnSchemaV2 *col = (*begin);
      if (OB_ISNULL(col)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (col->is_extend()) {
      }
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
