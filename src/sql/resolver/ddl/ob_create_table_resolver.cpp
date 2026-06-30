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
#include "sql/resolver/ddl/ob_create_table_resolver.h"
#include "share/ob_fts_index_builder_util.h"
#include "sql/rewrite/ob_transform_utils.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "share/ob_index_builder_util.h"
#include "observer/ob_server.h"
#include "sql/resolver/cmd/ob_help_resolver.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "share/vector_index/ob_vector_index_util.h"
#include "share/ob_vec_index_builder_util.h"
#include "share/table/ob_ttl_util.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace share::schema;
using namespace omt;
namespace sql
{
ObCreateTableResolver::ObCreateTableResolver(ObResolverParams &params)
    : ObCreateTableResolverBase(params),
      cur_column_id_(OB_APP_MIN_COLUMN_ID - 1),
      primary_keys_(),
      column_name_set_(),
      if_not_exist_(false),
      index_arg_(),
      current_index_name_set_(),
      cur_udt_set_id_(0),
      vec_index_col_ids_(),
      has_vec_index_(false),
      has_fts_index_(false),
      has_multivalue_index_(false)
{
}

ObCreateTableResolver::~ObCreateTableResolver()
{
}

uint64_t ObCreateTableResolver::gen_column_id()
{
  return ++cur_column_id_;
}

uint64_t ObCreateTableResolver::gen_udt_set_id()
{
  return ++cur_udt_set_id_;
}

int64_t ObCreateTableResolver::get_primary_key_size() const
{
  return primary_keys_.count();
}

int ObCreateTableResolver::add_primary_key_part(const ObString &column_name,
                                                ObArray<ObColumnResolveStat> &stats,
                                                int64_t &pk_data_length)
{
  int ret = OB_SUCCESS;
  ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
  ObColumnSchemaV2 *col = NULL;
  if (OB_ISNULL(create_table_stmt)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "stmt is null", KP(create_table_stmt), K(ret));
  } else if (OB_FAIL(ObCreateTableResolverBase::add_primary_key_part(column_name,
                                                              create_table_stmt->get_create_table_arg().schema_,
                                                              primary_keys_.count(),
                                                              pk_data_length,
                                                              col))) {
  } else if (OB_FAIL(primary_keys_.push_back(col->get_column_id()))) {
  } else {
    // In mysql mode, when creating a table, if the primary key column is set null or set default value = null, an error should be reported
    // oracle mode, when creating a table, if the primary key column is set null or set default value = null, no error will be reported, so skip the following check
    ObColumnResolveStat *stat = NULL;
    for (int64_t i = 0; NULL == stat && OB_SUCC(ret) && i < stats.count(); ++i) {
      if (stats.at(i).column_id_ == col->get_column_id()) {
        stat = &stats.at(i);
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(stat)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "fail to find column stat", K(ret), K(column_name));
    } else if (stat->is_set_null_ || (stat->is_set_default_value_ && col->get_cur_default_value().is_null())) {
      ret = OB_ERR_PRIMARY_CANT_HAVE_NULL;
    }
  }
  return ret;
}

int ObCreateTableResolver::add_hidden_tablet_seq_col()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt_)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "stmt is NULL", K(stmt_), K(ret));
  } else if (0 == get_primary_key_size()) {
    ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
    ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
    ObColumnSchemaV2 hidden_pk;
    hidden_pk.reset();
    hidden_pk.set_column_id(OB_HIDDEN_PK_INCREMENT_COLUMN_ID);
    hidden_pk.set_data_type(ObUInt64Type);
    hidden_pk.set_nullable(false);
    hidden_pk.set_is_hidden(true);
    hidden_pk.set_charset_type(CHARSET_BINARY);
    hidden_pk.set_collation_type(CS_TYPE_BINARY);
    if (OB_FAIL(hidden_pk.set_column_name(OB_HIDDEN_PK_INCREMENT_COLUMN_NAME))) {
    } else if (OB_FAIL(primary_keys_.push_back(OB_HIDDEN_PK_INCREMENT_COLUMN_ID))) {
    } else {
      hidden_pk.set_rowkey_position(primary_keys_.count());
      if (OB_FAIL(table_schema.add_column(hidden_pk))) {
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "tablet seq expects to be the first primary key", K(stmt_), K(ret));
  }
  return ret;
}

// Set additional information for the temporary table
int ObCreateTableResolver::set_temp_table_info(ObTableSchema &table_schema, ParseNode *commit_option_node)
{
  int ret = OB_SUCCESS;
  session_info_->set_has_temp_table_flag();
  if (OB_FAIL(set_table_name(table_name_))) {
  } else if (session_info_->is_obproxy_mode() && 0 == session_info_->get_sess_create_time()) {
    ret = OB_NOT_SUPPORTED;
    SQL_RESV_LOG(WARN, "can't create temporary table via obproxy, upgrade obproxy first", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "obproxy version is too old, create temporary table");
  } else {
    table_schema.set_table_type(TMP_TABLE);
    table_schema.set_session_id(session_info_->get_sessid_for_table()); ////Set session_id and session creation time, used for judgment during cleanup, Oracle function is different and does not need to be set

    table_schema.set_sess_active_time(ObTimeUtility::current_time());
  }
  LOG_DEBUG("resolve create temp table", K(session_info_->is_obproxy_mode()), K(*session_info_), K(table_schema));
  return ret;
}

int ObCreateTableResolver::set_default_micro_index_clustered_(share::schema::ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  // set default value. If user_specified, it is modifed in resolve_table_option.
  if (OB_FAIL(ret)) {
    // error occurred
  } else { // shared_nothing
    table_schema.set_micro_index_clustered(false);
  }
  return ret;
}

int ObCreateTableResolver::set_default_enable_macro_block_bloom_filter_(share::schema::ObTableSchema &table_schema)
{
  table_schema.set_enable_macro_block_bloom_filter(false);
  return OB_SUCCESS;
}

ERRSIM_POINT_DEF(ERRSIM_SET_MERGE_ENGINE_DELETE_INSERT);
int ObCreateTableResolver::set_default_merge_engine_type_(share::schema::ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  if (ERRSIM_SET_MERGE_ENGINE_DELETE_INSERT) {
    table_schema.set_merge_engine_type(ObMergeEngineType::OB_MERGE_ENGINE_DELETE_INSERT);
  } else {
    const char *delete_insert = ObMergeEngineStoreFormat::get_merge_engine_type_name(ObMergeEngineType::OB_MERGE_ENGINE_DELETE_INSERT);
    if (0 == GCONF.default_table_merge_engine.case_compare(delete_insert)) {
      table_schema.set_merge_engine_type(ObMergeEngineType::OB_MERGE_ENGINE_DELETE_INSERT);
    } else {
      table_schema.set_merge_engine_type(ObMergeEngineType::OB_MERGE_ENGINE_PARTIAL_UPDATE);
    }
  }
  return ret;
}

int ObCreateTableResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  bool is_temporary_table = false;
  const bool is_mysql_mode = true;
  ParseNode *create_table_node = const_cast<ParseNode*>(&parse_tree);
  CHECK_COMPATIBILITY_MODE(session_info_);
  if (OB_ISNULL(create_table_node)
      || T_CREATE_TABLE != create_table_node->type_
      || (CREATE_TABLE_NUM_CHILD != create_table_node->num_child_ &&
          CREATE_TABLE_AS_SEL_NUM_CHILD != create_table_node->num_child_)
      || OB_ISNULL(create_table_node->children_)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid argument.", K(ret));
  } else {
    ObCreateTableStmt *create_table_stmt = NULL;
    ObString table_name;
    ObString database_name;
    uint64_t database_id = OB_INVALID_ID;
    ObSEArray<ObString, 8> pk_columns_name;
    bool is_create_as_sel = (CREATE_TABLE_AS_SEL_NUM_CHILD == create_table_node->num_child_);
    
    if (OB_ISNULL(create_table_stmt = create_stmt<ObCreateTableStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "failed to create select stmt", K(ret));
    } else {
      create_table_stmt->set_allocator(*allocator_);
      stmt_ = create_table_stmt;
    }
    //resolve temporary option or external table option
    if (OB_SUCC(ret)) {
      if (NULL != create_table_node->children_[0]) {
          switch (create_table_node->children_[0]->type_) {
            case T_TEMPORARY:
              if (create_table_node->children_[5] != NULL) { // Temporary table does not support partitioning
                ret = OB_ERR_TEMPORARY_TABLE_WITH_PARTITION;
              } else {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "MySQL compatible temporary table");
              }
              break;
            default:
              ret = OB_INVALID_ARGUMENT;
              SQL_RESV_LOG(WARN, "invalid argument.",
                           K(ret), K(create_table_node->children_[0]->type_));
            }
      }
    }
    //resolve if_not_exists
    if (OB_SUCC(ret)) {
      if (NULL != create_table_node->children_[1]) {
        if (T_IF_NOT_EXISTS != create_table_node->children_[1]->type_) {
          ret = OB_INVALID_ARGUMENT;
          SQL_RESV_LOG(WARN, "invalid argument.",
                       K(ret), K(create_table_node->children_[1]->type_));
        } else {
          if_not_exist_ = true;
        }
      }
    }
    //resolve table_name
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(create_table_node->children_[2])) {
        ret = OB_INVALID_ARGUMENT;
        SQL_RESV_LOG(WARN, "invalid argument.", K(ret));
      } else if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
        ret = OB_NOT_INIT;
        SQL_RESV_LOG(WARN, "session_info is null.", K(ret));
      } else if (OB_FAIL(resolve_table_relation_node(create_table_node->children_[2], table_name, database_name,
                                                     false, false))) {
      } else if ((ObString(OB_RECYCLEBIN_SCHEMA_NAME) == database_name
                  && ObSQLSessionInfo::USER_SESSION == session_info_->get_session_type())
                 || ObString(OB_PUBLIC_SCHEMA_NAME) == database_name) {
        ret = OB_OP_NOT_ALLOW;
        SQL_RESV_LOG(WARN, "create table in recyclebin database is not permitted", K(ret));
      } else if (OB_FAIL(set_table_name(table_name))) {
      } else if (OB_FAIL(schema_checker_->get_database_id(database_name, database_id))) {
        if (OB_ERR_BAD_DATABASE == ret) {
          LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
        }
        SQL_RESV_LOG(WARN, "get database id failed", K(ret));
      } else if (OB_FAIL(set_database_name(database_name))) {
      } else if (OB_FAIL(ob_write_string(
                  *allocator_, database_name, create_table_stmt->get_non_const_db_name()))) {
      } else if (ObCharset::case_insensitive_equal(ObString(strlen(OB_SYS_DATABASE_NAME), OB_SYS_DATABASE_NAME), database_name)) {
        
        uint64_t database_id = OB_INVALID_ID;
        if (OB_ISNULL(schema_checker_)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "schema_checker_ is null.", K(ret));
        } else if (OB_FAIL(schema_checker_->get_database_id(database_name, database_id)))  {
        } else {
          create_table_stmt->set_database_id(database_id);
        }
      } else {
        // When creating a table, resolver passes database_name to the RS layer instead of database_id,
        // Generate temporary schema_checker, need to use database_id,
        // So here we use an arbitrary value to set database_id,
        // resolve ends, set database_id to OB_INVALID_ID
        create_table_stmt->set_database_id(generate_table_id());
      }
      // string column length and default value length judgment logic depends on the character set type of the column
      // mysql's behavior is if a column specifies a character set then use the specified one, otherwise use the table's character set
      // Therefore need to resolve table charset&collation before resolve_table_elements
      if (OB_SUCC(ret)) {
        if (OB_FAIL(resolve_table_charset_info(create_table_node->children_[4]))) {
        } else if (is_create_as_sel) {
          if (OB_NOT_NULL(create_table_node->children_[3]) && T_TABLE_ELEMENT_LIST != create_table_node->children_[3]->type_) {
            ret = OB_INVALID_ARGUMENT;
            SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(create_table_node->children_[2]->type_));
          } else { /* do nothing */ }
        }  else if (OB_ISNULL(create_table_node->children_[3])) {
          ret = OB_INVALID_ARGUMENT;
          SQL_RESV_LOG(WARN, "invalid argument.", K(ret));
        } else if (T_TABLE_ELEMENT_LIST != create_table_node->children_[3]->type_) {
          ret = OB_INVALID_ARGUMENT;
          SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(create_table_node->children_[3]->type_));
        } else {
          // do nothing
        }

        // 1、 resolve table_id first for check whether is inner_table
        if (OB_SUCC(ret) && OB_FAIL(resolve_table_id_pre(create_table_node->children_[4]))) {
          SQL_RESV_LOG(WARN, "resolve_table_id_pre failed", K(ret));
        }

        // resolve table organizations before resolve table elements
        if (OB_FAIL(ret)) {
          //do nothing
        } else if (!is_inner_table(table_id_) &&
                    OB_FAIL(resolve_table_organization(&GCONF, create_table_node->children_[4]))) {
          SQL_RESV_LOG(WARN, "resolve table organization failed", K(ret));
        }

        //consider index can be defined before column, so column should be
        //resolved firstly;avoid to rescan table_element_list_node, use a
        //array named index_node_position_list to record the position of indexes
        ParseNode *table_element_list_node = create_table_node->children_[3];
        ObArray<int> index_node_position_list;
        ObArray<int> foreign_key_node_position_list;
        ObArray<int> table_level_constraint_list;
        if (OB_SUCC(ret)) {
          if (false == is_create_as_sel) {
            if (OB_FAIL(resolve_table_elements(table_element_list_node, index_node_position_list, foreign_key_node_position_list, table_level_constraint_list, RESOLVE_ALL))) {
            }
          } else {
            if (OB_FAIL(resolve_table_elements(table_element_list_node, index_node_position_list, foreign_key_node_position_list, table_level_constraint_list, RESOLVE_COL_ONLY))) {
            } else if (OB_FAIL(resolve_insert_mode(&parse_tree))) {
            } else if (OB_FAIL(resolve_table_elements_from_select(parse_tree))) {
            } else if (OB_FAIL(resolve_table_elements(table_element_list_node, index_node_position_list, foreign_key_node_position_list, table_level_constraint_list, RESOLVE_NON_COL))) {
            }
          }
          if (OB_SUCC(ret)) {
            reset();
            // When the user creates a table without specifying a primary key, it is internally implemented as a new table without a primary key, TableOrganizationFormat specified by the user is currently not supported
            if (0 == get_primary_key_size() || is_organization_set_to_heap()) {
              // change default no pk to heap table
              table_mode_.table_organization_mode_ = is_organization_set_to_heap() ? TOM_HEAP_ORGANIZED : TOM_INDEX_ORGANIZED;
              table_mode_.pk_exists_ = 0 == get_primary_key_size() ? TOM_TABLE_WITHOUT_PK : TOM_TABLE_WITH_PK;
              table_mode_.pk_mode_ = TPKM_TABLET_SEQ_PK;
            }
            if (OB_SUCC(ret)) {
              const char *ptr = NULL;
              if (OB_ISNULL(ptr = GCONF.default_auto_increment_mode.get_value())) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("default auto increment mode ptr is null", K(ret));
              } else {
                table_mode_.auto_increment_mode_ =
                  (0 == ObString::make_string("order").case_compare(ptr)) ?
                    ObTableAutoIncrementMode::ORDER : ObTableAutoIncrementMode::NOORDER;
                table_mode_.rowid_mode_ = GCONF.default_enable_extended_rowid ?
                    ObTableRowidMode::ROWID_EXTENDED : ObTableRowidMode::ROWID_NORMAL;
              }
            }
            ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
            if (!table_schema.is_sys_table()) {
              pctfree_ = 0; // set default pctfree value for non-sys table
            }
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(set_default_micro_index_clustered_(table_schema))) {
            } else if (OB_FAIL(set_default_enable_macro_block_bloom_filter_(table_schema))) {
            } else if (OB_FAIL(set_default_merge_engine_type_(table_schema))) {
            } else if (OB_FAIL(resolve_table_options(create_table_node->children_[4], false))) {
            } else if (OB_FAIL(set_table_option_to_schema(table_schema))) {
            } else if (OB_FAIL(check_max_row_data_length(table_schema))) {
            } else {
              table_schema.set_collation_type(collation_type_);
              table_schema.set_charset_type(charset_type_);
              // No longer need this step. At the beginning of resolve, directly parse out the collation/charset information of the table, by the time column information is resolved, it can already be obtained
              // Table's collation/charset information
              //if (OB_FAIL(table_schema.fill_column_collation_info())) {
              //  SQL_RESV_LOG(WARN, "fail to fill column collation info", K(ret), K(table_name_));
              //} else {
              //  //do nothing
              //}
            }
          }
        }

        // !!Attention!! resolve_partition_option should always call after resolve_table_options
        if (OB_SUCC(ret)) {
          ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
          if (OB_FAIL(resolve_partition_option(
                      create_table_node->children_[5], table_schema,
                      (is_mysql_mode && 1 == create_table_node->reserved_) ? false : true))) {
          }
        }

        // 4.0 new heap table has hidden primary key (tablet seq)
        if (OB_SUCC(ret) && (0 == get_primary_key_size() || is_organization_set_to_heap())
            && (TOM_HEAP_ORGANIZED == table_mode_.table_organization_mode_ ||
              (TOM_INDEX_ORGANIZED == table_mode_.table_organization_mode_ &&
              TOM_TABLE_WITHOUT_PK == table_mode_.pk_exists_))) {
          ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
          if (OB_FAIL(add_hidden_tablet_seq_col())) {
          }
        }

        // column group
        if (OB_SUCC(ret)) {
          if (OB_FAIL(resolve_column_group(create_table_node->children_[6]))) {
          }
        }

        if (OB_SUCC(ret)) {
          ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
          if (OB_FAIL(check_skip_index(table_schema))) {
          }
        }

        if (OB_SUCC(ret)) {
          ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
          table_schema.set_define_user_id(session_info_->get_priv_user_id());
          create_table_stmt->set_if_not_exists(if_not_exist_);
          if (false == is_temporary_table && OB_NOT_NULL(create_table_node->children_[7])) {
            ret = OB_ERR_PARSER_SYNTAX;
            SQL_RESV_LOG(WARN, "on commit option can only be used for temp table", K(ret));
          } else if (is_temporary_table && OB_FAIL(set_temp_table_info(table_schema, create_table_node->children_[7]))) {
            SQL_RESV_LOG(WARN, "set temp table info failed", K(ret));
          } else if (OB_FAIL(table_schema.set_table_name(table_name_))) {
          } else {
            create_table_stmt->set_database_id(OB_INVALID_ID);
          }
          // Query table creation or temporary table T creation time, record the received request obs address obs#1, used for obs backend job cleanup, to restrict T can only be dropped by the original obs#1
          if (OB_SUCC(ret) && (is_temporary_table || is_create_as_sel)) {
            char create_host_str[OB_MAX_HOST_NAME_LENGTH];
            MYADDR.ip_port_to_string(create_host_str, OB_MAX_HOST_NAME_LENGTH);
            table_schema.set_create_host(create_host_str);
            if (is_temporary_table) {
              table_schema.set_sess_active_time(ObTimeUtility::current_time());
            }
          }
        }
        // put after parsing temporary table information settings, because it involves error checking for foreign key reference not supported by temporary tables
        if (OB_SUCC(ret)) {
          if (OB_FAIL(resolve_index(table_element_list_node, index_node_position_list))) {
          } else if (OB_FAIL(resolve_foreign_key(table_element_list_node, foreign_key_node_position_list))) {
          } else if (OB_FAIL(resolve_table_level_constraint_for_mysql(table_element_list_node, table_level_constraint_list))) {
          } else { /* do nothing */ }
        }

        if (OB_SUCC(ret)) {
          ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
          ParseNode *partition_node = create_table_node->children_[5];
          if (table_schema.is_user_table()) {
            if (nullptr != partition_node) {
              // acquire partition_node with similar logic like resolve_partition_option()
              const bool is_partition_option_node_with_opt = !is_mysql_mode || 1 != create_table_node->reserved_;
              if (!is_partition_option_node_with_opt) {
                // current node is partition node
              } else if (T_VERTICAL_COLUMNS_PARTITION == partition_node->type_) {
                // no need to resolve, partition node doesn't exist
                partition_node = nullptr;
              } else if (T_PARTITION_OPTION != partition_node->type_) {
                ret = OB_INVALID_ARGUMENT;
                SQL_RESV_LOG(WARN, "node type is invalid.", K(ret), K(partition_node->type_));
              } else if (OB_UNLIKELY(partition_node->num_child_ < 1 || partition_node->num_child_ > 2)) {
                ret = OB_INVALID_ARGUMENT;
                SQL_RESV_LOG(WARN, "node number is invalid.", K(ret), K(partition_node->num_child_));
              } else if (OB_ISNULL(partition_node->children_[0])) {
                ret = OB_ERR_UNEXPECTED;
                SQL_RESV_LOG(WARN, "partition node is null.", K(ret));
              } else {
                partition_node = partition_node->children_[0];
              }
            }

            if (FAILEDx(resolve_auto_partition_with_tenant_config(create_table_stmt,
                                                                  partition_node,
                                                                  table_schema))) {
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)){
      if (OB_FAIL(deep_copy_string_in_part_expr(create_table_stmt))) {
      }
    }
    if (OB_SUCC(ret) && is_create_as_sel) {
      if (OB_FAIL(resolve_hints(create_table_node->children_[9],
                               *create_table_stmt,
                               create_table_stmt->get_create_table_arg().schema_))) {
      }
    }

    // check storage cache policy for partitioned table
    // because we only know the if the table is partitioned table after resolve_table_options
    if (OB_SUCC(ret) && GCTX.is_shared_storage_mode() && is_mysql_mode) {
      ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
      if (OB_FAIL(check_create_stmt_storage_cache_policy(table_schema.get_storage_cache_policy(), &table_schema))) {
      }
    }

  }
  return ret;
}
// Generate array of primary key column names
// Generate an array composed of the indices of uk in index_arg_list
// Check if the primary key and unique index are built on exactly the same (including order) columns or column families
// Check if the unique index and unique index are built on exactly the same (including order) columns or column families

int ObCreateTableResolver::resolve_partition_option(
    ParseNode *node, ObTableSchema &table_schema, const bool is_partition_option_node_with_opt)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObCreateTableResolverBase::resolve_partition_option(node, table_schema, is_partition_option_node_with_opt))) {
  }
  if (OB_SUCC(ret) && OB_NOT_NULL(node)) {
    if (OB_FAIL(check_generated_partition_column(table_schema))) {
    } else if (OB_FAIL(table_schema.check_primary_key_cover_partition_column())) {
    }
  }
  return ret;
}

int ObCreateTableResolver::check_generated_partition_column(ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  const ObPartitionKeyInfo &part_key_info = table_schema.get_partition_key_info();
  const ObPartitionKeyColumn *part_column = NULL;
  ObColumnSchemaV2 *column_schema = NULL;
  ObRawExpr *dependant_expr = NULL;
  ObString expr_def;
  for (int64_t idx = 0; OB_SUCC(ret) && idx < part_key_info.get_size(); ++idx) {
    if (OB_ISNULL(part_column = part_key_info.get_column(idx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Part column is NULL", K(ret), K(idx));
    } else if (OB_ISNULL(column_schema = table_schema.get_column_schema(part_column->column_id_))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Failed to get column schema", K(ret), K(part_column->column_id_));
    } else if (column_schema->is_generated_column()) {
      if (OB_FAIL(column_schema->get_cur_default_value().get_string(expr_def))) {
      } else if (OB_FAIL(ObRawExprUtils::build_generated_column_expr(NULL,
                                                                     expr_def,
                                                                     *params_.expr_factory_,
                                                                     *params_.session_info_,
                                                                     table_schema,
                                                                     dependant_expr,
                                                                     schema_checker_))) {
      } /*
        if gc column is partition key, then this is no restriction
        else {
        //check Expr Function for generated column whether allowed.
        ObRawExprPartFuncChecker part_func_checker(true);
        if (OB_FAIL(gen_col_expr->preorder_accept(part_func_checker))) {
          LOG_WARN("check partition function failed", K(ret));
        }
      }*/
    } else { }//do nothing
  }

  return ret;
}

int ObCreateTableResolver::check_column_name_duplicate(const ParseNode *node)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "node is null.", K(ret));
  } else if (OB_ISNULL(stmt_)
      || T_TABLE_ELEMENT_LIST != node->type_
      || OB_ISNULL(node->children_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(node->type_), K(node->num_child_));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < node->num_child_; ++i) {
      ParseNode *element = node->children_[i];
      if (OB_ISNULL(element)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("element node is null", K(ret));
      } else if (OB_LIKELY(T_COLUMN_DEFINITION == element->type_)) {
        if (OB_USER_ROW_MAX_COLUMNS_COUNT < column_name_set_.count()) {
          ret = OB_ERR_TOO_MANY_COLUMNS;
        } else if (element->num_child_ < COLUMN_DEFINITION_NUM_CHILD ||
            OB_ISNULL(element->children_) || OB_ISNULL(element->children_[COLUMN_REF_NODE]) ||
            T_COLUMN_REF != element->children_[COLUMN_REF_NODE]->type_ ||
            COLUMN_DEF_NUM_CHILD != element->children_[COLUMN_REF_NODE]->num_child_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid parse node", K(ret));
        } else {
          ParseNode *name_node = element->children_[COLUMN_REF_NODE]->children_[COLUMN_NAME_NODE];
          if (OB_ISNULL(name_node)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("name node can not be null", K(ret));
          } else if (0 == name_node->str_len_) {
            ret = OB_WRONG_COLUMN_NAME;
            LOG_USER_ERROR(OB_WRONG_COLUMN_NAME, (int)name_node->str_len_, name_node->str_value_);
          } else {
            ObString name(name_node->str_len_, name_node->str_value_);
            ObCollationType cs_type = CS_TYPE_INVALID;
            if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
            } else if (OB_FAIL(ObSQLUtils::check_column_name(cs_type, name))) {
            } else {
              ObColumnNameHashWrapper column_name_key(name);
              if (OB_HASH_EXIST  == column_name_set_.exist_refactored(column_name_key)) {
                ret = OB_ERR_COLUMN_DUPLICATE;
                LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, name.length(), name.ptr());
              } else {
                if (OB_FAIL(column_name_set_.set_refactored(column_name_key))) {
                }
              }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      column_name_set_.reset();
    }
  }
  return ret;
}

int ObCreateTableResolver::resolve_primary_key_node(const ParseNode &pk_node,
                                                    ObArray<ObColumnResolveStat> &stats)
{
  int ret = OB_SUCCESS;

  const bool is_mysql_mode = true;
  if ((is_mysql_mode ? 3 < pk_node.num_child_ : 2 < pk_node.num_child_ )
      || OB_ISNULL(pk_node.children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "the num_child of primary_node is wrong.",
                 K(ret), K(pk_node.num_child_), K(pk_node.children_));
  } else {
    ParseNode *column_list_node = pk_node.children_[0];
    if (OB_ISNULL(column_list_node)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "column_list_node is null.", K(ret));
    } else if (T_COLUMN_LIST != column_list_node->type_
               || column_list_node->num_child_ <= 0
               || OB_ISNULL(column_list_node->children_)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "unexpected.",
                   K(ret), K(column_list_node->type_), K(column_list_node->num_child_));
    } else {
      ParseNode *key_node = NULL;
      int64_t pk_data_length = 0;
      for (int32_t i = 0; OB_SUCC(ret) && i < column_list_node->num_child_; ++i) {
        if (OB_ISNULL(column_list_node->children_[i])) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "column_list_node->children_[i] is null.", K(ret));
        } else {
          key_node = column_list_node->children_[i];
          ObString key_name;
          if (OB_ISNULL(key_node)) {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "key_node is null.", K(ret));
          } else {
            key_name.assign_ptr(key_node->str_value_,static_cast<int32_t>(key_node->str_len_));
            if (OB_FAIL(add_primary_key_part(key_name, stats, pk_data_length))) {
            }
          }
        }
      }
    }
    if (OB_SUCC(ret) && is_mysql_mode && NULL != pk_node.children_[1]) {
      ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
      if (T_USING_HASH == pk_node.children_[1]->type_) {
        create_table_stmt->set_index_using_type(share::schema::USING_HASH);
      } else {
        create_table_stmt->set_index_using_type(share::schema::USING_BTREE);
      }
    }
    if (OB_SUCC(ret) && is_mysql_mode) {
      if (NULL != pk_node.children_[2]) {
        ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
        ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
        ObString pk_comment;
        pk_comment.assign_ptr(pk_node.children_[2]->str_value_,static_cast<int32_t>(pk_node.children_[2]->str_len_));
        if (OB_FAIL(table_schema.set_pk_comment(pk_comment))) {
        }
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::get_resolve_stats_from_table_schema(
    const ObTableSchema &table_schema,
    ObArray<ObColumnResolveStat> &stats)
{
  int ret = OB_SUCCESS;
  ObColumnResolveStat stat;
  ObTableSchema::const_column_iterator it_begin = table_schema.column_begin();
  ObTableSchema::const_column_iterator it_end = table_schema.column_end();
  for(; OB_SUCC(ret) && it_begin != it_end; it_begin++) {
    if (OB_ISNULL(it_begin)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("it_begin should not be NULL", K(ret));
    } else if (OB_ISNULL(*it_begin)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("*it_begin should not be NULL", K(ret));
    } else {
      const ObColumnSchemaV2 &column_schema = **it_begin;
      stat.reset();
      stat.column_id_ = column_schema.get_column_id();
      if (OB_FAIL(stats.push_back(stat))) {
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::resolve_table_elements(const ParseNode *node,
                                                  ObArray<int> &index_node_position_list,
                                                  ObArray<int> &foreign_key_node_position_list,
                                                  ObArray<int> &table_level_constraint_list,
                                                  const int resolve_rule) // Query table creation will call twice, first time only parse columns
                                                                          // Second parsing non-column
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
   // do nothing, create table t as select ... will come here
  } else if (OB_ISNULL(stmt_)
             || OB_ISNULL(session_info_)
             || T_TABLE_ELEMENT_LIST != node->type_
             || node->num_child_ < 1
             || OB_ISNULL(node->children_)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid argument",
                 K(ret), K(node->type_), K(node->num_child_));
  } else {
    ObSEArray<ObString, 4> gen_col_expr_arr;
    ParseNode *primary_node = NULL;
    ParseNode *primary_node_in_heap_table = NULL;
    ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
    uint64_t autoinc_column_id = 0;
    int64_t first_timestamp_index = -1;
    int64_t row_data_length = 0;
    ObArray<ObColumnResolveStat> stats;
    ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
    
    bool has_visible_col = false;
    bool primary_key_set_in_heap_table = false;
    // Store the column schema after resolve_column_definition in resolved_cols
    // To support generating columns in any order, all column_schema are generated first and then uniformly stored in table_schema
    ObSEArray<ObColumnSchemaV2, SEARRAY_INIT_NUM> resolved_cols;
    // The column needs to be case-sensitive based on tenant id, here we first set the tenant id into table_schema
    //RESOLVE_NON_COL needs to add the columns in the query to stats in order to resolve PK constraint information etc.
    if (OB_FAIL(ret)) {
      //do nothing ...
    } else if (RESOLVE_NON_COL == resolve_rule) {
      if (OB_FAIL(get_resolve_stats_from_table_schema(table_schema, stats))) {
      }
    } else if (OB_FAIL(check_column_name_duplicate(node))) {
    }
    // To implement defining generated columns in any order, we need to traverse the node twice
    // First traversal, parse all column names, generate column_schema and store in resolved_cols
    for (int32_t i = 0; OB_SUCC(ret) && i < node->num_child_; ++i) {
      ParseNode *element = node->children_[i];
      CK (OB_NOT_NULL(element));
      if (OB_FAIL(ret)) {
      } else if ((RESOLVE_NON_COL == resolve_rule && T_COLUMN_DEFINITION == element->type_)
                 || (RESOLVE_COL_ONLY == resolve_rule && T_COLUMN_DEFINITION != element->type_)) {
        //continue
      } else if (OB_LIKELY(T_COLUMN_DEFINITION == element->type_)) {
        ObColumnSchemaV2 column;
        
        ObColumnResolveStat stat;
        common::ObString pk_name;
        if (OB_INVALID_ID == column.get_column_id()) {
          column.set_column_id(gen_column_id());
        } else {
          // column id set by user sql such as "c1 int comment 'manual' id 17"
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(resolve_column_name(column, element))) {
        } else {
          if (GCONF._enable_pseudo_partition_id &&
              ObResolverUtils::is_pseudo_partition_column_name(column.get_column_name_str())) {
            ret = OB_ERR_COLUMN_DUPLICATE;
            LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, column.get_column_name_str().length(),
              column.get_column_name_str().ptr());
            LOG_WARN("invalid partition pseudo column", K(ret), K(column.get_column_name_str()));
          }
          OZ (resolved_cols.push_back(column));
        }
      }
    }

    int64_t resolved_cols_count = resolved_cols.count();
    // Second pass traversal, use resolved_cols to parse out all column schema and save to table_schema
    // And parse index etc
    for (int32_t i = 0, ele_pos = 0; OB_SUCC(ret) && i < node->num_child_; ++i) {
      ParseNode *element = node->children_[i];
      CK (OB_NOT_NULL(element));
      if (OB_FAIL(ret)) {
      } else if ((RESOLVE_NON_COL == resolve_rule && T_COLUMN_DEFINITION == element->type_)
                 || (RESOLVE_COL_ONLY == resolve_rule && T_COLUMN_DEFINITION != element->type_)) {
        continue;
      } else if (OB_LIKELY(T_COLUMN_DEFINITION == element->type_)) {
        bool is_modify_column_visibility = false;
        const bool is_create_table_as = (RESOLVE_COL_ONLY == resolve_rule);
        CK (ele_pos >= 0 && ele_pos < resolved_cols_count);
        if (OB_SUCC(ret)) {
          ObColumnSchemaV2 &column = resolved_cols.at(ele_pos);
          ObColumnResolveStat stat;
          common::ObString pk_name;
          // ele_pos + 1, points to the next column_schema
          ++ele_pos;
          ObString tmp_str[ObNLSFormatEnum::NLS_MAX];
          tmp_str[ObNLSFormatEnum::NLS_DATE] = session_info_->get_local_nls_date_format();
          tmp_str[ObNLSFormatEnum::NLS_TIMESTAMP] = session_info_->get_local_nls_timestamp_format();
          tmp_str[ObNLSFormatEnum::NLS_TIMESTAMP_TZ] = session_info_->get_local_nls_timestamp_tz_format();
          if (OB_FAIL(resolve_column_definition(column, element, stat,
                                                is_modify_column_visibility,
                                                pk_name,
                                                table_schema,
                                                false,
                                                is_create_table_as))) {
          } else if (OB_FAIL(check_default_value(column.get_cur_default_value(),
                                          session_info_->get_tz_info_wrap(),
                                          tmp_str,
                                          NULL,
                                          *allocator_,
                                          table_schema,
                                          resolved_cols,
                                          column,
                                          gen_col_expr_arr,
                                          session_info_->get_sql_mode(),
                                          session_info_,
                                          true, /* allow_sequence */
                                          schema_checker_,
                                          NULL == element->children_[1]))) {
          } else if (column.is_string_type() || is_lob_storage(column.get_data_type())) {
            int64_t length = 0;
            if (OB_FAIL(column.get_byte_length(length, false))) {
            } else if (ob_is_string_tc(column.get_data_type()) && length > OB_MAX_VARCHAR_LENGTH) {
              ret = OB_ERR_TOO_LONG_COLUMN_LENGTH;
              LOG_USER_ERROR(OB_ERR_TOO_LONG_COLUMN_LENGTH, column.get_column_name(), static_cast<int32_t>(OB_MAX_VARCHAR_LENGTH));
            } else if (is_lob_storage(column.get_data_type())) {
              ObLength max_length = 0;
              max_length = ObAccuracy::MAX_ACCURACY2[0][column.get_data_type()].get_length();
              if (length > max_length) {
                ret = OB_ERR_TOO_LONG_COLUMN_LENGTH;
                LOG_USER_ERROR(OB_ERR_TOO_LONG_COLUMN_LENGTH, column.get_column_name(),
                    ObAccuracy::MAX_ACCURACY2[0][column.get_data_type()].get_length());
              } else {
                // table lob inrow theshold has not been parsed, so use handle length check
                // will recheck after parsing table lob inrow theshold
                length = min(length, OB_MAX_LOB_HANDLE_LENGTH);
              }
            }
            if (OB_SUCC(ret) && (row_data_length += length) > OB_MAX_USER_ROW_LENGTH) {
              ret = OB_ERR_TOO_BIG_ROWSIZE;
            }
          }

          if (OB_SUCC(ret) && column.is_generated_column()) {
            if (OB_FAIL(column.set_orig_default_value(column.get_cur_default_value()))) {
            }
          }
          if (OB_SUCC(ret)){
            stat.column_id_ = column.get_column_id();
            if (stat.is_primary_key_) {
              if (is_organization_set_to_heap()) {
                primary_key_set_in_heap_table = true;
                if (OB_FAIL(uk_or_heap_table_pk_add_to_index_list(index_node_position_list, i))) {
                } else if (OB_FALSE_IT(column.add_column_flag(HEAP_TABLE_PRIMARY_KEY_FLAG))) {
                } else if (OB_FALSE_IT(column.set_rowkey_position(0))) {
                } else { /*do nothing*/ }
              } else {
                int64_t pk_data_length = 0;
                if (get_primary_key_size() > 0) {
                  ret = OB_ERR_MULTIPLE_PRI_KEY;
                  SQL_RESV_LOG(WARN, "Multiple primary key defined");
                } else if (stat.is_set_null_) {
                  ret = OB_ERR_PRIMARY_CANT_HAVE_NULL;
                } else if (ob_is_string_tc(column.get_data_type()) && !column.is_string_lob()) {
                  int64_t length = 0;
                  if (OB_FAIL(column.get_byte_length(length, false))){
                  } else if (pk_data_length += length > OB_MAX_VARCHAR_LENGTH_KEY) {
                    ret = OB_ERR_TOO_LONG_KEY_LENGTH;
                    LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_VARCHAR_LENGTH_KEY);
                  } else if (length <= 0) {
                    ret = OB_ERR_WRONG_KEY_COLUMN;
                    LOG_USER_ERROR(OB_ERR_WRONG_KEY_COLUMN, column.get_column_name_str().length(), column.get_column_name());
                  } else {
                    // do nothing
                  }
                }
                if (OB_SUCC(ret)) {
                  if (OB_FAIL(primary_keys_.push_back(column.get_column_id()))) {
                  } else {
                    column.set_rowkey_position(get_primary_key_size());
                  }
                }
              }
            } else {
              column.set_rowkey_position(0);
            }
          }

          if (OB_SUCC(ret)) {
            // In MySQL mode, when column definitions are provided in a CTAS,
            // they are always complete (name,type,attri,...) and will ignore the deduced attributes from SELECT statement.
            if (OB_FAIL(cols_with_nullable_specified_.push_back(column.get_column_name_str()))) {
            }
          }

          if (OB_SUCC(ret)) {
            if (stat.is_unique_key_) {
              //consider column with unique_key as a special index node,
              //then resolve it in resolve_index_node()
              if (OB_FAIL(uk_or_heap_table_pk_add_to_index_list(index_node_position_list, i))) {
              }
            }
          }

          if (OB_SUCC(ret)) {
            if (stat.is_autoincrement_) {
              if (0 == autoinc_column_id) {
                autoinc_column_id = column.get_column_id();
              } else {
                ret = OB_ERR_WRONG_AUTO_KEY;
                LOG_USER_ERROR(OB_ERR_WRONG_AUTO_KEY);
                SQL_RESV_LOG(WARN, "only one auto-increment column permitted", K(ret));
              }
            }
          }

          if (OB_SUCC(ret)) {
            bool is_first_timestamp = false;
            if (ObTimestampType == column.get_data_type()) {
              if (-1 == first_timestamp_index) {
                is_first_timestamp = true;
                first_timestamp_index = column.get_column_id();
              }
              if (OB_FAIL(ObResolverUtils::resolve_timestamp_node(stat.is_set_null_, stat.is_set_default_value_,
                                                                  is_first_timestamp, session_info_, column))) {
              }
            }
          }

          if (OB_SUCC(ret)) {
            ObColumnSchemaV2 *tmp_col = NULL;
            LOG_DEBUG("resolve table elements mid2", K(i), K(column));
            if (OB_FAIL(table_schema.add_column(column))) {
            } else if (OB_ISNULL(tmp_col = table_schema.get_column_schema(column.get_column_id()))) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("get column schema failed", K(column));
            } else {
              ObColumnNameHashWrapper name_key(tmp_col->get_column_name_str());
              if (OB_FAIL(column_name_set_.set_refactored(name_key))) {
              } else {
                ret = OB_SUCCESS;
              }
            }
          }

          if (OB_SUCC(ret) && !column.is_invisible_column()) {
            has_visible_col = true;
          }

          if (OB_SUCC(ret)) {
            if (OB_FAIL(stats.push_back(stat))) {
            }
          }
        }
      } else if (T_PRIMARY_KEY == element->type_) {
        if (primary_key_set_in_heap_table) {
          ret = OB_ERR_PRIMARY_KEY_DUPLICATE;
          SQL_RESV_LOG(WARN, "multiple primary key defined");
        } else if (NULL == primary_node) {
          if (is_organization_set_to_heap()) {
            primary_node_in_heap_table = element;
            if (OB_FAIL(uk_or_heap_table_pk_add_to_index_list(index_node_position_list, i))) {
            }
          } else {
            primary_node = element;
          }
        } else {
          ret = OB_ERR_PRIMARY_KEY_DUPLICATE;
          SQL_RESV_LOG(WARN, "multiple primary key defined");
        }
      } else if(ObItemType::T_INDEX == element->type_) {
        if (OB_MAX_INDEX_PER_TABLE == index_node_position_list.count()) {
          ret = OB_ERR_TOO_MANY_KEYS;
          LOG_USER_ERROR(OB_ERR_TOO_MANY_KEYS, OB_MAX_INDEX_PER_TABLE);
        } else if (OB_FAIL(index_node_position_list.push_back(i))){
        } else { /*do nothing*/ }
      } else if (T_FOREIGN_KEY == element->type_) {
        // FIXME: foreign key maximum quantity limit is the same as index quantity
        if (OB_MAX_INDEX_PER_TABLE == foreign_key_node_position_list.count()) {
          ret = OB_ERR_TOO_MANY_KEYS;
          LOG_USER_ERROR(OB_ERR_TOO_MANY_KEYS, OB_MAX_INDEX_PER_TABLE);
        } else if (OB_FAIL(foreign_key_node_position_list.push_back(i))){
        } else { /*do nothing*/ }
      } else if (T_CHECK_CONSTRAINT == element->type_) {
        if (OB_FAIL(table_level_constraint_list.push_back(i))) {
        }
      } else if (T_EMPTY == element->type_) {
        // compatible with mysql 5.7 check (expr), do nothing
      } else {
        // won't be here
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "unexpected branch", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      for (int32_t i = resolved_cols_count; i < resolved_cols.count() && OB_SUCC(ret); i++) {
        ObColumnSchemaV2 &hidden_col = resolved_cols.at(i);
        if (OB_FAIL(table_schema.add_column(hidden_col))) {
        } else {
          ObColumnNameHashWrapper name_key(hidden_col.get_column_name_str());
          if (OB_FAIL(column_name_set_.set_refactored(name_key))) {
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      int64_t identity_column_count = 0;
      if (OB_FAIL(get_identity_column_count(table_schema, identity_column_count))) {
      } else if (identity_column_count > 1) {
        ret = OB_ERR_IDENTITY_COLUMN_COUNT_EXCE_LIMIT;
        SQL_RESV_LOG(WARN, "each table can only have an identity column", K(ret));
      }
    }
    // A table must have at least one column as a visible column
    if (OB_SUCC(ret)) {
      // RESOLVE_NON_COL == resolve_rule when, only parse non-column definitions
      if (RESOLVE_NON_COL != resolve_rule && !has_visible_col) {
        ret = OB_ERR_ONLY_HAVE_INVISIBLE_COL_IN_TABLE;
        SQL_RESV_LOG(WARN, "table must have at least one column that is not invisible", K(ret));
      }
    }

    // MySQL mode, a table must have at least one non-hidden column
    if (OB_SUCC(ret)) {
      bool has_non_hidden_column = false;
      for (int64_t i = 0;
           OB_SUCC(ret) && !has_non_hidden_column && i < table_schema.get_column_count();
           ++i) {
        const ObColumnSchemaV2 *column = table_schema.get_column_schema_by_idx(i);
        CK (OB_NOT_NULL(column));
        if (OB_SUCC(ret) && !column->is_hidden()) {
          has_non_hidden_column = true;
        }
      }
      if (OB_SUCC(ret) && !has_non_hidden_column) {
        ret = OB_ERR_AT_LEAST_ONE_COLUMN_NOT_VIRTUAL;
        SQL_RESV_LOG(WARN, "table must have at least one column that is not hidden", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(get_primary_key_size() > 0 && NULL != primary_node)) {
        ret = OB_ERR_PRIMARY_KEY_DUPLICATE;
        SQL_RESV_LOG(WARN, "multiple primary key defined");
      } else if (NULL == primary_node) {
        // do nothing
      } else if (OB_FAIL(resolve_primary_key_node(*primary_node, stats))) {
      }
      if (OB_SUCC(ret)) {
        table_schema.set_max_used_column_id(cur_column_id_);
        if (0 != autoinc_column_id) {
          table_schema.set_autoinc_column_id(autoinc_column_id);
        }
      }
    }

    if (OB_SUCC(ret)) {
      // todo@lanyi add related case
      if (OB_UNLIKELY(is_organization_set_to_heap() && primary_key_set_in_heap_table && NULL != primary_node_in_heap_table)) {
        ret = OB_ERR_PRIMARY_KEY_DUPLICATE;
        SQL_RESV_LOG(WARN, "multiple primary key defined");
      } else if (NULL == primary_node_in_heap_table) {
        // do nothing
      // todo@lanyi This function should be placed in a separate document
      } else if (OB_FAIL(resolve_primary_key_node_in_heap_table(primary_node_in_heap_table, stats, resolved_cols))) {
      }
    }
    LOG_DEBUG("resolve table elements end ", K(resolve_rule), K(table_schema));
  }
  return ret;
}

int ObCreateTableResolver::set_nullable_for_cta_column(ObSelectStmt *select_stmt,
                                                       ObColumnSchemaV2& column,
                                                       const ObRawExpr *expr,
                                                       const ObString &table_name,
                                                       ObIAllocator &allocator,
                                                       ObStmt *stmt)
{
  int ret = OB_SUCCESS;
  bool is_not_null = false;
  if (OB_ISNULL(expr) || OB_ISNULL(select_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null of expr and select stmt.", K(ret));
  } else {
    // scope set to FROM since it will not go deduce process with context,
    // such as null reject in where condition and having condition.
    // if is_not_null true, it will pass into full scope checking at next step.
    ObNotNullContext ctx(NULL, NULL, select_stmt);
    if (OB_FAIL(ctx.generate_stmt_context(NULLABLE_SCOPE::NS_FROM))) {
    } else if (OB_FAIL(ObTransformUtils::is_expr_not_null(ctx,
                                                          const_cast<ObRawExpr *>(expr),
                                                          is_not_null,
                                                          NULL))) {
    }
  }
  if (OB_SUCC(ret) && is_not_null) {
    // deduce pre-condition: already not null
    // oracle:
    // 1. only deduce column not null, not for composed exprs(e.g, c1+c2) and const expr
    // 2. column not null depends on HAS_NOT_NULL_VALIDATE_CONSTRAINT_FLAG, e.g, ctas from pk (NULL: YES)
    // mysql:
    // 1. supports composed expr not null deduce
    // 2. column not null depends on NOT_NULL_FLAG, e.g, ctas from pk (NULL: NO)
    ObNotNullContext ctx(NULL, NULL, select_stmt, false);
    if (OB_FAIL(ctx.generate_stmt_context(NULLABLE_SCOPE::NS_TOP))) {
    } else if (OB_FAIL(ObTransformUtils::is_expr_not_null(ctx,
                                                          const_cast<ObRawExpr *>(expr),
                                                          is_not_null,
                                                          NULL))) {
    }
  }
  LOG_DEBUG("set nullable_for_cta_column", K(is_not_null), K(column));
  if (OB_SUCC(ret)) {
    if (expr->is_win_func_expr()) {//compatible with mysql
      const ObWinFunRawExpr *win_expr = reinterpret_cast<const ObWinFunRawExpr*>(expr);
      if (T_WIN_FUN_RANK == win_expr->get_func_type() ||
          T_WIN_FUN_DENSE_RANK == win_expr->get_func_type() ||
          T_WIN_FUN_ROW_NUMBER == win_expr->get_func_type()) {
        ObObj temp_default;
        temp_default.set_uint64(0);
        column.set_cur_default_value(temp_default, false);
      } else if (T_WIN_FUN_CUME_DIST == win_expr->get_func_type() ||
                  T_WIN_FUN_PERCENT_RANK == win_expr->get_func_type()) {
        ObObj temp_default;
        temp_default.set_double(0);
        column.set_cur_default_value(temp_default, false);
      } else {}
    } else {}
    column.set_nullable(!is_not_null);
  }
  return ret;
}
int ObCreateTableResolver::resolve_insert_mode(const ParseNode *parse_tree)
{
  int ret = OB_SUCCESS;
  ParseNode *flag_node = NULL;
  ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt *>(stmt_);
  ObExecContext *exec_ctx = NULL;
  ObSqlCtx *sql_ctx = NULL;
  if (OB_ISNULL(parse_tree) ||
      OB_ISNULL(create_table_stmt) ||
      OB_ISNULL(params_.query_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error",K(parse_tree), K(create_table_stmt), K(session_info_), K(exec_ctx), K(sql_ctx), K(ret));
  } else if (parse_tree->num_child_ != CREATE_TABLE_AS_SEL_NUM_CHILD){
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected child_num",K(parse_tree->num_child_), K(ret));
  } else {
    flag_node = parse_tree->children_[10];
    if (flag_node == NULL) {
      create_table_stmt->set_insert_mode(0);
    } else if (flag_node->type_ == T_IGNORE) {
      create_table_stmt->set_insert_mode(1);
    } else if (flag_node->type_ == T_REPLACE) {
      create_table_stmt->set_insert_mode(2);
    }
  }
  return ret;
}
// Parse column_list and query, then set the column names and data types of the new table based on opt_column_list (which may be absent) and the query
int ObCreateTableResolver::resolve_table_elements_from_select(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt *>(stmt_);
  const ObTableSchema *base_table_schema = NULL;
  ParseNode *sub_sel_node = parse_tree.children_[8];
  ObSelectStmt *select_stmt = NULL;
  ObSelectResolver select_resolver(params_);
  select_resolver.params_.is_from_create_table_ = true;
  select_resolver.params_.is_specified_col_name_ = parse_tree.num_child_ > 3 &&
                                                   parse_tree.children_[3] != NULL &&
                                                   T_TABLE_ELEMENT_LIST == parse_tree.children_[3]->type_;
  // select layer should not see the insert stmt's attributes from the upper layer, so the upper scope stmt should be empty
  select_resolver.set_parent_namespace_resolver(NULL);
  if (OB_NOT_NULL(params_.query_ctx_)
        && 0 != params_.query_ctx_->question_marks_count_
        && !params_.is_prepare_protocol_) {
    ret = OB_ERR_PARSER_SYNTAX;
    LOG_WARN("not support questionmark in normal create.", K(ret));
  } else if (OB_UNLIKELY(parse_tree.num_child_ <= 3 ||
                         (parse_tree.children_[3] != NULL &&
                          T_TABLE_ELEMENT_LIST != parse_tree.children_[3]->type_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument.", K(ret));
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_) || OB_ISNULL(params_.param_list_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "ObCreateTableResolver is not init", K(params_.param_list_), K(allocator_),
                                                            K(session_info_), K(ret));
  } else if (OB_ISNULL(sub_sel_node) || OB_UNLIKELY(T_SELECT != sub_sel_node->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid select node", K(sub_sel_node));
  } else if (OB_FAIL(set_table_name(table_name_))) {
  } else if (OB_FAIL(select_resolver.resolve(*sub_sel_node))) {
  } else {
    select_stmt = select_resolver.get_select_stmt();
    ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
    
    LOG_DEBUG("resolve table select item begin", K(table_schema));
    if (OB_ISNULL(select_stmt)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid select stmt", K(select_stmt));
    } else if (OB_FAIL(params_.query_ctx_->query_hint_.init_query_hint(allocator_,
                                                                       session_info_,
                                                                       select_stmt))) {
    } else {
      ObIArray<SelectItem> &select_items = select_stmt->get_select_items();
      ObColumnSchemaV2 column;
      create_table_stmt->set_sub_select(select_stmt);
      const int64_t create_table_column_count = table_schema.get_column_count();
      // Check for duplicate names among query items, if any, report an error; query item names can be the same as column names in the table definition;
      for (int64_t i = 0; OB_SUCC(ret) && i < select_items.count(); ++i) {
        const SelectItem &cur_item = select_items.at(i);
        const ObString *cur_name = NULL;
        if (!cur_item.alias_name_.empty()) {
            cur_name = &cur_item.alias_name_;
        } else {
            cur_name = &cur_item.expr_name_;
        }
        if (cur_name->length() > OB_MAX_COLUMN_NAME_LENGTH) {
          ret = OB_ERR_TOO_LONG_IDENT;
          LOG_USER_ERROR(OB_ERR_TOO_LONG_IDENT, cur_name->length(), cur_name->ptr());
        }
        for (int64_t j = 0; OB_SUCC(ret) && j < i; ++j) {
          const SelectItem &pre_item = select_items.at(j);
          const ObString *prev_name = NULL;
          if (!pre_item.alias_name_.empty()) {
            prev_name = &pre_item.alias_name_;
          } else {
            prev_name = &pre_item.expr_name_;
          }
          if (ObCharset::case_compat_mode_equal(*prev_name, *cur_name)) {
            ret = OB_ERR_COLUMN_DUPLICATE;
            LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, cur_name->length(), cur_name->ptr());
          }
        }
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < select_items.count(); ++i) {
        const SelectItem &select_item = select_items.at(i);
        ObRawExpr *expr = select_item.expr_;
        ObColumnRefRawExpr *new_col_ref = static_cast<ObColumnRefRawExpr *>(expr);
        TableItem *new_table_item = select_stmt->get_table_item_by_id(new_col_ref->get_table_id());
        if (OB_UNLIKELY(NULL == expr)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("select item expr is null", K(ret), K(i));
        } else {
          column.reset();
          if (!select_item.alias_name_.empty()) {
            OZ(column.set_column_name(select_item.alias_name_));
          } else {
            OZ(column.set_column_name(select_item.expr_name_));
          }
          if (OB_SUCC(ret)) {
            if (GCONF._enable_pseudo_partition_id &&
                ObResolverUtils::is_pseudo_partition_column_name(column.get_column_name_str())) {
              ret = OB_ERR_COLUMN_DUPLICATE;
              LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, column.get_column_name_str().length(),
                column.get_column_name_str().ptr());
              LOG_WARN("cannot create table from select stmt, duplicate partition pseudo column",
                K(ret), K(column.get_column_name_str()));
            }
          }
          if (OB_SUCC(ret)) {
            if (new_table_item != NULL && new_table_item->is_basic_table()) {
              if (base_table_schema == NULL &&
                  OB_FAIL(schema_checker_->get_table_schema(
                                                            new_table_item->ref_id_, base_table_schema))) {
                LOG_WARN("get table schema failed", K(ret));
              } else if (OB_ISNULL(base_table_schema)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("NULL table schema", K(ret));
              } else {
                const ObColumnSchemaV2 *org_column = base_table_schema->get_column_schema(select_item.expr_name_);
                if (NULL != org_column &&
                    !org_column->is_generated_column() &&
                    !org_column->get_cur_default_value().is_null()) {
                  column.set_cur_default_value(
                      org_column->get_cur_default_value(),
                      org_column->is_default_expr_v2_column());
                  }
              }
            } else if (new_table_item == NULL &&
                       (ObRawExpr::EXPR_CONST == expr->get_expr_class() ||
                        (ObRawExpr::EXPR_OPERATOR == expr->get_expr_class() &&
                         expr->is_static_const_expr())) &&
                        !expr->get_result_type().is_null()) {
              common::ObObjType result_type = expr->get_result_type().get_obj_meta().get_type();
              if (ob_is_numeric_type(result_type) || ob_is_string_tc(result_type) || ob_is_time_tc(result_type)) {
                common::ObObj zero_obj(0);
                if (OB_FAIL(column.set_cur_default_value(zero_obj, false))) {
                }
              }
            } else { /*do nothing*/ }
          }
          if (OB_SUCC(ret) && ObResolverUtils::is_restore_user(*session_info_)
              && ObCharset::case_insensitive_equal(column.get_column_name_str(), OB_HIDDEN_PK_INCREMENT_COLUMN_NAME)) {
            continue;
          }
          if (OB_FAIL(ret)) {
            // do nothing
          } else if (expr->get_result_type().is_null()) { //bug16503918, NULL needs to be replaced with binary(0)
            const ObAccuracy binary_accuracy(0);
            ObObjMeta binary_meta;
            binary_meta.set_binary();
            column.set_meta_type(binary_meta);
            column.set_accuracy(binary_accuracy);
          } else {
            ObObjMeta column_meta = expr->get_result_type().get_obj_meta();
            column.set_meta_type(column_meta);
            ObCharsetType char_type = table_schema.get_charset_type();
            ObCollationType collation_type = expr->get_collation_type();
            column.set_charset_type(char_type);
            column.set_collation_type(collation_type);
            column.set_accuracy(expr->get_accuracy());
            column.set_zero_fill(expr->get_result_flag() & ZEROFILL_FLAG);
            OZ (adjust_number_decimal_column_accuracy_within_max(column));
            if (OB_SUCC(ret) && (column.is_enum_or_set() || column.is_collection())) {
              if (OB_FAIL(fill_column_with_subschema(*expr, *session_info_, column))) {
              }
            }
            if (OB_SUCC(ret) && ob_is_geometry(expr->get_result_type().get_type())) {
              column.set_geo_type(static_cast<uint64_t>(expr->get_geo_expr_result_type()));
            }
            OZ (adjust_string_column_length_within_max(column));
            LOG_DEBUG("column expr debug", K(*expr));
          }
          if (OB_FAIL(ret)) { // do nothing.
          //create_table_column_count > 0  means the format of ctas is "create table t(c1, c2,...) as select"
          // During the first step of resolving ctas, column schemas of (c1, c2, ...) are
          // generated and added into table_schema.
          } else {
            LOG_DEBUG("ctas mysql mode, create_table_column_count = 0,begin", K(create_table_column_count), K(column));
            column.set_column_id(gen_column_id());
            ObColumnSchemaV2 *org_column = table_schema.get_column_schema(column.get_column_name());
            if (OB_NOT_NULL(org_column)) {
              bool need_set_nullable = !ObOptimizerUtil::find_item(cols_with_nullable_specified_,
                                                                   org_column->get_column_name_str());
              // Same column name exists, to keep consistent with mysql, the order of the original column needs to be adjusted
              ObColumnSchemaV2 new_column;
              if (OB_FAIL(new_column.assign(*org_column))) {
              } else {
                new_column.set_column_id(gen_column_id());
                new_column.set_prev_column_id(UINT64_MAX);
                new_column.set_next_column_id(UINT64_MAX);
              }
              if (OB_FAIL(ret)) {
              } else if (1 == table_schema.get_column_count()) {
                //do nothing, only one column so no adjustment needed
                if (need_set_nullable && OB_FAIL(set_nullable_for_cta_column(select_stmt,
                                                                             *org_column,
                                                                             expr,
                                                                             table_name_,
                                                                             *allocator_,
                                                                             stmt_))) {
                  LOG_WARN("failed to check and set nullable for cta.", K(ret));
                }
              } else if (need_set_nullable && OB_FAIL(set_nullable_for_cta_column(select_stmt,
                                                                                  new_column,
                                                                                  expr,
                                                                                  table_name_,
                                                                                  *allocator_,
                                                                                  stmt_))) {
                LOG_WARN("failed to check and set nullable for cta.", K(ret));
              } else if (OB_FAIL(table_schema.delete_column(org_column->get_column_name_str()))) {
              } else if (OB_FAIL(table_schema.add_column(new_column))) {
              } else {
                LOG_DEBUG("reorder column successfully", K(new_column));
              }
            } else {
              if (OB_FAIL(set_nullable_for_cta_column(select_stmt, column, expr, table_name_, *allocator_, stmt_))) {
              } else if (column.is_string_type() || column.is_json() || column.is_geometry()) {
                if (column.is_geometry() && T_REF_COLUMN == select_item.expr_->get_expr_type()) {
                  column.set_srs_id((static_cast<ObColumnRefRawExpr*>(select_item.expr_))->get_srs_id());
                } else if (ObHexStringType == column.get_data_type()) {
                  column.set_data_type(ObVarcharType);
                }
                if (column.get_meta_type().is_lob() || column.get_meta_type().is_json()
                    || column.get_meta_type().is_geometry()) {
                  if (OB_FAIL(check_text_column_length_and_promote(column, table_id_, true))) {
                  }
                } else if (OB_FAIL(check_string_column_length(column, params_.is_prepare_stage_))) {
                }
              }
              if (OB_FAIL(ret)) {
                //do nothing ...
              } else if (OB_FAIL(table_schema.add_column(column))) {
              } else {
                ObColumnNameHashWrapper name_key(column.get_column_name_str());
                if (OB_FAIL(column_name_set_.set_refactored(name_key))) {
                }
              }
            }
            LOG_DEBUG("ctas mysql mode, create_table_column_count = 0,end", K(column));
          }
        }
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::add_sort_column(const ObColumnSortItem &sort_column)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt_) || OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "variables are not inited", K(ret), KP(stmt_));
  } else {
    ObColumnSchemaV2 *column_schema = NULL;
    ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
    ObTableSchema &tbl_schema = create_table_stmt->get_create_table_arg().schema_;
    share::schema::ObColumnNameWrapper column_key(sort_column.column_name_, sort_column.prefix_len_);
    bool check_prefix_len = false;
    if (NULL == (column_schema = tbl_schema.get_column_schema(sort_column.column_name_))) {
      ret = OB_ERR_BAD_FIELD_ERROR;
      LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR,
          sort_column.column_name_.length(), sort_column.column_name_.ptr(),
          table_name_.length(), table_name_.ptr());
    } else if (is_column_exists(sort_column_array_, column_key, check_prefix_len)) {
      ret = OB_ERR_COLUMN_DUPLICATE;
      LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, sort_column.column_name_.length(), sort_column.column_name_.ptr());
    } else if (OB_FAIL(check_prefix_key(sort_column.prefix_len_, *column_schema))) {
    } else if (OB_FAIL(sort_column_array_.push_back(column_key))) {
    } else if (OB_FAIL(index_arg_.index_columns_.push_back(sort_column))) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "add sort column to index arg failed", K(ret));
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObCreateTableResolver::get_table_schema_for_check(const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
  table_schema = &(create_table_stmt->get_create_table_arg().schema_);
  return ret;
}

int ObCreateTableResolver::generate_index_arg(const bool process_heap_table_primary_key)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(stmt_) || OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "variables are not inited.", K(ret), KP(stmt_));
  } else if (OB_FAIL(set_index_name())) {
  } else if (OB_FAIL(set_index_option_to_arg())) {
  } else if(OB_FAIL(set_storing_column())) {
  } else {
    ObIndexType type = INDEX_TYPE_IS_NOT;
    //index default is global, if not specified, but oracle temporary table is internally converted, can only be local
    if (NOT_SPECIFIED == index_scope_) {
      // MySQL default index mode is local,
      // and Oracle default index mode is global
      global_ = false;
    } else {
      global_ = (GLOBAL_INDEX == index_scope_);
    }
    ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
    ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
    if (OB_SUCC(ret)) {
      if (UNIQUE_KEY == index_keyname_) {
        if (process_heap_table_primary_key) {
          type = INDEX_TYPE_HEAP_ORGANIZED_TABLE_PRIMARY;
        } else if (global_) {
          type = INDEX_TYPE_UNIQUE_GLOBAL;
        } else {
          type = INDEX_TYPE_UNIQUE_LOCAL;
        }
      } else if (NORMAL_KEY == index_keyname_) {
        if (global_) {
          type = INDEX_TYPE_NORMAL_GLOBAL;
        } else {
          type = INDEX_TYPE_NORMAL_LOCAL;
        }
      } else if (SPATIAL_KEY == index_keyname_) {
        if (global_) {
          type = INDEX_TYPE_SPATIAL_GLOBAL;
        } else {
          type = INDEX_TYPE_SPATIAL_LOCAL;
        }
      } else if (VEC_KEY == index_keyname_) {
        if (global_) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("not support global vec index now", K(ret));
        }
        if (OB_SUCC(ret)) {
          type = INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL; // Need to consider ivf, hnsw, spiv these three modes, where ivf index is divided into ivfflat, ivfsq8, ivfpq three categories
        }
      } else if (FTS_KEY == index_keyname_) {
        if (global_) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("not support global fts index now", K(ret));
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "global fulltext index is");
        } else {
          // set type to fts_doc_rowkey first, append other fts arg later
          type = INDEX_TYPE_FTS_INDEX_LOCAL;
        }
      } else if (MULTI_KEY == index_keyname_) {
        if (global_) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("not support global fts index now", K(ret));
        } else {
          type = INDEX_TYPE_NORMAL_MULTIVALUE_LOCAL;
        }
      } else if (MULTI_UNIQUE_KEY == index_keyname_) {
        if (global_) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("not support global multivalue index now", K(ret));
        } else {
          type = INDEX_TYPE_UNIQUE_MULTIVALUE_LOCAL;
        }
      }
    }
    if(OB_SUCC(ret)) {
      index_arg_.index_type_ = type;
      //create table with index .the status of index is available
      index_arg_.index_option_.index_status_ = INDEX_STATUS_AVAILABLE;
      index_arg_.index_option_.index_attributes_set_ = index_attributes_set_;
      index_arg_.sql_mode_ = session_info_->get_sql_mode();
      index_arg_.is_index_scope_specified_ = !(NOT_SPECIFIED == index_scope_);
    }
    if (OB_FAIL(ret)) {
      // skip
    } else if (INDEX_TYPE_UNIQUE_GLOBAL == type
               || INDEX_TYPE_NORMAL_GLOBAL == type) {
      ObArray<ObColumnSchemaV2 *> gen_columns;
      ObTableSchema &index_schema = index_arg_.index_schema_;
      index_schema.set_table_type(USER_INDEX);
      index_schema.set_index_type(index_arg_.index_type_);
      
      bool check_data_schema = false;
      if (OB_FAIL(share::ObIndexBuilderUtil::adjust_expr_index_args(
              index_arg_, table_schema, *allocator_, gen_columns))) {
      } else if (OB_FAIL(share::ObIndexBuilderUtil::set_index_table_columns(
              index_arg_, table_schema, index_schema, check_data_schema))) {
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::set_index_name()
{
  int ret = OB_SUCCESS;
  ObIndexNameHashWrapper key(index_name_);
  if (OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "allocator is null.", K(ret));
  } else if (OB_FAIL(current_index_name_set_.set_refactored(key))) {
  } else if (OB_FAIL(ob_write_string(*allocator_, index_name_, index_arg_.index_name_))) {
  } else {
    index_arg_.index_schema_.set_name_generated_type(name_generated_type_);
  }

  return ret;
}

int ObCreateTableResolver::set_index_option_to_arg()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "allocator is null.", K(ret));
  } else {
    index_arg_.index_option_.block_size_ = block_size_;
    if (OB_FAIL(ob_write_string(*allocator_, compress_method_,
                                index_arg_.index_option_.compress_method_))) {
    } else if (OB_FAIL(ob_write_string(*allocator_, comment_,
                                       index_arg_.index_option_.comment_))) {
    } else if (OB_FAIL(ob_write_string(*allocator_, storage_cache_policy_,
                   index_arg_.index_option_.storage_cache_policy_))) {
    } else {
      index_arg_.index_option_.parser_name_ = parser_name_;
      index_arg_.index_option_.parser_properties_ = parser_properties_;
      index_arg_.index_option_.row_store_type_  = row_store_type_;
      index_arg_.index_option_.store_format_ = store_format_;
      index_arg_.with_rowid_ = with_rowid_;
    }
  }

  return ret;
}

int ObCreateTableResolver::set_storing_column()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < store_column_names_.count(); ++i) {
    ret = index_arg_.store_columns_.push_back(store_column_names_.at(i));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < hidden_store_column_names_.count(); ++i) {
    ret = index_arg_.hidden_store_columns_.push_back(hidden_store_column_names_.at(i));
  }
  return ret;
}

int ObCreateTableResolver::resolve_table_level_constraint_for_mysql(
    const ParseNode* node, ObArray<int>& constraint_position_list)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    // do nothing, create table t as select ... will come here
  } else if (T_TABLE_ELEMENT_LIST != node->type_) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(node->type_));
  } else if (OB_ISNULL(stmt_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "stmt_ is null.", K(ret));
  } else if (OB_ISNULL(node->children_)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(node->children_));
  } else {
    ObCreateTableStmt* create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
    ObSEArray<ObConstraint, 4>& csts = create_table_stmt->get_create_table_arg().constraint_list_;
    for (int64_t i = 0; OB_SUCC(ret) && i < constraint_position_list.size(); ++i) {
      if (OB_UNLIKELY(constraint_position_list.at(i) >= node->num_child_)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(constraint_position_list.at(i)));
      } else if (OB_FAIL(resolve_check_constraint_node(*node->children_[constraint_position_list.at(i)], csts))) {
      } else { /*do nothing*/
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::resolve_index(
    const ParseNode *node,
    ObArray<int> &index_node_position_list)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(node)) {
    // do nothing, create table t as select ... will come here
  } else if (T_TABLE_ELEMENT_LIST != node->type_) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(node->type_));
  } else if (OB_ISNULL(stmt_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "stmt_ is null.", K(ret));
  } else if (OB_ISNULL(node->children_)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(node->children_));
  } else {
    vec_index_col_ids_.reset();
    has_vec_index_ = false;
    has_fts_index_ = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < index_node_position_list.size(); ++i) {
      reset();
      index_attributes_set_ = OB_DEFAULT_INDEX_ATTRIBUTES_SET;
      index_arg_.reset();
      if (OB_UNLIKELY(index_node_position_list.at(i) >= node->num_child_)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(index_node_position_list.at(i)));
      } else if (OB_FAIL(resolve_index_node(node->children_[index_node_position_list.at(i)]))) {
      } else { /*do nothing*/ }
    }
    if (OB_SUCC(ret) && (has_fts_index_ || has_multivalue_index_ || has_vec_index_)) {
      if (OB_FAIL(check_building_domain_index_legal())) {
      }
    }
    current_index_name_set_.reset();
  }

  return ret;
}

int ObCreateTableResolver::resolve_index_node(const ParseNode *node)
{
  int ret = OB_SUCCESS;
  ObString uk_name;
  bool is_index_part_specified = false;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "node is null.", K(ret));
  } else if ((ObItemType::T_INDEX != node->type_ && ObItemType::T_COLUMN_DEFINITION != node->type_)
          && (is_organization_set_to_heap() && ObItemType::T_PRIMARY_KEY != node->type_)) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid arguments.", K(ret), K(node->type_), K(node->num_child_));
  } else if (OB_ISNULL(stmt_) || OB_ISNULL(session_info_) || OB_ISNULL(schema_checker_)){
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "stmt or session_info or schema_checker is null.",
                 K(ret), KP(session_info_), KP(stmt_), K_(schema_checker));
  } else if (OB_ISNULL(node->children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "node->children_ is null.", K(ret));
  } else {
    index_arg_.reset();
    ObColumnSortItem sort_item;
    ObString first_column_name;
    ObColumnSchemaV2 *column_schema = NULL;
    ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
    ObTableSchema &tbl_schema = create_table_stmt->get_create_table_arg().schema_;
    int64_t vec_index_col_id = 0;
    bool process_heap_table_primary_key = false;
    if(ObItemType::T_INDEX == node->type_) {
      //if index_name is not specified, new index_name will be generated
      //by the first_column_name, so resolve the index_column_list_node firstly.
      if (NULL == node->children_[1] || T_INDEX_COLUMN_LIST != node->children_[1]->type_) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid index column list.", K(ret));
      } else {
        int64_t index_data_length = 0;
        index_keyname_ = static_cast<INDEX_KEYNAME>(node->value_);
        ParseNode *index_column_list_node = node->children_[1];
        if (index_column_list_node->num_child_ > OB_USER_MAX_ROWKEY_COLUMN_NUMBER) {
          ret = OB_ERR_TOO_MANY_ROWKEY_COLUMNS;
          LOG_USER_ERROR(OB_ERR_TOO_MANY_ROWKEY_COLUMNS, OB_USER_MAX_ROWKEY_COLUMN_NUMBER);
        } else if (OB_ISNULL(index_column_list_node->children_)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "children_ is null.", K(ret));
        } else { /*do nothing*/ }

        ParseNode *index_column_node = NULL;
        bool is_ctxcat_added = false;
        bool cnt_func_index_mysql = false;
        bool is_multi_value_index = false;
        const bool is_vec_index = (index_keyname_ == INDEX_KEYNAME::VEC_KEY);
        const bool is_fts_index = (index_keyname_ == INDEX_KEYNAME::FTS_KEY);
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(session_info_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null", K(ret));
        } else if (is_vec_index && index_column_list_node->num_child_ >= 2) {
          ret = OB_NOT_SUPPORTED;
          LOG_WARN("multi column of vector index is not support yet", K(ret), K(index_column_list_node->num_child_));
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "multi vector index column is");
        }
        for (int32_t i = 0; OB_SUCC(ret) && i < index_column_list_node->num_child_; ++i) {
          ObString &column_name = sort_item.column_name_;
          if (NULL == index_column_list_node->children_[i]
              || T_SORT_COLUMN_KEY != index_column_list_node->children_[i]->type_) {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "invalid index_column_list_node.", K(ret));
          } else {
            index_column_node = index_column_list_node->children_[i];
          }
          if (OB_SUCC(ret)) {
            if (OB_ISNULL(index_column_node->children_)
                || index_column_node->num_child_ < 3
                || OB_ISNULL(index_column_node->children_[0])) {
              ret = OB_ERR_UNEXPECTED;
              SQL_RESV_LOG(WARN, "invalid index_column_node.", K(ret),
                          K(index_column_node->num_child_),
                          K(index_column_node->children_),
                          K(index_column_node->children_[0]));
            } else {
              //column_name
              if (index_column_node->children_[0]->type_ != T_IDENT) {
                sort_item.is_func_index_ = true;
                cnt_func_index_mysql = true;
              } else {
                sort_item.is_func_index_ = false;
              }
              column_name.assign_ptr(
                  const_cast<char *>(index_column_node->children_[0]->str_value_),
                  static_cast<int32_t>(index_column_node->children_[0]->str_len_));
              if (OB_FAIL(ObMulValueIndexBuilderUtil::adjust_index_type(column_name,
                                                                        is_multi_value_index,
                                                                        reinterpret_cast<int*>(&index_keyname_)))) {
              } else if (NULL != index_column_node->children_[1]) {
                sort_item.prefix_len_ = static_cast<int32_t>(index_column_node->children_[1]->value_);
                if (0 == sort_item.prefix_len_) {
                  ret = OB_KEY_PART_0;
                  LOG_USER_ERROR(OB_KEY_PART_0, column_name.length(), column_name.ptr());
                }
              } else {
                sort_item.prefix_len_ = 0;
              }
            }
          }
          if (OB_SUCC(ret)) {
            if (sort_item.is_func_index_) {
              ObRawExpr *expr = NULL;
              if (is_multi_value_index) {
                ObColumnSchemaV2 *budy_column_schema = NULL;
                bool force_rebuild = true;
                if (OB_FAIL(ObMulValueIndexBuilderUtil::build_and_generate_multivalue_column(
                                                                                  *allocator_,
                                                                                  sort_item,
                                                                                  *params_.expr_factory_,
                                                                                  *session_info_,
                                                                                  tbl_schema,
                                                                                  schema_checker_,
                                                                                  force_rebuild,
                                                                                  column_schema,
                                                                                  budy_column_schema))) {
                } else if (OB_ISNULL(column_schema) || OB_ISNULL(budy_column_schema)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("multivalue index generate column, or budy column is null.",
                    K(ret), KP(column_schema), KP(budy_column_schema));
                } else {
                  ObColumnNameHashWrapper column_name_key(column_schema->get_column_name_str());
                  if (OB_FAIL(column_name_set_.set_refactored(column_name_key))) {
                  } else {
                    ObColumnSortItem budy_sort_item;
                    budy_sort_item.is_func_index_ = true;
                    budy_sort_item.column_name_ = budy_column_schema->get_column_name_str();

                    ObColumnNameHashWrapper budy_column_name_key(budy_column_schema->get_column_name_str());
                    if (OB_FAIL(column_name_set_.set_refactored(budy_column_name_key))) {
                    } else if (OB_FAIL(add_sort_column(budy_sort_item))) {
                    }
                  }
                }
              } else if (OB_FAIL(ObRawExprUtils::build_generated_column_expr(NULL,
                                                                      column_name,
                                                                      *params_.expr_factory_,
                                                                      *session_info_,
                                                                      tbl_schema,
                                                                      expr,
                                                                      schema_checker_,
                                                                      ObResolverUtils::CHECK_FOR_FUNCTION_INDEX))) {
              } else if (!expr->is_column_ref_expr()) {
                //real index expr, so generate hidden generated column in data table schema
                if (ob_is_geometry(expr->get_data_type()) || static_cast<int64_t>(INDEX_KEYNAME::SPATIAL_KEY) == node->value_) {
                  ret = OB_ERR_SPATIAL_FUNCTIONAL_INDEX;
                  LOG_WARN("Spatial functional index is not supported.", K(ret), K(column_name));
                } else if (ob_is_collection_sql_type(expr->get_data_type()) || static_cast<int64_t>(INDEX_KEYNAME::VEC_KEY) == node->value_) {
                  ret = OB_ERR_FUNCTIONAL_INDEX_ON_FIELD;
                  LOG_WARN("Functional index for vector index is not supported.", K(ret), K(column_name));
                } else if (OB_FAIL(ObIndexBuilderUtil::generate_ordinary_generated_column(*expr,
                                                                                   *session_info_,
                                                                                   tbl_schema,
                                                                                   column_schema,
                                                                                   schema_checker_->get_schema_guard()))) {
                } else {
                  ObColumnNameHashWrapper column_name_key(column_schema->get_column_name_str());
                  sort_item.column_name_ = column_schema->get_column_name_str();
                  sort_item.is_func_index_ = false;
                  if (OB_FAIL(column_name_set_.set_refactored(column_name_key))) {
                  }
                }
              } else {
                ret = OB_ERR_FUNCTIONAL_INDEX_ON_FIELD;
                LOG_WARN("Functional index on a column is not supported.", K(ret), K(*expr));
              }
            } else {
              if (NULL == (column_schema = tbl_schema.get_column_schema(column_name))) {
                ret = OB_ERR_KEY_COLUMN_DOES_NOT_EXITS;
                LOG_USER_ERROR(OB_ERR_KEY_COLUMN_DOES_NOT_EXITS, column_name.length(), column_name.ptr());
              }
            }
            if (OB_FAIL(ret)) {
            } else if (is_vec_index) {
              vec_index_col_id = column_schema->get_column_id();
              if (ObVectorIndexUtil::has_multi_index_on_same_column(vec_index_col_ids_, vec_index_col_id)) {
                ret = OB_NOT_SUPPORTED;
                LOG_WARN("more than one vector index on same column is not supported", K(ret), K(vec_index_col_id), K(vec_index_col_ids_));
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "more than one vector index on same column is");
              } else if (OB_FAIL(set_vec_column_name(column_schema->get_column_name()))) {
              }
            }
            if (OB_SUCC(ret)) {
              if (OB_ISNULL(session_info_)) {
                ret = OB_NOT_INIT;
                LOG_WARN("session_info_ is null");
              }  else if (sort_item.prefix_len_ > column_schema->get_data_length()) {
                ret = OB_WRONG_SUB_KEY;
                SQL_RESV_LOG(WARN, "prefix length is longer than column length", K(sort_item), K(column_schema->get_data_length()), K(ret));
              } else if (ob_is_collection_sql_type(column_schema->get_data_type())
                  && static_cast<int64_t>(INDEX_KEYNAME::VEC_KEY) != node->value_) {
                ret = OB_NOT_SUPPORTED;
                LOG_WARN("index column is vector column, but is not vector index is not supported", K(ret));
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "vector column index but not vector index is");
              } else if (column_schema->is_key_forbid_lob() && static_cast<int64_t>(INDEX_KEYNAME::FTS_KEY) != node->value_ && static_cast<int64_t>(INDEX_KEYNAME::VEC_KEY) != node->value_) {
                if (column_schema->is_hidden()) {
                  //functional index in mysql mode
                  ret = OB_ERR_FUNCTIONAL_INDEX_ON_LOB;
                  LOG_WARN("Cannot create a functional index on an expression that returns a BLOB or TEXT.", K(ret));
                } else if(sort_item.prefix_len_ <= 0) {
                  ret = OB_ERR_WRONG_KEY_COLUMN;
                  LOG_USER_ERROR(OB_ERR_WRONG_KEY_COLUMN, column_name.length(), column_name.ptr());
                }
              } else if (OB_FAIL(resolve_spatial_index_constraint(*column_schema,
                  index_column_list_node->num_child_, node->value_, false/*false*/,
                  NULL != index_column_node->children_[2] && 1 != index_column_node->children_[2]->is_empty_))) {
              } else if (OB_FAIL(resolve_vec_index_constraint(*column_schema,
                                                              node->value_,
                                                              node->children_[2]))) {
              } else if (OB_FAIL(resolve_fts_index_constraint(*column_schema,
                                                              node->value_))) {
              } else if (OB_FAIL(resolve_multivalue_index_constraint(*column_schema, index_keyname_))) {
              }

              if (OB_SUCC(ret) && ob_is_string_type(column_schema->get_data_type()) && !column_schema->is_string_lob()) {
                int64_t length = 0;
                if (OB_FAIL(column_schema->get_byte_length(length, false))) {
                } else if (sort_item.prefix_len_ > 0) {
                  length = length * sort_item.prefix_len_ / column_schema->get_data_length();
                } else { /*do nothing*/ }

                if (OB_SUCC(ret)) {
                  if ((index_data_length += length) > OB_MAX_USER_ROW_KEY_LENGTH
                      && static_cast<int64_t>(INDEX_KEYNAME::FTS_KEY) != node->value_
                      && static_cast<int64_t>(INDEX_KEYNAME::VEC_KEY) != node->value_) {
                    // text or varchar column is bigger than 16k will be blocked
                    ret = OB_ERR_TOO_LONG_KEY_LENGTH;
                    LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_USER_ROW_KEY_LENGTH);
                  } else if (length <= 0) {
                    ret = OB_ERR_WRONG_KEY_COLUMN;
                    LOG_USER_ERROR(OB_ERR_WRONG_KEY_COLUMN, column_name.length(), column_name.ptr());
                    LOG_WARN("byte_length of string type column should bigger than zero", K(length), K(ret));
                  } else {
                    // do nothing
                  }
                }
              }
            }
            if (OB_SUCC(ret)) {
              //column_order
              {
                // Compatible with mysql5.7, descending index does not take effect and does not report an error
                sort_item.order_type_ = common::ObOrderType::ASC;
              }
              ObColumnNameHashWrapper column_key(column_name);
              if (OB_HASH_NOT_EXIST == column_name_set_.exist_refactored(column_key)) {
                ret = OB_ERR_BAD_FIELD_ERROR;
                LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(),
                               table_name_.length(), table_name_.ptr());
              } else {
                if (0 == i) {
                  first_column_name.assign_ptr(
                      const_cast<char *>(index_column_node->children_[0]->str_value_),
                      static_cast<int32_t>(index_column_node->children_[0]->str_len_));
                }
              }
            }

            if (OB_SUCC(ret)) {
              if (OB_FAIL(add_sort_column(sort_item))) {
              } else { /*do nothing*/ }
            }
          }
          if (OB_SUCC(ret)) {
            if (NULL != node->children_[3]) {
              if (T_USING_BTREE == node->children_[3]->type_) {
                index_arg_.index_using_type_ = USING_BTREE;
              } else {
                index_arg_.index_using_type_ = USING_HASH;
              }
            }
          }
        }

        if (OB_SUCC(ret) && cnt_func_index_mysql) {
          first_column_name = ObString::make_string("functional_index");
        }
      }
    } else if (is_organization_set_to_heap() && ObItemType::T_PRIMARY_KEY == node->type_) {
      if (OB_FAIL(resolve_single_column_primary_key_node(node->children_[0], tbl_schema, process_heap_table_primary_key, first_column_name))) {
      }
    } else {
      //unique [key]
      if (T_COLUMN_DEFINITION != node->type_) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(node->type_));
      } else if (NULL == node->children_[0]
                 || T_COLUMN_REF != node->children_[0]->type_
                 || COLUMN_DEF_NUM_CHILD != node->children_[0]->num_child_) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid argument.", K(ret));
      } else if (OB_ISNULL(node->children_[0]->children_) || OB_ISNULL(node->children_[0]->children_[2])) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "node->ch[0]->ch[2] is null.", K(ret));
      } else {
        index_keyname_ = UNIQUE_KEY;
        ObString &column_name = sort_item.column_name_;
        column_name.assign_ptr(
            const_cast<char *>(node->children_[0]->children_[2]->str_value_),
            static_cast<int32_t>(node->children_[0]->children_[2]->str_len_));
        if (NULL == (column_schema = tbl_schema.get_column_schema(column_name))) {
          ret = OB_ERR_BAD_FIELD_ERROR;
          LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(),
              table_name_.length(), table_name_.ptr());
        } else if (ob_is_string_tc(column_schema->get_data_type()) && !column_schema->is_string_lob()) {
          int64_t length = 0;
          if (OB_FAIL(column_schema->get_byte_length(length, false))) {
          } else if (length > OB_MAX_USER_ROW_KEY_LENGTH) {
            ret = OB_ERR_TOO_LONG_KEY_LENGTH;
            LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_USER_ROW_KEY_LENGTH);
          } else if (length <= 0) {
            ret = OB_ERR_WRONG_KEY_COLUMN;
            LOG_USER_ERROR(OB_ERR_WRONG_KEY_COLUMN, column_name.length(), column_name.ptr());
          } else {
            // do nothing
          }
        }
        if (OB_SUCC(ret) && is_organization_set_to_heap()) {
          ParseNode *attrs_node = node->children_[2];
          bool has_pk = false;
          HEAP_VAR(ObCreateIndexStmt, create_index_stmt) {
            ObSArray<obcall::ObCreateIndexArg> &index_arg_list = create_table_stmt->get_index_arg_list();
            for (int64_t i = 0; OB_SUCC(ret) && i < index_arg_list.size(); ++i) {
              ObCreateIndexArg &create_index_arg = index_arg_list.at(i);
              has_pk |= INDEX_TYPE_HEAP_ORGANIZED_TABLE_PRIMARY == create_index_arg.index_type_;
            }
          }
          if (OB_FAIL(ret)) {
          } else if (attrs_node != NULL && !has_pk) {
            for (int64_t i = 0; OB_SUCC(ret) && i < attrs_node->num_child_; ++i) {
              ParseNode *attr_node = attrs_node->children_[i];
              if (T_CONSTR_PRIMARY_KEY == attr_node->type_) {
                process_heap_table_primary_key = true;
              }
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
          first_column_name = sort_item.column_name_;
          sort_item.order_type_ = common::ObOrderType::ASC;
          if (OB_FAIL(add_sort_column(sort_item))) {
          }
      }
    }

    if (OB_SUCC(ret)) {
      has_index_using_type_ = false;
      if (OB_FAIL(resolve_index_name(
          ObItemType::T_INDEX == node->type_ ? node->children_[0] : NULL,
          first_column_name,
          (UNIQUE_KEY == index_keyname_ || MULTI_UNIQUE_KEY == index_keyname_) ? true : false,
          uk_name))) {
      } else if (ObItemType::T_INDEX == node->type_ && OB_FAIL(resolve_table_options(node->children_[2], true))) {
        SQL_RESV_LOG(WARN, "resolve index options failed", K(ret));
      }
      if (OB_SUCC(ret)) {
        if (ObItemType::T_INDEX == node->type_ && NULL != node->children_[4]) {
          if (1 != node->children_[4]->num_child_ || T_PARTITION_OPTION != node->children_[4]->type_) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "column vertical partition for index");
          } else if (OB_ISNULL(node->children_[4]->children_[0])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("node is null", K(ret));
          } else if (LOCAL_INDEX == index_scope_) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "specify partition option of local index");
          } else if (NOT_SPECIFIED == index_scope_) {
            index_scope_ = GLOBAL_INDEX;
          }
          is_index_part_specified = true;
        }
      }

      // index column_group
      if (OB_SUCC(ret)) { //only mysql support create table with index
        if (node->num_child_ < 6) {
          // no cg, ignore
        } else if (ObItemType::T_INDEX == node->type_ && NULL != node->children_[5]) {
          if (T_COLUMN_GROUP != node->children_[5]->type_ || node->children_[5]->num_child_ <= 0) {
            ret = OB_INVALID_ARGUMENT;
            SQL_RESV_LOG(WARN, "invalid argument", KR(ret), K(node->type_), K(node->num_child_));
          } else if (OB_ISNULL(node->children_[5]->children_[0])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("node is null", K(ret));
          } else if (OB_FAIL(resolve_index_column_group(node->children_[5], index_arg_))) {
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(generate_index_arg(process_heap_table_primary_key))) {
      } else if (tbl_schema.is_partitioned_table()
          && INDEX_TYPE_SPATIAL_GLOBAL == index_arg_.index_type_) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "spatial global index");
      } else {
        if (has_index_using_type_) {
          index_arg_.index_using_type_ = index_using_type_;
        }
      }
    }
    if (OB_SUCC(ret)) {
      HEAP_VARS_2((ObCreateIndexStmt, create_index_stmt), (ObPartitionResolveResult, resolve_result))  {
        ObCreateIndexArg &create_index_arg = create_index_stmt.get_create_index_arg();
        ObSArray<ObPartitionResolveResult> &resolve_results = create_table_stmt->get_index_partition_resolve_results();
        ObSArray<obcall::ObCreateIndexArg> &index_arg_list = create_table_stmt->get_index_arg_list();
        index_arg_.index_key_ = static_cast<int64_t>(index_keyname_);
        if (OB_FAIL(create_index_arg.assign(index_arg_))) {
        } else if (is_index_part_specified) {
          if (OB_FAIL(resolve_index_partition_node(node->children_[4]->children_[0], &create_index_stmt))) {
          } else {
            resolve_result.get_part_fun_exprs() = create_index_stmt.get_part_fun_exprs();
            resolve_result.get_part_values_exprs() = create_index_stmt.get_part_values_exprs();
            resolve_result.get_subpart_fun_exprs() = create_index_stmt.get_subpart_fun_exprs();
            resolve_result.get_template_subpart_values_exprs() = create_index_stmt.get_template_subpart_values_exprs();
            resolve_result.get_individual_subpart_values_exprs() = create_index_stmt.get_individual_subpart_values_exprs();
          }
        }
        if (OB_SUCC(ret)) {
          if (is_vec_index(index_arg_.index_type_)) {
            // refresh vector index type
            if (!is_vec_index(vec_index_type_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("unexpected index type", KR(ret), K(vec_index_type_));
            } else if (FALSE_IT(create_index_arg.index_type_ = vec_index_type_)) {
            } else if (FALSE_IT(create_index_arg.index_schema_.set_index_params(index_params_))) {
            } else if (ObVectorIndexUtil::should_set_max_lob_inrow_threshold_for_async_index(
                           tbl_schema, vec_index_type_, index_params_)
                       && FALSE_IT(tbl_schema.set_lob_inrow_threshold(OB_MAX_LOB_INROW_THRESHOLD_FOR_VEC_ASYNC_INDEX))) {
            } else if (OB_FAIL(ObVecIndexBuilderUtil::append_vec_args(tbl_schema,
                                                                      resolve_result,
                                                                      create_index_arg,
                                                                      have_generate_vec_arg_,
                                                                      have_generate_fts_arg_,
                                                                      resolve_results,
                                                                      index_arg_list,
                                                                      allocator_,
                                                                      session_info_))) {
            } else if (OB_FAIL(vec_index_col_ids_.push_back(vec_index_col_id))) {
            } else {
              has_vec_index_ = true;
            }
          } else if (is_fts_index(index_arg_.index_type_)) {
            if (OB_FAIL(ObDDLResolver::append_fts_args(tbl_schema,
                                                       resolve_result,
                                                       create_index_arg,
                                                       have_generate_fts_arg_,
                                                       resolve_results,
                                                       index_arg_list,
                                                       allocator_))) {
            } else {
              has_fts_index_ = true;
            }
          } else if (is_multivalue_index(index_arg_.index_type_)) {
            if (OB_FAIL(ObDDLResolver::append_multivalue_args(tbl_schema,
                                                              resolve_result,
                                                              create_index_arg,
                                                              have_generate_fts_arg_,
                                                              resolve_results,
                                                              index_arg_list,
                                                              allocator_))) {
            } else {
              has_multivalue_index_ = true;
            }
          } else {
            if (OB_FAIL(resolve_results.push_back(resolve_result))) {
            } else if (OB_FAIL(index_arg_list.push_back(create_index_arg))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::resolve_index_name(
    const ParseNode *node,
    const ObString &first_column_name,
    bool is_unique,
    ObString &uk_name)
{
  int ret =OB_SUCCESS;
  name_generated_type_ = GENERATED_TYPE_USER;
  if (NULL == node) {
    if (OB_FAIL(generate_index_name(index_name_, current_index_name_set_, first_column_name))) {
    } else {
      name_generated_type_ = GENERATED_TYPE_SYSTEM;
    }
  } else if (T_IDENT != node->type_) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "node_type is wrong.", K(ret));
  } else {
    int32_t len = static_cast<int32_t>(node->str_len_);
    index_name_.assign_ptr(node->str_value_, len);
    //check duplicate for index_name
    ObIndexNameHashWrapper index_key(index_name_);
    if (OB_HASH_EXIST == (ret = current_index_name_set_.exist_refactored(index_key))) {
      SQL_RESV_LOG(WARN, "duplicate index name", K(ret), K(index_name_));
      ret = OB_ERR_KEY_NAME_DUPLICATE;
      LOG_USER_ERROR(OB_ERR_KEY_NAME_DUPLICATE,
                     index_name_.length(),
                     index_name_.ptr());
    } else if (0 == ObString::make_string("primary").case_compare(index_name_)) {
    //index name can not be 'primary'
      ret = OB_WRONG_NAME_FOR_INDEX;
      LOG_USER_ERROR(OB_WRONG_NAME_FOR_INDEX, index_name_.length(), index_name_.ptr());
    } else if (index_name_.empty()) {
      ret = OB_WRONG_NAME_FOR_INDEX;
      SQL_RESV_LOG(WARN, "index name is empty", K(ret), K(index_name_));
      LOG_USER_ERROR(OB_WRONG_NAME_FOR_INDEX, index_name_.length(), index_name_.ptr());
    } else {
      ret = OB_SUCCESS;
    }
  }

  if (OB_SUCC(ret)) {
    ObCollationType cs_type = CS_TYPE_INVALID;
    if (OB_UNLIKELY(NULL == session_info_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session if NULL", K(ret));
    } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    } else if (OB_FAIL(ObSQLUtils::check_index_name(cs_type, index_name_))) {
    }
  }

  return ret;
}

int ObCreateTableResolver::resolve_table_charset_info(const ParseNode *node) {
  int ret = OB_SUCCESS;
  if (NULL != node) {
    if (T_TABLE_OPTION_LIST != node->type_) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "invalid argument.", K(ret));
    } else {
      ParseNode *option_node = NULL;
      int32_t num = node->num_child_;
      for (int32_t i = 0; OB_SUCC(ret) && i < num; ++i) {
        option_node = node->children_[i];
        if (OB_ISNULL(option_node)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "invalid argument.", K(ret), K(option_node));
        } else if (T_CHARSET == option_node->type_
            && OB_FAIL(resolve_table_option(option_node, false))) {
          SQL_RESV_LOG(WARN, "resolve failed", K(ret));
        } else if (T_COLLATION == option_node->type_
                   && OB_FAIL(resolve_table_option(option_node, false))) {
          SQL_RESV_LOG(WARN, "resolve failed", K(ret));
        } else { /*do nothing*/ }
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (CHARSET_INVALID == charset_type_ && CS_TYPE_INVALID == collation_type_) {
      // The database character set and collation affect these aspects of server operation:
      //
      // For CREATE TABLE statements, the database character set and collation are used as default
      // values for table definitions if the table character set and collation are not specified.
      // To override this, provide explicit CHARACTER SET and COLLATE table options.
      
      ObString database_name;
      uint64_t database_id = OB_INVALID_ID;
      const ObDatabaseSchema *database_schema = NULL;
      if (OB_FAIL(schema_checker_->get_database_id(database_name_, database_id)))  {
      } else if (OB_FAIL(schema_checker_->get_database_schema( database_id, database_schema))) {
      } else if (OB_ISNULL(database_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error. db schema is null", K(ret), K(database_schema));
      } else {
        charset_type_ = database_schema->get_charset_type();
        collation_type_ = database_schema->get_collation_type();
      }
    } else if (OB_FAIL(ObCharset::check_and_fill_info(charset_type_, collation_type_))) {
    }
  }

  return ret;
}

int ObCreateTableResolver::check_max_row_data_length(const ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  int64_t row_data_length = 0;
  int64_t rowkey_data_length = 0;
  bool has_string_lob = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < table_schema.get_column_count(); ++i) {
    int64_t length = 0;
    const ObColumnSchemaV2 *column = table_schema.get_column_schema_by_idx(i);
    if (OB_ISNULL(column)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("column is null", K(ret), K(table_schema));
    } else if (! column->is_string_type() && ! is_lob_storage(column->get_data_type()) ) { // skip non string or lob storage type
    } else if (OB_FAIL(column->get_byte_length(length, false))) {
    } else if (ob_is_string_tc(column->get_data_type()) && length > OB_MAX_VARCHAR_LENGTH) {
      ret = OB_ERR_TOO_LONG_COLUMN_LENGTH;
      LOG_USER_ERROR(OB_ERR_TOO_LONG_COLUMN_LENGTH, column->get_column_name(), static_cast<int32_t>(OB_MAX_VARCHAR_LENGTH));
    } else if (is_lob_storage(column->get_data_type())) {
      ObLength max_length = 0;
      max_length = ObAccuracy::MAX_ACCURACY2[MYSQL_MODE][column->get_data_type()].get_length();
      if (length > max_length) {
        ret = OB_ERR_TOO_LONG_COLUMN_LENGTH;
        LOG_USER_ERROR(OB_ERR_TOO_LONG_COLUMN_LENGTH, column->get_column_name(),
            ObAccuracy::MAX_ACCURACY2[MYSQL_MODE][column->get_data_type()].get_length());
      } else {
        if (length <= 0) {  // Temporary workaround only for array/vector/roaringbitmap types.
          if (column->is_roaringbitmap()) {
            length = ObAccuracy::DDL_DEFAULT_ACCURACY[ObRoaringBitmapType].get_length();
          } else if (column->is_collection()) {
            length = ObAccuracy::DDL_DEFAULT_ACCURACY[ObCollectionSQLType].get_length();
          }
        }
        length = min(length, max(table_schema.get_lob_inrow_threshold(), OB_MAX_LOB_HANDLE_LENGTH));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (column->is_rowkey_column()) {
      if (column->is_string_lob()) {
        has_string_lob = true;
      } else {
        rowkey_data_length += length;
      }
    } else {
      row_data_length += length;
    }
  }
  if (OB_SUCC(ret)) {
    if (has_string_lob) {
      rowkey_data_length = OB_MAX_VARCHAR_LENGTH_KEY;
    }
    if (row_data_length + rowkey_data_length > OB_MAX_USER_ROW_LENGTH) {
      ret = OB_ERR_TOO_BIG_ROWSIZE;
      SQL_RESV_LOG(WARN, "too big rowsize", KR(ret), K(row_data_length), K(rowkey_data_length));
    }
  }

  return ret;
}

int ObCreateTableResolver::check_building_domain_index_legal()
{
  int ret = OB_SUCCESS;
  if (!index_aux_name_set_.created() &&
      OB_FAIL(index_aux_name_set_.create(common::OB_MAX_COLUMN_NUMBER))) {
    LOG_WARN("fail to init index aux name set", K(ret));
  } else {
    ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
    const ObSArray<obcall::ObCreateIndexArg> &index_arg_list = create_table_stmt->get_index_arg_list();
    for (int64_t i = 0; OB_SUCC(ret) && i < index_arg_list.count(); ++i) {
      const obcall::ObCreateIndexArg &index_arg = index_arg_list.at(i);
      ObIndexNameHashWrapper index_name_key(index_arg.index_name_);
      if (OB_FAIL(index_aux_name_set_.exist_refactored(index_name_key))) {
        if (OB_HASH_EXIST == ret) {
          ret = OB_ERR_KEY_NAME_DUPLICATE;
          LOG_USER_ERROR(OB_ERR_KEY_NAME_DUPLICATE,
              index_arg.index_name_.length(), index_arg.index_name_.ptr());
          LOG_WARN("there is duplicate index aux name", K(ret), K(index_arg.index_name_));
        } else if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail to search index aux name set", K(ret), K(index_arg.index_name_));
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(index_aux_name_set_.set_refactored(index_name_key))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::resolve_primary_key_node_in_heap_table(const ParseNode *element, common::ObArray<ObColumnResolveStat> &stats,
                                                                  ObSEArray<ObColumnSchemaV2, SEARRAY_INIT_NUM> &resolved_cols)
{
  int ret = OB_SUCCESS;
  int64_t resolved_cols_count = resolved_cols.count();
  const bool is_mysql_mode = true;
  if ((3 < element->num_child_ )
      || OB_ISNULL(element->children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "the num_child of the parse node is wrong.",
                 K(ret), K(element->num_child_), K(element->children_));
  } else {
    ParseNode *column_list_node = element->children_[0];
    if (OB_ISNULL(column_list_node)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "column_list_node is null.", K(ret));
    } else if (T_COLUMN_LIST != column_list_node->type_
               || column_list_node->num_child_ <= 0
               || OB_ISNULL(column_list_node->children_)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "unexpected.",
                   K(ret), K(column_list_node->type_), K(column_list_node->num_child_));
    } else {
      ParseNode *key_node = nullptr;
      for (int32_t i = 0; OB_SUCC(ret) && i < column_list_node->num_child_; ++i) {
        if (OB_ISNULL(column_list_node->children_[i])) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "column_list_node->children_[i] is null.", K(ret));
        } else {
          key_node = column_list_node->children_[i];
          if (OB_ISNULL(key_node)) {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "key_node is null.", K(ret));
            break;
          }
          ObString column_name;
          column_name.assign_ptr(const_cast<char *>(key_node->str_value_), static_cast<int32_t>(key_node->str_len_));
          ObColumnSchemaV2 *col = NULL;
          ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
          ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
          int64_t length = 0;
          int64_t index_data_length = 0;

          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(create_table_stmt)) {
            ret = OB_NOT_INIT;
            SQL_RESV_LOG(WARN, "stmt is null", KP(create_table_stmt), K(ret));
          } else if (OB_ISNULL(session_info_)) {
            ret = OB_NOT_INIT;
            SQL_RESV_LOG(WARN, "session is null", KP(session_info_), K(ret));
          } else if (OB_ISNULL(col = table_schema.get_column_schema(column_name))) {
            ret = OB_ERR_KEY_COLUMN_DOES_NOT_EXITS;
            LOG_USER_ERROR(OB_ERR_KEY_COLUMN_DOES_NOT_EXITS, column_name.length(), column_name.ptr());
            SQL_RESV_LOG(WARN, "column does not exists", K(ret), K(column_name));
          } else if (OB_FAIL(check_add_column_as_pk_allowed(*col))) {
          } else if (is_mysql_mode && ob_is_collection_sql_type(col->get_data_type())) {
            ret = OB_NOT_SUPPORTED;
            LOG_WARN("not support primary key is vector column yet", K(ret));
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "create primary key on vector column is");
          } else if (column_list_node->num_child_ > OB_USER_MAX_ROWKEY_COLUMN_NUMBER) {
            ret = OB_ERR_TOO_MANY_ROWKEY_COLUMNS;
            LOG_USER_ERROR(OB_ERR_TOO_MANY_ROWKEY_COLUMNS, OB_USER_MAX_ROWKEY_COLUMN_NUMBER);
          } else if (OB_FALSE_IT(col->add_column_flag(HEAP_TABLE_PRIMARY_KEY_FLAG))
                  || OB_FALSE_IT(col->set_nullable(false))
                  || OB_FALSE_IT(col->set_rowkey_position(0))){
          } else if (col->is_string_lob() || !col->is_string_type()) {
            /* do nothing */
          } else if (OB_FAIL(col->get_byte_length(length, false))) {
          } else if ((index_data_length += length) > OB_MAX_USER_ROW_KEY_LENGTH) {
            ret = OB_ERR_TOO_LONG_KEY_LENGTH;
            LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_USER_ROW_KEY_LENGTH);
          } else if (index_data_length <= 0) {
            ret = OB_ERR_WRONG_KEY_COLUMN;
            LOG_USER_ERROR(OB_ERR_WRONG_KEY_COLUMN, column_name.length(), column_name.ptr());
          }
          ObColumnResolveStat *stat = NULL;
          for (int64_t i = 0; NULL == stat && OB_SUCC(ret) && i < stats.count(); ++i) {
            if (stats.at(i).column_id_ == col->get_column_id()) {
              stat = &stats.at(i);
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(stat)) {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "fail to find column stat", K(ret), K(column_name));
          } else if (stat->is_set_null_ || (stat->is_set_default_value_ && col->get_cur_default_value().is_null())) {
            ret = OB_ERR_PRIMARY_CANT_HAVE_NULL;
          }
        }
      }
    }
    if (OB_SUCC(ret) && NULL != element->children_[1]) {
      ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
      if (T_USING_HASH == element->children_[1]->type_) {
        create_table_stmt->set_index_using_type(share::schema::USING_HASH);
      } else {
        create_table_stmt->set_index_using_type(share::schema::USING_BTREE);
      }
    }
    if (OB_SUCC(ret)) {
      if (NULL != element->children_[2]) {
        ObCreateTableStmt *create_table_stmt = static_cast<ObCreateTableStmt*>(stmt_);
        ObTableSchema &table_schema = create_table_stmt->get_create_table_arg().schema_;
        ObString pk_comment;
        pk_comment.assign_ptr(element->children_[2]->str_value_,static_cast<int32_t>(element->children_[2]->str_len_));
        if (OB_FAIL(table_schema.set_pk_comment(pk_comment))) {
        }
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::resolve_single_column_primary_key_node(const ParseNode *column_list_node, ObTableSchema &tbl_schema,
                                                                  bool &process_heap_table_primary_key, ObString &first_column_name)
{
  int ret = OB_SUCCESS;
  process_heap_table_primary_key = true;
  if (OB_ISNULL(column_list_node)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "column_list_node is null.", K(ret));
  } else if (T_COLUMN_LIST != column_list_node->type_
            || column_list_node->num_child_ <= 0
            || OB_ISNULL(column_list_node->children_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "unexpected.",
                K(ret), K(column_list_node->type_), K(column_list_node->num_child_));
  } else {
    ParseNode *key_node = nullptr;
    index_keyname_ = UNIQUE_KEY;
    ObColumnSchemaV2 *column_schema = NULL;
    for (int32_t i = 0; OB_SUCC(ret) && i < column_list_node->num_child_; ++i) {
      if (OB_ISNULL(column_list_node->children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "column_list_node->children_[i] is null.", K(ret));
      } else {
        ObColumnSortItem sort_item;
        ObString &column_name = sort_item.column_name_;
        key_node = column_list_node->children_[i];
        column_name.assign_ptr(
          const_cast<char *>(key_node->str_value_),
          static_cast<int32_t>(key_node->str_len_));
        if (NULL == (column_schema = tbl_schema.get_column_schema(column_name))) {
          ret = OB_ERR_BAD_FIELD_ERROR;
          LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(),
              table_name_.length(), table_name_.ptr());
        } else if (ob_is_string_tc(column_schema->get_data_type()) && !column_schema->is_string_lob()) {
          int64_t length = 0;
          if (OB_FAIL(column_schema->get_byte_length(length, false))) {
          } else if (length > OB_MAX_USER_ROW_KEY_LENGTH) {
            ret = OB_ERR_TOO_LONG_KEY_LENGTH;
            LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_USER_ROW_KEY_LENGTH);
          } else if (length <= 0) {
            ret = OB_ERR_WRONG_KEY_COLUMN;
            LOG_USER_ERROR(OB_ERR_WRONG_KEY_COLUMN, column_name.length(), column_name.ptr());
          } else {
            // do nothing
          }
        }

        if (OB_SUCC(ret)) {
          if (0 == i) {
            first_column_name = sort_item.column_name_;
          }

          sort_item.order_type_ = common::ObOrderType::ASC;
          if (OB_FAIL(add_sort_column(sort_item))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObCreateTableResolver::uk_or_heap_table_pk_add_to_index_list(ObArray<int> &index_node_position_list, const int32_t node_index)
{
  int ret = OB_SUCCESS;
  if (OB_MAX_INDEX_PER_TABLE == index_node_position_list.count()) {
    ret = OB_ERR_TOO_MANY_KEYS;
    LOG_USER_ERROR(OB_ERR_TOO_MANY_KEYS, OB_MAX_INDEX_PER_TABLE);
  } else if (OB_FAIL(index_node_position_list.push_back(node_index))){
  } else {
  }
  return ret;
}
}//end namespace sql
}//end namespace oceanbase
