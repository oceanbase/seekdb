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
#include "sql/resolver/ddl/ob_alter_table_resolver.h"
#include "sql/resolver/expr/ob_raw_expr_part_expr_checker.h"
#include "sql/resolver/dml/ob_delete_resolver.h"
#include "sql/resolver/ddl/ob_index_builder_util.h"
#include "sql/resolver/ddl/ob_fts_index_builder_util.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/session/ob_local_session_var.h"
#include "query/command/ob_root_command_service.h"
#include "query/vector/ob_vector_index_util.h"

namespace oceanbase
{
using namespace share::schema;
using obcall::ObCreateIndexArg;
using obcall::ObDropIndexArg;
using namespace common;
using namespace obcall;
namespace sql
{
ObAlterTableResolver::ObAlterTableResolver(ObResolverParams &params)
    : ObDDLResolver(params),
      table_schema_(NULL),
      index_schema_(NULL),
      current_index_name_set_(),
      add_or_modify_check_cst_times_(0),
      modify_constraint_times_(0),
      add_not_null_constraint_(false),
      add_column_cnt_(0)
{
}

ObAlterTableResolver::~ObAlterTableResolver()
{
}


int ObAlterTableResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "session_info should not be null", K(ret));
  } else {
    ObAlterTableStmt *alter_table_stmt = NULL;
    //create alter table stmt
    if (NULL == (alter_table_stmt = create_stmt<ObAlterTableStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "failed to create alter table stmt", K(ret));
    } else if (OB_FAIL(alter_table_stmt->set_tz_info_wrap(session_info_->get_tz_info_wrap()))) {
    } else if (OB_FAIL(alter_table_stmt->fill_session_vars(*session_info_))) {
    } else {
      alter_table_stmt->set_lock_priority();
      alter_table_stmt->set_lock_session_info(session_info_->get_server_sid(),
                                              session_info_->get_sess_create_time());
      stmt_ = alter_table_stmt;
    }
    //resolve table
    if (OB_SUCC(ret)) {
      //alter table database_name.table_name ...
      ObString database_name;
      ObString table_name;
      if (OB_FAIL(resolve_table_relation_node(&peg::alter_table_relation(parse_tree),
                                                     table_name,
                                                     database_name,
                                                     false))) {
      } else if (OB_FAIL(set_database_name(database_name))) {
      } else {
        
        if (OB_FAIL(alter_table_stmt->set_origin_table_name(table_name))) {
        } else if (OB_FAIL(set_table_name(table_name))) {
        } else if (OB_FAIL(alter_table_stmt->set_origin_database_name(database_name))) {
        } else if (OB_FAIL(schema_checker_->get_table_schema(
                      database_name,
                      table_name,
                      false/*not index table*/,
                      table_schema_))) {
          if (OB_TABLE_NOT_EXIST == ret) {
            ObCStringHelper helper;
            LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(database_name),
                helper.convert(table_name));
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(table_schema_)) {
          ret = OB_ERR_UNEXPECTED;

        }
      }
    }
    if (OB_SUCC(ret)) {
      
      alter_table_stmt->set_table_id(table_schema_->get_table_id());
      alter_table_stmt->get_alter_table_arg().alter_table_schema_.set_charset_type(table_schema_->get_charset_type());
      alter_table_stmt->get_alter_table_arg().alter_table_schema_.set_collation_type(table_schema_->get_collation_type());
    }
    //resolve action list
    if (OB_SUCCESS == ret){
      if (OB_FAIL(resolve_action_list(peg::alter_table_actions(parse_tree)))) {
      } else if (OB_FAIL(set_table_options())) {
      } else if (OB_FAIL(check_semistruct_encoding_type(*table_schema_, alter_table_stmt->get_alter_table_schema()))) {
      } else {
        // deal with alter table rename to mock_fk_parent_table_name
        if (alter_table_bitset_.has_member(obcall::ObAlterTableArg::TABLE_NAME)) {
          ObSchemaGetterGuard *schema_guard = schema_checker_->get_schema_guard();
          const AlterTableSchema &alter_table_schema = alter_table_stmt->get_alter_table_arg().alter_table_schema_;
          const ObString &new_table_name = alter_table_schema.get_table_name_str();
          const ObString &origin_table_name = alter_table_schema.get_origin_table_name();
          const ObString &new_database_name = alter_table_schema.get_database_name();
          const ObString &origin_database_name = alter_table_schema.get_origin_database_name();
          const ObTableSchema *orig_table_schema = NULL;
          const ObMockFKParentTableSchema *mock_parent_table_schema = NULL;
          if (OB_ISNULL(schema_guard)) {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "schema_guard is null", K(ret));
          } else if (OB_FAIL(schema_guard->get_table_schema(
                             origin_database_name, origin_table_name, false, orig_table_schema))) {
          } else if (OB_ISNULL(orig_table_schema)) {
            ret = OB_ERR_TABLE_EXIST;
          } else {
            ObString database_name;
            uint64_t database_id = OB_INVALID_ID;
            ObNameCaseMode mode = OB_NAME_CASE_INVALID;
            if (OB_FAIL(schema_guard->get_runtime_name_case_mode(mode))) {
            } else if (!new_database_name.empty() && !ObCharset::case_mode_equal(mode, new_database_name, origin_database_name)) {
              database_name = new_database_name;
            } else {
              database_name = origin_database_name;
            }
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(schema_guard->get_database_id(database_name, database_id))) {
            } else if (OB_INVALID_ID == database_id) {
              // do nothing
            } else if (OB_FAIL(schema_checker_->get_mock_fk_parent_table_with_name(database_id,
                new_table_name, mock_parent_table_schema))) {
            } else if (OB_NOT_NULL(mock_parent_table_schema)) {
              if (alter_table_stmt->get_alter_table_action_count() > 1) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "Alter table rename to mock fk parent table name with other actions");
              }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      // alter table path
      if (OB_SUCC(ret) && OB_NOT_NULL(table_schema_)) {
        if (OB_FAIL(alter_table_stmt->get_alter_table_arg().based_schema_object_infos_.
                    push_back(ObBasedSchemaObjectInfo(
                              table_schema_->get_table_id(),
                              TABLE_SCHEMA,
                              table_schema_->get_schema_version())))) {
        }
      }
      // alter index path
      if (OB_SUCC(ret) && OB_NOT_NULL(index_schema_)) {
        if (OB_FAIL(alter_table_stmt->get_alter_table_arg().based_schema_object_infos_.
                    push_back(ObBasedSchemaObjectInfo(
                              index_schema_->get_table_id(),
                              TABLE_SCHEMA,
                              index_schema_->get_schema_version())))) {
        }
      }
    }
    
    if (OB_SUCC(ret)) {
      if (OB_FAIL(resolve_hints(peg::query_hints(parse_tree),
          *alter_table_stmt, nullptr == index_schema_ ? *table_schema_ : *index_schema_))) {
      }
    }
    if (OB_SUCC(ret)){
      if (OB_FAIL(deep_copy_string_in_part_expr(get_alter_table_stmt()))) {
      }
    }

    if (OB_SUCC(ret)) {
      const ObTableSchema *tbl_schema = nullptr;
      if (OB_FAIL(get_table_schema_for_check(tbl_schema))) {
      } else if (OB_ISNULL(tbl_schema)) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  DEBUG_SYNC(HANG_BEFORE_RESOLVER_FINISH);
  return ret;
}

int ObAlterTableResolver::set_table_options()
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  if (OB_ISNULL(alter_table_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "stmt should not be null!", K(ret));
  } else {
    AlterTableSchema &alter_table_schema = alter_table_stmt->get_alter_table_arg().alter_table_schema_;
      //this can be set by alter table option
    alter_table_schema.set_auto_increment(auto_increment_);
    alter_table_schema.set_block_size(block_size_);
    alter_table_schema.set_charset_type(charset_type_);
    alter_table_schema.set_collation_type(collation_type_);
    alter_table_schema.set_tablet_size(tablet_size_);
    alter_table_schema.set_pctfree(pctfree_);
    alter_table_schema.set_progressive_merge_num(progressive_merge_num_);
    alter_table_schema.set_read_only(read_only_);
    alter_table_schema.set_row_store_type(row_store_type_);
    alter_table_schema.set_store_format(store_format_);
    alter_table_schema.set_enable_row_movement(enable_row_movement_);
    alter_table_schema.set_table_mode_struct(table_mode_);
    alter_table_schema.set_dop(table_dop_);
    alter_table_schema.set_lob_inrow_threshold(lob_inrow_threshold_);
    alter_table_schema.set_auto_increment_cache_size(auto_increment_cache_size_);
    alter_table_schema.set_semistruct_encoding_type(semistruct_encoding_type_);
    //deep copy
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_FAIL(alter_table_schema.set_compress_func_name(compress_method_))) {
    } else if (OB_FAIL(alter_table_schema.set_comment(comment_))) {
    } else if (OB_FAIL(alter_table_schema.set_table_name(table_name_))) {
    } else if (OB_FAIL(alter_table_schema.set_database_name(database_name_))) {
    } else {
      alter_table_schema.alter_option_bitset_ = alter_table_bitset_;
    }

    if (OB_FAIL(ret)) {
      //do nothing
    }

    if (OB_FAIL(ret)) {
      alter_table_schema.reset();
      SQL_RESV_LOG(WARN, "Set table options error!", K(ret));
    } else {
    }
  }
  return ret;
}

// bug: 48644348
int ObAlterTableResolver::check_alter_column_schemas_valid(ObAlterTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (stmt.get_alter_table_arg().is_alter_columns_) {
    const AlterTableSchema &alter_table_schema = stmt.get_alter_table_arg().alter_table_schema_;
    ObTableSchema::const_column_iterator it_begin = alter_table_schema.column_begin();
    ObTableSchema::const_column_iterator it_end = alter_table_schema.column_end();
    ObSEArray<ObString, 2> dependent_columns;
    ObSEArray<ObString, 2> drop_columns;
    const ObColumnSchemaV2 *col_schema = NULL;
    AlterColumnSchema *alter_column_schema = NULL;
    ObString alter_column_name;
    for (; OB_SUCC(ret) && it_begin != it_end; it_begin++) {
      if (OB_ISNULL(alter_column_schema = static_cast<AlterColumnSchema *>(*it_begin))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_DDL_DROP_COLUMN == alter_column_schema->alter_type_) {
        alter_column_name = alter_column_schema->get_origin_column_name();
        if (OB_FAIL(drop_columns.push_back(alter_column_name))) {
        }
      } else if (OB_DDL_ADD_COLUMN == alter_column_schema->alter_type_ &&
                 alter_column_schema->is_generated_column()) {
        ObSEArray<ObString, 2> columns_names;
        ObItemType root_expr_type = T_INVALID;
        if (OB_FAIL(alter_column_schema->get_cur_default_value().get_string(alter_column_name))) {
        } else if (OB_UNLIKELY(alter_column_name.empty())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ObResolverUtils::resolve_generated_column_info(alter_column_name,
                                                                          *allocator_,
                                                                          root_expr_type,
                                                                          columns_names))) {
        } else if (OB_FAIL(append(dependent_columns, columns_names))) {
        }
      }
    }
    if (OB_SUCC(ret) && !drop_columns.empty() && !dependent_columns.empty()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < drop_columns.count(); ++i) {
        for (int64_t j = 0; OB_SUCC(ret) && j < dependent_columns.count(); ++j) {
          if (0 == drop_columns.at(i).compare(dependent_columns.at(j))) {
            const ObString &column_name = drop_columns.at(i);
            ObString scope_name = "generated column function";
            ret = OB_ERR_BAD_FIELD_ERROR;
            LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(),
                          scope_name.length(), scope_name.ptr());
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_action_list(rust::Slice<const peg::Node> actions)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  if (OB_ISNULL(alter_table_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
  } else if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
  } else {
    // Get the existing index name in the table, and initialize current_index_name_set_
    ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
    // drop_col_act_position_list is only used in mysql mode
    // to resolve drop_column_nodes after drop constraint nodes resolved
    ObArray<int> drop_col_act_position_list;
    if (OB_FAIL(table_schema_->get_simple_index_infos(simple_index_infos))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
      const ObTableSchema *index_table_schema = NULL;
      ObString index_name;
      if (OB_FAIL(schema_checker_->get_table_schema( simple_index_infos.at(i).table_id_, index_table_schema))) {
      } else if (OB_ISNULL(index_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (index_table_schema->is_built_in_fts_index()) {
        // skip built-in fts index
      } else if (OB_FAIL(index_table_schema->get_index_name(index_name))) {
      } else {
        ObIndexNameHashWrapper index_key(index_name);
        if (OB_FAIL(current_index_name_set_.set_refactored(index_key))) {
        }
      }
    }
    HEAP_VARS_3((ObColumnNameSet, add_column_names_set),
                (ObReducedVisibleColSet, reduced_visible_col_set),
                (ObReducedVisibleColSet, drop_column_names_set)) {
    // Column visibility bookkeeping for alter table.
    bool is_modify_column_visibility = false;
    int64_t alter_column_times = 0;
    int64_t alter_column_visibility_times = 0;
    bool has_alter_column_option = false;
    bool has_add_column = false;
    bool has_drop_column = false;
    bool has_alter_partition = false;
    //in mysql mode, resolve add index after resolve column actions
    ObSEArray<int64_t, 4> add_index_action_idxs;
    for (int64_t i = 0; OB_SUCC(ret) && i < actions.size(); ++i) {
      const peg::Node *action_node = &actions[i];
      if (OB_ISNULL(action_node)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "invalid parse tree!", K(ret));
      } else if (FALSE_IT(alter_table_stmt->inc_alter_table_action_count())) {
      } else {
        switch (action_node->kind()) {
        //deal with alter table option
        case peg::NodeKind::TableRename: {
            alter_table_stmt->set_alter_table_option();
            OZ(resolve_table_option(*action_node, false));
            break;
          }
        case peg::NodeKind::TableOptions: {
            alter_table_stmt->set_alter_table_option();
            if (OB_FAIL(resolve_alter_table_option_list(*action_node))) {
            }
            break;
          }
        case peg::NodeKind::ConvertTableCharset: {
          alter_table_stmt->set_convert_to_character();
          if (OB_FAIL(resolve_convert_to_character(*action_node))) {
          }
          break;
        }
        //deal with add column, alter column, drop column, change column, modify column
        case peg::NodeKind::ColumnAdd:
        case peg::NodeKind::ColumnDrop:
        case peg::NodeKind::ColumnModify:
        case peg::NodeKind::ColumnChange:
        case peg::NodeKind::ColumnRename:
        case peg::NodeKind::TableForce:
        case peg::NodeKind::ColumnAlter: {
            alter_table_stmt->set_alter_table_column();
            bool temp_is_modify_column_visibility = false;
            bool is_mysql_drop_column = false;
            has_alter_column_option = true;
            bool tmp_has_add_column = false;
            bool tmp_has_drop_column = false;
            if (OB_FAIL(resolve_column_options(*action_node, temp_is_modify_column_visibility,
                                              is_mysql_drop_column, add_column_names_set,
                                              reduced_visible_col_set, drop_column_names_set,
                                              tmp_has_add_column, tmp_has_drop_column))) {
            } else {
              if (temp_is_modify_column_visibility) {
                is_modify_column_visibility = temp_is_modify_column_visibility;
                ++alter_column_visibility_times;
              }
              if (is_mysql_drop_column) {
                if (OB_FAIL(drop_col_act_position_list.push_back(i))) {
                }
              }
              if (!has_add_column) {
                has_add_column = tmp_has_add_column;
              }
              if (!has_drop_column) {
                has_drop_column = tmp_has_drop_column;
              }
              ++alter_column_times;
            }
            break;
          }
        //deal with add index drop index rename index
        case peg::NodeKind::IndexAdd:
        case peg::NodeKind::IndexDrop:
        case peg::NodeKind::IndexRename:
        case peg::NodeKind::IndexVisibility:
        case peg::NodeKind::IndexParallel:
        case peg::NodeKind::PrimaryKey:
        case peg::NodeKind::PrimaryKeyDrop: {
            // mysql corresponds to alter index
            bool is_add_index = false;
            alter_table_stmt->set_alter_table_index();
            if (OB_FAIL(resolve_index_options(actions, *action_node, is_add_index))) {
            } else if (is_add_index) {
              if (OB_FAIL(add_index_action_idxs.push_back(i))) {
              }
            }
            break;
          }
        case peg::NodeKind::Repartition:
        case peg::NodeKind::PartitionAdd:
        case peg::NodeKind::PartitionExchange:
        case peg::NodeKind::PartitionDrop:
        case peg::NodeKind::PartitionTruncate:
        case peg::NodeKind::SubpartitionAdd:
        case peg::NodeKind::SubpartitionDrop:
        case peg::NodeKind::SubpartitionTruncate: {
            has_alter_partition = true;
            alter_table_stmt->set_alter_table_partition();
            if (alter_table_stmt->get_alter_table_arg().is_alter_columns_) {
              ret = OB_NOT_SUPPORTED;
              LOG_USER_ERROR(
                OB_NOT_SUPPORTED,
                "specify alter_column_action and alter_partition_action in a single alter table stmt");
            } else if (OB_FAIL(resolve_partition_options(*action_node))) {
            }
            break;
          }
        // Process alter table add check constraint in the MySQL syntax path.
        case peg::NodeKind::CheckConstraint:
        case peg::NodeKind::CheckConstraintDrop: {
            if (OB_FAIL(resolve_constraint_options(*action_node, actions.size() > 1))) {
            }
            break;
          }
        case peg::NodeKind::AlterCheckConstraint: {
            alter_table_stmt->set_alter_table_index();
            OZ(resolve_constraint_state(*action_node));
            break;
          }
        case peg::NodeKind::ForeignKey:
        case peg::NodeKind::ForeignKeyDrop: {
            alter_table_stmt->set_alter_table_index();
            if (OB_FAIL(resolve_foreign_key_options(*action_node))) {
            }
            break;
          }
        case peg::NodeKind::DropConstraint: {
            // Drop check constraint or foreign key.
            ObString constraint_name;
            uint64_t constraint_id = OB_INVALID_ID;
            bool is_constraint = false; // indicates other constraints except foreign keys and unique keys
            bool is_foreign_key = false;
            ObSchemaGetterGuard *schema_guard = schema_checker_->get_schema_guard();

            const auto &name = peg::alter_constraint_name(*action_node);
            const auto name_text = name.text();
            constraint_name.assign_ptr(syntax_text(name_text).ptr(), static_cast<int64_t>(name_text.size()));
            if (constraint_name.empty()) { ret = OB_ERR_UNEXPECTED; }
            if (OB_SUCC(ret)) {
              if (!table_schema_->is_mysql_tmp_table()) {
                if (OB_FAIL(schema_guard->get_constraint_id(table_schema_->get_database_id(),
                                                            constraint_name,
                                                            constraint_id))) {
                } else {
                  is_constraint = OB_INVALID_ID != constraint_id;
                }
              } else { // tmp table in mysql mode
                ObTableSchema::const_constraint_iterator iter = table_schema_->constraint_begin();
                for (; OB_SUCC(ret) && iter != table_schema_->constraint_end(); ++iter) {
                  if (0 == constraint_name.case_compare((*iter)->get_constraint_name_str())) {
                    is_constraint = true;
                    break;
                  }
                }
              }
              if (OB_SUCC(ret)) { // drop foreign key
                // When dropping constraint check if the constraint type is foreign key or unique constraint
                if (OB_FAIL(schema_guard->get_foreign_key_id(table_schema_->get_database_id(),
                                                             constraint_name,
                                                             constraint_id))) {
                } else if (OB_INVALID_ID != constraint_id) {
                  if (is_constraint) {
                    ObString action("drop");
                    ret = OB_ERR_MULTIPLE_CONSTRAINTS_WITH_SAME_NAME;
                    LOG_USER_ERROR(OB_ERR_MULTIPLE_CONSTRAINTS_WITH_SAME_NAME,
                                   constraint_name.length(), constraint_name.ptr(),
                                   action.length(), action.ptr());
                  }
                  is_foreign_key = true;
                }
              }
            }
            if (OB_SUCC(ret)) {
              if (is_constraint) {
                if (OB_FAIL(resolve_constraint_options(*action_node, actions.size() > 1))) {
                }
              } else if(is_foreign_key) {
                alter_table_stmt->set_alter_table_index();
                if (OB_FAIL(resolve_foreign_key_options(*action_node))) {
                }
              } else {
                ret = OB_ERR_NONEXISTENT_CONSTRAINT;
                LOG_USER_ERROR(OB_ERR_NONEXISTENT_CONSTRAINT, constraint_name.length(), constraint_name.ptr());
                SQL_RESV_LOG(WARN,
                    "Cannot drop constraint  - nonexistent constraint",
                    K(ret),
                    K(*table_schema_),
                    K(constraint_name));
              }
            }
            break;
        }
        default: {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "Unknown alter table action %d", K(action_node->kind()), K(ret));
            /* won't be here */
            break;
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(check_skip_index(alter_table_stmt->get_alter_table_arg().alter_table_schema_))) {
    }
    //deal with drop column affer drop constraint (mysql mode)
    if (OB_SUCC(ret) && drop_col_act_position_list.count() > 0) {
      for (uint64_t i = 0; OB_SUCC(ret) && i < drop_col_act_position_list.count(); ++i) {
        if (OB_FAIL(resolve_drop_column(actions[drop_col_act_position_list.at(i)], reduced_visible_col_set, drop_column_names_set))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; OB_SUCC(ret) && i < add_index_action_idxs.count(); ++i) {
        const int64_t position = add_index_action_idxs.at(i);
        if (position < 0 || static_cast<size_t>(position) >= actions.size()) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          OZ(resolve_add_index(actions[position]));
        }
      }
    }
    if (OB_SUCC(ret)) {
      bool has_visible_col = false;
      bool has_invisible_col = false;
      bool has_hidden_gencol = false;
      ObColumnIterByPrevNextID iter(*table_schema_);
      const ObColumnSchemaV2 *column_schema = NULL;
      while (OB_SUCC(ret) && OB_SUCC(iter.next(column_schema)) && !has_visible_col) {
        if (OB_ISNULL(column_schema)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "The column is null", K(ret));
        } else if (column_schema->is_shadow_column()) {
          // skip shadow column
          continue;
        } else if (column_schema->is_invisible_column()) {
          // skip invisible column
          if (!has_invisible_col) {
            // Check if the invisible_col been dropped.
            ObColumnNameHashWrapper col_key(column_schema->get_column_name_str());
            if (OB_HASH_NOT_EXIST == reduced_visible_col_set.exist_refactored(col_key)) {
              has_invisible_col = true;
              ret = OB_SUCCESS; // change ret from OB_HASH_NOT_EXIST to OB_SUCCESS
            } else {            // OB_HASH_EXIST
              ret = OB_SUCCESS; // change ret from OB_HASH_EXIST to OB_SUCCESS
            }
          }
          continue;
        } else if (column_schema->is_hidden()) {
          // skip hidden column
          has_hidden_gencol |= column_schema->is_virtual_generated_column();
          continue;
        } else { // is visible column
          ObColumnNameHashWrapper col_key(column_schema->get_column_name_str());
          if (OB_HASH_NOT_EXIST == reduced_visible_col_set.exist_refactored(col_key)) {
            has_visible_col = true;
            ret = OB_SUCCESS; // change ret from OB_HASH_NOT_EXIST to OB_SUCCESS
          } else { // OB_HASH_EXIST
            ret = OB_SUCCESS; // change ret from OB_HASH_EXIST to OB_SUCCESS
          }
        }
      }
      if (OB_FAIL(ret) && OB_ITER_END != ret) {
        SQL_RESV_LOG(WARN, "failed to check column visibility", K(ret));
        if (NULL != column_schema) {
          SQL_RESV_LOG(WARN, "failed column schema", K(*column_schema),
                                                      K(column_schema->is_hidden()));
        }
      } else {
        ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret) && !has_visible_col) {
        share::schema::AlterTableSchema &alter_table_schema =
          alter_table_stmt->get_alter_table_arg().alter_table_schema_;
        bool has_add_visible_col = false;
        bool has_add_invisible_col = false;

        for (ObTableSchema::const_column_iterator it = alter_table_schema.column_begin();
              OB_SUCC(ret) && it < alter_table_schema.column_end()
              && (!has_add_visible_col || !has_add_invisible_col);
              it++) {
          const AlterColumnSchema *alter_column_schema = nullptr;
          if (OB_ISNULL(alter_column_schema = static_cast<AlterColumnSchema *>(*it))) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            if (alter_column_schema->alter_type_ != OB_DDL_ADD_COLUMN &&
                alter_column_schema->alter_type_ != OB_DDL_MODIFY_COLUMN) {
              continue;
            } else if (alter_column_schema->is_invisible_column()) {
              has_add_invisible_col = true;
            } else {
              has_add_visible_col = true;
            }
          }
        }
        if (!has_add_visible_col) {
          has_invisible_col |= has_add_invisible_col;
          if (has_invisible_col || has_hidden_gencol) {
            // If there has invisible column after alter, use this error code.
            ret = OB_ERR_ONLY_HAVE_INVISIBLE_COL_IN_TABLE;
            SQL_RESV_LOG(WARN, "table must have at least one column that is not invisible",
                          K(ret));
          } else {
            // RootServer will handle the case there is no visible column or invisible column,
            // it will use OB_CANT_REMOVE_ALL_FIELDS.
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (alter_table_stmt->get_alter_table_action_count() > 1) {
        if(0 != alter_table_stmt->get_foreign_key_arg_list().count()) {
          // suppored in mysql
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Add/modify foreign key together with other DDLs");
        }
      }
    }
    LOG_DEBUG("check add/modify cst allowed", K(alter_table_stmt->get_alter_table_action_count()), K(add_or_modify_check_cst_times_),
              K(add_not_null_constraint_), K(add_column_cnt_));
    const AlterTableSchema &table_schema = alter_table_stmt->get_alter_table_arg().alter_table_schema_;
    if (OB_SUCC(ret)) {
      if (alter_table_stmt->get_alter_table_action_count() > 1) {
        if (0 != add_or_modify_check_cst_times_) {
          // suppored in mysql
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "Add/modify constraint together with other DDLs");
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(check_alter_column_schemas_valid(*alter_table_stmt))) {
    }

    if (OB_SUCC(ret)) {
      ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
      if (OB_ISNULL(alter_table_stmt) || OB_ISNULL(table_schema_)) {
        ret = OB_ERR_UNEXPECTED;
      // Keep the alter algorithm correct: after 4352, add/drop column can be
      // instant, but compound DDL is offline.
      } else if (has_add_column
                 && has_drop_column) {
        alter_table_stmt->get_alter_table_arg().alter_algorithm_ = obcall::ObAlterTableArg::AlterAlgorithm::INPLACE;
      }
    }
    } // end for heap_vars_2.
  }
  return ret;
}

int ObAlterTableResolver::resolve_column_options(const peg::Node &node,
                                                 bool &is_modify_column_visibility,
                                                 bool &is_mysql_drop_column,
                                                 ObColumnNameSet &add_column_names_set,
                                                 ObReducedVisibleColSet &reduced_visible_col_set,
                                                 ObReducedVisibleColSet &drop_column_names_set,
                                                 bool &has_add_column,
                                                 bool &has_drop_column)
{
  int ret = OB_SUCCESS;
  has_add_column = node.kind() == peg::NodeKind::ColumnAdd;
  has_drop_column = node.kind() == peg::NodeKind::ColumnDrop;
  if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    switch (node.kind()) {
      case peg::NodeKind::ColumnAdd:
        OZ(resolve_add_column(node, add_column_names_set));
        for (const auto &storage : peg::alter_column_storage(node)) {
          OZ(resolve_json_storage(storage));
        }
        break;
      case peg::NodeKind::ColumnAlter: OZ(resolve_alter_column(node)); break;
      case peg::NodeKind::ColumnChange: OZ(resolve_change_column(node)); break;
      case peg::NodeKind::ColumnRename: OZ(resolve_rename_column(node)); break;
      case peg::NodeKind::ColumnModify:
        OZ(resolve_modify_column(node, is_modify_column_visibility, reduced_visible_col_set)); break;
      case peg::NodeKind::ColumnDrop: is_mysql_drop_column = true; break;
      case peg::NodeKind::TableForce:
        if (!check_can_drop_column_instant()) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "the current data format version does not support alter table force");
        } else {
          get_alter_table_stmt()->get_alter_table_arg().ddl_task_type_ = share::ObDDLTaskType::DELETE_COLUMN_FROM_SCHEMA;
        }
        break;
      default: ret = OB_ERR_UNEXPECTED; break;
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_index_column_list(rust::Slice<const peg::Node> columns,
                                                    obcall::ObCreateIndexArg &index_arg,
                                                    const int64_t index_name_value,
                                                    ObIArray<ObString> &input_index_columns_name,
                                                    bool &cnt_func_index)
{
  int ret = OB_SUCCESS;
  {
    obcall::ObColumnSortItem sort_item;
    //reset sort column set
    sort_column_array_.reset();
    cnt_func_index = false;
    for (int32_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
      const auto &column = columns[i];
      const auto &expression = peg::index_column_expression(column);
      {
        sort_item.reset();
        sort_item.is_func_index_ = expression.kind() != peg::NodeKind::Ident;
        cnt_func_index |= sort_item.is_func_index_;
        const auto expression_text = expression.text();
        const ObString name = sort_item.is_func_index_ ? params_.syntax_->source(expression)
            : syntax_text(expression_text);
        OZ(ob_write_string(*allocator_, name, sort_item.column_name_));
        bool is_multi_value_index = false;
        OZ(share::ObMulValueIndexBuilderUtil::adjust_index_type(sort_item.column_name_,
            is_multi_value_index, reinterpret_cast<int*>(&index_keyname_)));
        if (OB_FAIL(ret)) {
          //do nothing
        } else if (peg::index_column_has_prefix(column)) {
          sort_item.prefix_len_ = static_cast<int32_t>(peg::index_column_prefix(column));
          //can't not be zero
          if (0 == sort_item.prefix_len_) {
            ret = OB_KEY_PART_0;
            LOG_USER_ERROR(OB_KEY_PART_0, sort_item.column_name_.length(), sort_item.column_name_.ptr());
            SQL_RESV_LOG(WARN, "Key part length cannot be 0", K(sort_item), K(ret));
          }
        } else {
          sort_item.prefix_len_ = 0;
        }
        if (OB_SUCC(ret)) {
          const ObColumnSchemaV2 *column_schema = NULL;
          if (OB_NOT_NULL(column_schema = table_schema_->get_column_schema(sort_item.column_name_))) {
            if (ob_is_collection_sql_type(column_schema->get_data_type()) && index_keyname_ != VEC_KEY) {
              ret = OB_NOT_SUPPORTED;
              LOG_USER_ERROR(OB_NOT_SUPPORTED, "create index on vector column is");
            }
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
          } else if (OB_FAIL(resolve_fts_index_constraint(*table_schema_,
                                                          sort_item.column_name_,
                                                          index_name_value))) {
          }
        } else if (index_keyname_ == VEC_KEY) {
          // TODO@xiajin
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "alter table to modify vector index is");
        } else { // spatial index, NOTE resolve_spatial_index_constraint() will set index_keyname
          ObSEArray<ObColumnSchemaV2 *, 8> resolved_cols;
          ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
          bool is_explicit_order = peg::index_column_has_direction(column);
          if (OB_ISNULL(alter_table_stmt)) {
            ret = OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
          } else if (OB_FAIL(get_table_schema_all_column_schema(resolved_cols, alter_table_stmt->get_alter_table_schema()))) {
          } else if (OB_FAIL(resolve_spatial_index_constraint(*table_schema_, sort_item.column_name_,
              columns.size(), index_name_value, is_explicit_order, sort_item.is_func_index_ ? &expression : nullptr, &resolved_cols))) {
          }
        }

        //column_order
        if (OB_FAIL(ret)) {
          //do nothing
        } else {
          // Compatible with mysql5.7, descending index does not take effect and no error is reported
          sort_item.order_type_ = common::ObOrderType::ASC;
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(add_sort_column(sort_item, index_arg))){
          } else { /*do nothing*/ }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(input_index_columns_name.push_back(sort_item.column_name_))) {
          }
        }
      }
    }
  }
  return ret;
}


int ObAlterTableResolver::add_sort_column(const obcall::ObColumnSortItem &sort_column,
                                          obcall::ObCreateIndexArg &index_arg)
{
  int ret = OB_SUCCESS;
  const ObString &column_name = sort_column.column_name_;
  ObColumnNameWrapper column_key(column_name, sort_column.prefix_len_);
  bool check_prefix_len = false;
  if (is_column_exists(sort_column_array_, column_key, check_prefix_len)) {
    ret = OB_ERR_COLUMN_DUPLICATE;    //index (c1,c1) or index (c1(3), c1 (6))
    LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, column_name.length(), column_name.ptr());
  } else if (OB_FAIL(sort_column_array_.push_back(column_key))) {
  } else if (OB_FAIL(index_arg.index_columns_.push_back(sort_column))) {
  }
  return ret;
}

int ObAlterTableResolver::get_table_schema_for_check(const ObTableSchema *&table_schema)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  const ObTableSchema *tmp_table_schema = NULL;
  if (OB_ISNULL(alter_table_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
  } else if (OB_FAIL(schema_checker_->get_table_schema(
                                                alter_table_stmt->get_org_database_name(),
                                                alter_table_stmt->get_org_table_name(),
                                                false/*not index table*/,
                                                tmp_table_schema))) {
    if (OB_TABLE_NOT_EXIST == ret) {
      ObCStringHelper helper;
      LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(alter_table_stmt->get_org_database_name()),
                     helper.convert(alter_table_stmt->get_org_table_name()));
    }
  } else if (OB_ISNULL(tmp_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    table_schema = tmp_table_schema;
  }
  return ret;
}

int ObAlterTableResolver::resolve_add_index(const peg::Node &node)
{
  int ret = OB_SUCCESS;

  {
    reset_index();
    const auto definition = peg::index_definition(node);
    const bool is_unique_key = definition.kind == peg::IndexKind::Unique;
    const peg::Node *index_name_node = definition.has_name ? &peg::index_definition_name(node) : nullptr;
    bool is_index_part_specified = false;
    ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(alter_table_stmt)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
    } else {
      sort_column_array_.reset();
      storing_column_set_.reset();
      index_keyname_ = index_keyname(definition.kind);

      //column_list node should be parse first in case the index name is not specified
      if (OB_SUCC(ret)) {
        HEAP_VAR(ObCreateIndexStmt, create_index_stmt ,allocator_) {
          obcall::ObCreateIndexArg *create_index_arg = NULL;
          void *tmp_ptr = NULL;
          ObSEArray<ObString, 8> input_index_columns_name;
          bool cnt_func_index = false;
          if (NULL == (tmp_ptr = (ObCreateIndexArg *)allocator_->alloc(
                  sizeof(obcall::ObCreateIndexArg)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            SQL_RESV_LOG(ERROR, "failed to allocate memory", K(ret));
          } else {
            create_index_arg = new (tmp_ptr) ObCreateIndexArg();
            if (OB_FAIL(resolve_index_column_list(definition.columns,
                                                         *create_index_arg,
                                                         static_cast<int64_t>(index_keyname(definition.kind)),
                                                         input_index_columns_name,
                                                         cnt_func_index))) {
            }
          }
          if (OB_SUCC(ret)) {
            if (definition.method == peg::IndexMethod::Hash) {
              if (SPATIAL_KEY == index_keyname_) {
                ret = OB_ERR_INDEX_TYPE_NOT_SUPPORTED_FOR_SPATIAL_INDEX;
                LOG_USER_ERROR(OB_ERR_INDEX_TYPE_NOT_SUPPORTED_FOR_SPATIAL_INDEX, "HASH");
              } else {
                create_index_arg->index_using_type_ = USING_HASH;
              }
            }
          }

          if (OB_SUCC(ret)) {
            if (OB_ISNULL(index_name_node)) {
              // create_index_arg->index_name_.reset();
              // generate index name
              ObString index_name;
              if (input_index_columns_name.count() < 1) {
                ret = OB_ERR_UNEXPECTED;
                SQL_RESV_LOG(WARN, "size of index columns is less than 1", K(ret));
              } else {
                ObString first_column_name = cnt_func_index ? ObString::make_string("functional_index") : input_index_columns_name.at(0);
                if (OB_FAIL(generate_index_name(index_name, current_index_name_set_, first_column_name))) {
                }
                if (OB_SUCC(ret)) {
                  ObIndexNameHashWrapper index_key(index_name);
                  if (OB_FAIL(current_index_name_set_.set_refactored(index_key))) {
                  } else if (OB_FAIL(ob_write_string(
                          *allocator_,
                          index_name,
                          create_index_arg->index_name_))) {
                  } else {
                    create_index_arg->index_schema_.set_name_generated_type(GENERATED_TYPE_SYSTEM);
                  }
                }
              }
            } else {
              {
                const auto index_name_node_text = index_name_node->text();
                OZ(ob_write_string(*allocator_, syntax_text(index_name_node_text), create_index_arg->index_name_));
                ObCollationType cs_type = CS_TYPE_INVALID;
                if (OB_FAIL(ret)) {
                } else if (OB_UNLIKELY(NULL == session_info_)) {
                  ret = OB_ERR_UNEXPECTED;
                } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
                } else if (OB_FAIL(ObSQLUtils::check_index_name(cs_type, create_index_arg->index_name_))) {
                } else {
                  create_index_arg->index_schema_.set_name_generated_type(GENERATED_TYPE_USER);
                }
              }
            }

            if (OB_SUCC(ret)) {
              if (!definition.options.empty()) {
                has_index_using_type_ = false;
                if (OB_FAIL(resolve_table_options(definition.options, true))) {
                } else if (has_index_using_type_) {
                  create_index_arg->index_using_type_ = index_using_type_;
                }
              } 
            }
            if (OB_SUCC(ret)) {
              if (definition.has_partition) {
                if (LOCAL_INDEX == index_scope_) {
                  ret = OB_NOT_SUPPORTED;
                  LOG_USER_ERROR(OB_NOT_SUPPORTED, "Specify partition option of local index");
                } else if (NOT_SPECIFIED == index_scope_) {
                  index_scope_ = GLOBAL_INDEX;
                }
                is_index_part_specified = true;
              }
            }

            if (OB_SUCC(ret)) {
              create_index_arg->sql_mode_ = session_info_->get_sql_mode();
              if (OB_FAIL(ObLocalSessionVarHelper::load_session_vars(session_info_, create_index_arg->local_session_var_))) {
              }
            }
            if (OB_SUCC(ret)) {
              if (OB_FAIL(generate_index_arg(*create_index_arg, is_unique_key))) {
              } else if (table_schema_->is_partitioned_table()
                         && INDEX_TYPE_SPATIAL_GLOBAL == create_index_arg->index_type_) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "spatial global index");
              } else if (share::schema::is_fts_index(create_index_arg->index_type_)
                  && OB_FAIL(share::ObFtsIndexBuilderUtil::generate_fts_parser_name_and_property(*table_schema_, *create_index_arg, allocator_))) {
              } else {
                create_index_arg->index_schema_.set_table_type(USER_INDEX);
                create_index_arg->index_schema_.set_index_type(create_index_arg->index_type_);
                
                if (OB_FAIL(create_index_stmt.get_create_index_arg().assign(*create_index_arg))) {
                }
              }
            }
            if (OB_SUCC(ret)) {
              ObSArray<ObPartitionResolveResult> &resolve_results = alter_table_stmt->get_index_partition_resolve_results();
              ObSArray<obcall::ObCreateIndexArg*> &index_arg_list = alter_table_stmt->get_index_arg_list();
              ObPartitionResolveResult resolve_result;
              ObCreateIndexArg &index_arg = create_index_stmt.get_create_index_arg();
              if (is_index_part_specified) {
                ObTableSchema &index_schema = index_arg.index_schema_;
                SMART_VAR(ObCreateIndexArg, my_create_index_arg) {
                  SMART_VAR(ObTableSchema, new_table_schema) {
                    ObArray<ObColumnSchemaV2 *> gen_columns;
                    if (OB_FAIL(new_table_schema.assign(*table_schema_))) {
                    } else if (OB_FAIL(my_create_index_arg.assign(index_arg))) {
                    } else if (OB_FAIL(share::ObIndexBuilderUtil::adjust_expr_index_args(
                            my_create_index_arg, new_table_schema, *allocator_, gen_columns))) {
                    } else if (OB_FAIL(share::ObIndexBuilderUtil::set_index_table_columns(
                              my_create_index_arg, new_table_schema, my_create_index_arg.index_schema_, false))) {
                    } else if (OB_FAIL(index_schema.assign(my_create_index_arg.index_schema_))){
                    }
                  }
                }
                if (OB_FAIL(ret)) {
                } else if (OB_FAIL(resolve_index_partition_node(peg::index_definition_partition(node), &create_index_stmt))) {
                } else {
                  resolve_result.get_part_fun_exprs() = create_index_stmt.get_part_fun_exprs();
                  resolve_result.get_part_values_exprs() = create_index_stmt.get_part_values_exprs();
                  resolve_result.get_subpart_fun_exprs() = create_index_stmt.get_subpart_fun_exprs();
                  resolve_result.get_template_subpart_values_exprs() = create_index_stmt.get_template_subpart_values_exprs();
                  resolve_result.get_individual_subpart_values_exprs() = create_index_stmt.get_individual_subpart_values_exprs();
                }
              }
              if (OB_SUCC(ret)) {
                if (OB_FAIL(create_index_arg->assign(index_arg))) {
                }
                if (OB_SUCC(ret)) {
                  create_index_arg->index_key_ = static_cast<int64_t>(index_keyname_);
                  if (OB_FAIL(share::ObFtsIndexBuilderUtil::check_supportability_for_building_index(table_schema_, create_index_arg))) {
                  } else if (OB_FAIL(resolve_results.push_back(resolve_result))) {
                  } else if (OB_FAIL(index_arg_list.push_back(create_index_arg))) {
                  }
                }
              }
            }
            if (OB_SUCC(ret)) {
              storing_column_set_.reset();  //storing column for each index
              sort_column_array_.reset();   //column for each index
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_add_constraint(const peg::Node &node)
{
  int ret = OB_SUCCESS;

  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  AlterTableSchema &alter_table_schema = alter_table_stmt->get_alter_table_arg().alter_table_schema_;
  ObSEArray<ObConstraint, 4> csts;
  for (ObTableSchema::const_constraint_iterator iter = alter_table_schema.constraint_begin(); OB_SUCC(ret) &&
    iter != alter_table_schema.constraint_end(); iter ++) {
    ret = csts.push_back(**iter);
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(resolve_check_constraint_node(node, csts))) {
    } else if (OB_FAIL(alter_table_schema.add_constraint(csts.at(csts.count() - 1)))) {
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_add_partition(const peg::Node &node,
                                                const ObTableSchema &orig_table_schema)
{
  int ret = OB_SUCCESS;
  AlterTableSchema &alter_table_schema = get_alter_table_stmt()->get_alter_table_arg().alter_table_schema_;
  ObTableStmt *alter_stmt = get_alter_table_stmt();
  ObSEArray<ObString, 8> dummy_part_keys;
  const ObPartitionOption &part_option = orig_table_schema.get_part_option();
  const ObPartitionFuncType part_func_type = part_option.get_part_func_type();
  const auto elements = peg::alter_partition_additions(node);
  alter_table_schema.set_part_level(orig_table_schema.get_part_level());
  if (OB_FAIL(resolve_stored_partition_function(orig_table_schema, false,
      alter_stmt->get_part_fun_exprs(), dummy_part_keys))) {
  } else if (share::schema::PARTITION_LEVEL_ONE == orig_table_schema.get_part_level()) {
    // First-level partitioned table with first-level partitioning
    for (int64_t i = 0; OB_SUCC(ret) && i < elements.size(); ++i) {
      if (!peg::partition_element(elements[i]).subpartitions.empty()) {
        ret = OB_ERR_NOT_COMPOSITE_PARTITION;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(inner_add_partition(elements, part_func_type, part_option,
                                      alter_stmt, alter_table_schema))) {
      }
    }
  } else {
    // Non-template secondary partition table plus one-level partition, supports user-defined secondary partitions under one-level partition
    // 1. no_subpart == true: subpart info is the template of table.
    // 2. no_subpart == false: subpart info is specified by clause.
    bool no_subpart = false;
    if (OB_SUCC(ret)) {
      no_subpart = peg::partition_element(elements[0]).subpartitions.empty();
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < elements.size(); ++i) {
      if ((!no_subpart && peg::partition_element(elements[i]).subpartitions.empty())
          || (no_subpart && !peg::partition_element(elements[i]).subpartitions.empty())) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "Add partition with subpartition and add another partition without subpartition ");
      }
    }

    // In ddl_service, we will use alter_table_schema.sub_part_template_def_valid()
    // to determine whether to set new_table_schema sub_part_template_def as valid.
    if (OB_FAIL(ret)) {
    } else if (orig_table_schema.sub_part_template_def_valid()) {
      alter_table_schema.set_sub_part_template_def_valid();
    } else {
      alter_table_schema.unset_sub_part_template_def_valid();
    }

    if (OB_FAIL(ret)) {
    } else if (no_subpart && orig_table_schema.has_sub_part_template_def()) {
      bool generated = false;
      if (OB_FAIL(inner_add_partition(elements, part_func_type, part_option,
                 alter_stmt, alter_table_schema))) {
      } else if (OB_FAIL(alter_table_schema.try_assign_def_subpart_array(orig_table_schema))) {
      } else if (OB_FAIL(alter_table_schema.try_generate_subpart_by_template(generated))) {
      } else if (!generated) {
        ret = OB_ERR_UNEXPECTED;
      }
      alter_stmt->set_use_def_sub_part(false);
    } else {
      if (!no_subpart && orig_table_schema.has_sub_part_template_def()) {
        // add partition to subpartition template table with subpartition info specified by clause
        // set sub_part_template_def to invalid, which is only used for schema printer
        alter_table_schema.unset_sub_part_template_def_valid();
      }
      const ObSubPartitionOption &subpart_option = orig_table_schema.get_sub_part_option();
      const ObPartitionFuncType subpart_type = subpart_option.get_part_func_type();
      alter_stmt->set_use_def_sub_part(false);
      // First set up sub part option, parsing the definition of secondary partition depends on
      alter_table_schema.get_sub_part_option() = orig_table_schema.get_sub_part_option();
      alter_table_schema.get_part_option() = orig_table_schema.get_part_option();
      if (no_subpart && orig_table_schema.is_hash_like_subpart()) {
        hash_subpart_num_ = 1;
      }
      /* set subpartition key info */
      OZ (alter_table_schema.assign_subpartition_key_info(
                            orig_table_schema.get_subpartition_key_info()));
      OZ(resolve_stored_partition_function(orig_table_schema, true,
          alter_stmt->get_subpart_fun_exprs(), dummy_part_keys));
      OZ (inner_add_partition(elements, part_func_type, part_option,
                              alter_stmt, alter_table_schema));
      if (OB_SUCC(ret) && no_subpart) {
        const int64_t part_num = alter_table_schema.get_partition_num();
        ObPartition **part_array = alter_table_schema.get_part_array();
        if (OB_ISNULL(part_array) || part_num <= 0) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          const int64_t BUF_SIZE = OB_MAX_PARTITION_NAME_LENGTH;
          char buf[BUF_SIZE];
          ObSubPartition *subpart;
          for (int64_t i = 0; i < part_num && OB_SUCC(ret); ++i) {
            ObPartition *part = part_array[i];
            MEMSET(buf, 0, BUF_SIZE);
            int64_t pos = 0;
            ObString sub_part_name;
            if (OB_ISNULL(part) || OB_ISNULL(part->get_subpart_array()) ||
               OB_ISNULL(subpart = part->get_subpart_array()[0])) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(databuff_printf(buf, BUF_SIZE, pos, "%s%s",
                      part->get_part_name().ptr(), "sp0"))) {
            } else if (FALSE_IT(sub_part_name.assign_ptr(buf, static_cast<int32_t>(strlen(buf))))) {
            } else if (OB_FAIL(subpart->set_part_name(sub_part_name))) {
            } else {
              subpart->set_is_empty_partition_name(false);
            }
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    LOG_DEBUG("succ to resolve partition elements", KPC(alter_stmt),
          K(alter_stmt->get_part_fun_exprs()),
          K(alter_stmt->get_part_values_exprs()),
          K(alter_stmt->get_subpart_fun_exprs()),
          K(alter_stmt->get_individual_subpart_values_exprs()));

    alter_table_schema.get_part_option() = orig_table_schema.get_part_option();
    alter_table_schema.get_part_option().set_part_num(alter_table_schema.get_partition_num());
    // check part_name and subpart_name duplicate
    if (OB_FAIL(check_and_set_partition_names(alter_stmt, alter_table_schema, false))) {
    } else if (PARTITION_LEVEL_TWO == orig_table_schema.get_part_level()) {
      if (OB_FAIL(check_and_set_individual_subpartition_names(alter_stmt, alter_table_schema))) {
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::inner_add_partition(rust::Slice<const peg::Node> elements,
                                              const ObPartitionFuncType part_type,
                                              const ObPartitionOption &part_option,
                                              ObTableStmt *alter_stmt,
                                              ObTableSchema &alter_table_schema)
{
  int ret = OB_SUCCESS;
  if (elements.empty() || OB_ISNULL(alter_stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (part_option.is_range_part()) {
    if (elements[0].kind() == peg::NodeKind::ListPartitionElement) {
      ret = OB_ERR_PARSER_SYNTAX;
    } else if (OB_FAIL(resolve_range_partition_elements(alter_stmt,
                                                        elements,
                                                        alter_table_schema,
                                                        part_type,
                                                        alter_stmt->get_part_fun_exprs(),
                                                        alter_stmt->get_part_values_exprs()))) {
    }
  } else if (part_option.is_list_part()) {
    if (elements[0].kind() == peg::NodeKind::RangePartitionElement) {
      ret = OB_ERR_PARTITION_NOT_EXPECT_VALUES_LESS_THAN;
    } else if (OB_FAIL(resolve_list_partition_elements(alter_stmt,
                                                       elements,
                                                       alter_table_schema,
                                                       part_type,
                                                       alter_stmt->get_part_fun_exprs(),
                                                       alter_stmt->get_part_values_exprs()))) {
    }
  } else if (part_option.is_hash_like_part()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Add hash partition");
  }
  return ret;
}


int ObAlterTableResolver::resolve_stored_partition_function(const ObTableSchema &table_schema,
                                              const bool is_sub_part,
                                              ObIArray<ObRawExpr *> &expressions,
                                              ObIArray<ObString> &keys)
{
  int ret = OB_SUCCESS;
  const ObPartitionOption &option = is_sub_part ? table_schema.get_sub_part_option()
                                               : table_schema.get_part_option();
  const ObString &text = option.get_part_func_expr_str();
  const ObPartitionFuncType kind = option.get_part_func_type();
  ObSqlString expression_text;
  if (is_inner_table(table_schema.get_table_id())) {
    OZ(expression_text.append_fmt("partition_%.*s", text.length(), text.ptr()));
  } else if (kind == PARTITION_FUNC_TYPE_KEY) {
    OZ(expression_text.append_fmt("%s(%.*s)", N_PART_KEY, text.length(), text.ptr()));
  } else if (kind == PARTITION_FUNC_TYPE_HASH) {
    OZ(expression_text.append_fmt("%s(%.*s)", N_PART_HASH, text.length(), text.ptr()));
  } else {
    OZ(expression_text.append_fmt("(%.*s)", text.length(), text.ptr()));
  }
  ParsedSql syntax;
  ObParser parser(*allocator_, params_.session_info_->get_sql_mode());
  if (OB_SUCC(ret) && OB_FAIL(parser.parse_expression(expression_text.string(), syntax))) {
    ret = OB_ERR_PARSE_SQL;
  }
  if (OB_SUCC(ret)) {
    const peg::Node *expression = syntax.statement();
    while (expression->kind() == peg::NodeKind::Parenthesized) {
      expression = &peg::parenthesized_expression(*expression);
    }
    const ParsedSql *saved_syntax = params_.syntax_;
    params_.syntax_ = &syntax;
    ret = resolve_part_func(params_, *expression, kind, table_schema, expressions, keys);
    params_.syntax_ = saved_syntax;
  }
  return ret;
}

int ObAlterTableResolver::generate_index_arg(obcall::ObCreateIndexArg &index_arg, const bool is_unique_key)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  if (OB_ISNULL(session_info_) || OB_ISNULL(alter_table_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "session info should not be null", K(session_info_), K(alter_table_stmt));
  } else {
    //add storing column
    for (int32_t i = 0; OB_SUCC(ret) && i < store_column_names_.count(); ++i) {
      if (OB_FAIL(index_arg.store_columns_.push_back(store_column_names_.at(i)))) {
      }
    }
    for (int32_t i = 0; OB_SUCC(ret) && i < hidden_store_column_names_.count(); ++i) {
      if (OB_FAIL(index_arg.hidden_store_columns_.push_back(hidden_store_column_names_.at(i)))) {
      }
    }
    
    index_arg.table_name_ = alter_table_stmt->get_alter_table_arg().alter_table_schema_.get_origin_table_name();
    index_arg.database_name_ = alter_table_stmt->get_alter_table_arg().alter_table_schema_.get_origin_database_name();
    //set index option
    index_arg.index_option_.block_size_ = block_size_;
    index_arg.index_option_.compress_method_ = compress_method_;
    index_arg.index_option_.row_store_type_ = row_store_type_;
    index_arg.index_option_.store_format_ = store_format_;
    index_arg.index_option_.comment_ = comment_;
    index_arg.with_rowid_ = with_rowid_;
    index_arg.index_option_.parser_name_ = parser_name_;
    index_arg.index_option_.parser_properties_ = parser_properties_;
    if (OB_SUCC(ret)) {
      ObIndexType type = INDEX_TYPE_IS_NOT;
      if (NOT_SPECIFIED == index_scope_) {
        // Default index mode is local.
        global_ = false;
      } else {
        global_ = (GLOBAL_INDEX == index_scope_);
      }
      if (OB_FAIL(ret)) {
      } else if (is_unique_key) {
        if (global_) {
          type = INDEX_TYPE_UNIQUE_GLOBAL;
        } else {
          type = INDEX_TYPE_UNIQUE_LOCAL;
        }

        if (index_keyname_ == MULTI_KEY || index_keyname_ == MULTI_UNIQUE_KEY) {
          type = INDEX_TYPE_UNIQUE_MULTIVALUE_LOCAL;
          if (global_) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "global multivalue index");
          }
        }
      } else {
        if (global_) {
          if (index_keyname_ == SPATIAL_KEY) {
            type = INDEX_TYPE_SPATIAL_GLOBAL;
          } else if (index_keyname_ == FTS_KEY) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "global fulltext index is");
          } else if (index_keyname_ == MULTI_KEY) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "multivalue index");
          } else {
            type = INDEX_TYPE_NORMAL_GLOBAL;
          }
        } else {
          if (index_keyname_ == SPATIAL_KEY) {
            type = INDEX_TYPE_SPATIAL_LOCAL;
          } else if (index_keyname_ == FTS_KEY) {
            type = INDEX_TYPE_FTS_INDEX_LOCAL;
          } else if (index_keyname_ == MULTI_KEY) {
            type = INDEX_TYPE_NORMAL_MULTIVALUE_LOCAL;
          } else {
            type = INDEX_TYPE_NORMAL_LOCAL;
          }
        }
      }
      index_arg.index_type_ = type;
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_drop_index(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  {
    {
      const auto &name = peg::alter_index_name(node);
      ObString drop_index_name;
      const auto name_text = name.text();
      OZ(ob_write_string(*allocator_, syntax_text(name_text), drop_index_name));
      //construct ObDropIndexArg
      ObDropIndexArg *drop_index_arg = NULL;
      void *tmp_ptr = NULL;
      if (OB_FAIL(ret)) {
      } else if (NULL == (tmp_ptr = (ObDropIndexArg *)allocator_->alloc(sizeof(obcall::ObDropIndexArg)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SQL_RESV_LOG(ERROR, "failed to allocate memory", K(ret));
      } else {
        drop_index_arg = new (tmp_ptr)ObDropIndexArg();
        
        
        drop_index_arg->index_name_ = drop_index_name;
      }
      //push drop index arg
      if (OB_SUCC(ret)) {
        ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
        // Delete the index column when you need to check if the index-related columns are foreign key columns, if they are foreign key columns then deletion is not allowed, including cases:
        // 1. The main table of the index is the parent table
        // 2. The main table of the index is the subtable
        // When fetching foreign key info, it will bring over the foreign key info of parent/child tables
        if (OB_ISNULL(alter_table_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
        } else if (table_schema_->is_parent_table() || table_schema_->is_child_table()) {
          const ObTableSchema *index_table_schema = NULL;
          ObString index_table_name;
          ObArenaAllocator allocator(ObModIds::OB_SCHEMA);
          bool has_other_indexes_on_same_cols = false;
          if (OB_FAIL(ObTableSchema::build_index_table_name(allocator,
                                                            table_schema_->get_table_id(),
                                                            drop_index_name,
                                                            index_table_name))) {
          } else if (OB_FAIL(schema_checker_->get_table_schema(
                                                               alter_table_stmt->get_org_database_name(),
                                                               index_table_name,
                                                               true /* index table */,
                                                               index_table_schema))) {
            if (OB_TABLE_NOT_EXIST == ret) {
              ret = OB_ERR_CANT_DROP_FIELD_OR_KEY;
              LOG_USER_ERROR(OB_ERR_CANT_DROP_FIELD_OR_KEY, drop_index_name.length(), drop_index_name.ptr());
            }
          } else if (OB_ISNULL(index_table_schema)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(check_indexes_on_same_cols(*table_schema_,
                                                        *index_table_schema,
                                                        *schema_checker_,
                                                        has_other_indexes_on_same_cols))) {
          } else if (!has_other_indexes_on_same_cols) {
            if (OB_FAIL(check_index_columns_equal_foreign_key(*table_schema_, *index_table_schema))) {
            }
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(alter_table_stmt->add_index_arg(drop_index_arg))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_drop_foreign_key(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  const auto &name = peg::alter_constraint_name(node);
  const auto name_text = name.text();
  ObDropForeignKeyArg *foreign_key_arg = NULL;
  ObString foreign_key_name;
  bool has_same_fk_arg = false;
  void *tmp_ptr = NULL;
  if (name_text.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(tmp_ptr = allocator_->alloc(sizeof(ObDropForeignKeyArg)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SQL_RESV_LOG(ERROR, "failed to allocate memory", K(ret));
  } else if (FALSE_IT(foreign_key_arg = new (tmp_ptr)ObDropForeignKeyArg())) {
  } else if (OB_FAIL(ob_write_string(*allocator_, syntax_text(name_text), foreign_key_arg->foreign_key_name_))) {
  } else if (OB_ISNULL(alter_table_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
  } else if (OB_FAIL(alter_table_stmt->check_drop_fk_arg_exist(foreign_key_arg, has_same_fk_arg))) {
  } else if (has_same_fk_arg) {
    // do nothing
  } else if (OB_FAIL(alter_table_stmt->add_index_arg(foreign_key_arg))) {
  }
  return ret;
}

int ObAlterTableResolver::resolve_drop_constraint(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *statement = get_alter_table_stmt();
  if (OB_ISNULL(statement)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (const auto &name : peg::alter_constraint_names(node)) {
      if (OB_FAIL(ret)) break;
      const auto name_text = name.text();
      ObString text(syntax_text(name_text));
      ObConstraint constraint;
      auto iter = table_schema_->constraint_begin();
      for (; OB_SUCC(ret) && iter != table_schema_->constraint_end(); ++iter) {
        if (0 == text.case_compare((*iter)->get_constraint_name_str())) {
          OZ(constraint.assign(**iter));
          break;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (iter == table_schema_->constraint_end()) {
        if (node.kind() == peg::NodeKind::DropConstraint) {
          ret = OB_ERR_NONEXISTENT_CONSTRAINT;
          LOG_USER_ERROR(OB_ERR_NONEXISTENT_CONSTRAINT, text.length(), text.ptr());
        } else {
          ret = OB_ERR_CHECK_CONSTRAINT_NOT_FOUND;
          LOG_USER_ERROR(OB_ERR_CHECK_CONSTRAINT_NOT_FOUND, text.length(), text.ptr());
        }
      } else {
        OZ(statement->get_alter_table_arg().alter_table_schema_.add_constraint(constraint));
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_constraint_state(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *statement = get_alter_table_stmt();
  const auto name_text = peg::alter_constraint_name(node).text();
  ObString name(syntax_text(name_text));
  const bool check_only = peg::alter_constraint_check_only(node);
  const bool enforced = peg::alter_constraint_enforced(node);
  ObConstraint constraint;
  auto iter = table_schema_->constraint_begin();
  for (; OB_SUCC(ret) && iter != table_schema_->constraint_end(); ++iter) {
    if (0 == name.case_compare((*iter)->get_constraint_name_str())) {
      OZ(constraint.assign(**iter));
      break;
    }
  }
  if (OB_SUCC(ret) && !check_only) {
    uint64_t foreign_key_id = OB_INVALID_ID;
    ObSchemaGetterGuard *schema_guard = schema_checker_->get_schema_guard();
    OZ(schema_guard->get_foreign_key_id(table_schema_->get_database_id(), name, foreign_key_id));
    if (OB_SUCC(ret) && foreign_key_id != OB_INVALID_ID) {
      uint64_t constraint_id = OB_INVALID_ID;
      OZ(schema_guard->get_constraint_id(table_schema_->get_database_id(), name, constraint_id));
      if (OB_FAIL(ret)) {
      } else if (constraint_id != OB_INVALID_ID) {
        ret = OB_ERR_MULTIPLE_CONSTRAINTS_WITH_SAME_NAME;
        ObString action("alter");
        LOG_USER_ERROR(OB_ERR_MULTIPLE_CONSTRAINTS_WITH_SAME_NAME,
                       name.length(), name.ptr(), action.length(), action.ptr());
      } else {
        ret = OB_ERR_ALTER_CONSTRAINT_ENFORCEMENT_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_ERR_ALTER_CONSTRAINT_ENFORCEMENT_NOT_SUPPORTED, name.length(), name.ptr());
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (iter == table_schema_->constraint_end()) {
      if (check_only) {
        ret = OB_ERR_CHECK_CONSTRAINT_NOT_FOUND;
        LOG_USER_ERROR(OB_ERR_CHECK_CONSTRAINT_NOT_FOUND, name.length(), name.ptr());
      } else {
        ret = OB_ERR_NONEXISTENT_CONSTRAINT;
        LOG_USER_ERROR(OB_ERR_NONEXISTENT_CONSTRAINT, name.length(), name.ptr());
      }
    } else {
      if (constraint.get_enable_flag() != enforced) {
        constraint.set_is_modify_enable_flag(true);
        constraint.set_enable_flag(enforced);
        constraint.set_is_modify_validate_flag(true);
        constraint.set_validate_flag(enforced ? CST_FK_VALIDATED : CST_FK_NO_VALIDATE);
      }
      OZ(statement->get_alter_table_arg().alter_table_schema_.add_constraint(constraint));
      if (OB_SUCC(ret)) {
        statement->get_alter_table_arg().alter_constraint_type_ = ObAlterTableArg::ALTER_CONSTRAINT_STATE;
        ++add_or_modify_check_cst_times_;
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_drop_partition(const peg::Node &node,
                                                 const ObTableSchema &orig_table_schema)
{
  int ret = OB_SUCCESS;
  if (orig_table_schema.is_hash_like_part()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Drop hash partition");
  } else {
    const auto names = peg::alter_partition_names(node);
    if (names.empty()) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "invalid parse tree", K(ret));
    } else {
      ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
      if (OB_ISNULL(alter_table_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
      }
      AlterTableSchema &alter_table_schema =
          alter_table_stmt->get_alter_table_arg().alter_table_schema_;
      for (int64_t i = 0; OB_SUCC(ret) && i < names.size(); ++i) {
        ObPartition part;
        const auto partition_text = names[i].text();
        ObString partition_name(syntax_text(partition_text));
        if (OB_FAIL(part.set_part_name(partition_name))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          SQL_RESV_LOG(ERROR, "set partition name failed", K(partition_name), K(ret));
        } else if (OB_FAIL(alter_table_schema.check_part_name(part))) {
        } else if (OB_FAIL(alter_table_schema.add_partition(part))) {
        }
      }
      if (OB_SUCC(ret)) {
        alter_table_schema.set_part_level(orig_table_schema.get_part_level());
        alter_table_schema.get_part_option() = orig_table_schema.get_part_option();
        alter_table_schema.get_part_option().set_part_num(
            alter_table_schema.get_partition_num());
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_add_subpartition(const peg::Node &node,
                                                   const ObTableSchema &orig_table_schema)
{
  int ret = OB_SUCCESS;
  ObTableStmt *alter_stmt = get_alter_table_stmt();
  if (OB_ISNULL(alter_stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (share::schema::PARTITION_LEVEL_ONE == orig_table_schema.get_part_level()) {
    ret = OB_ERR_NOT_COMPOSITE_PARTITION;
  } else {
    const auto addition = peg::alter_subpartition_addition(node);
    const auto parent_text = peg::relation_name(addition.parent).text();
    ObString partition_name(syntax_text(parent_text));
    const ObPartitionOption &subpart_option = orig_table_schema.get_sub_part_option();
    AlterTableSchema &alter_table_schema = get_alter_table_stmt()->get_alter_table_arg().alter_table_schema_;
    ObSEArray<ObString, 8> dummy_part_keys;
    ObPartition dummy_part;
    ObPartition *cur_partition = nullptr;
    bool found_parent = false;
    alter_stmt->set_use_def_sub_part(false);
    alter_table_schema.get_sub_part_option() = orig_table_schema.get_sub_part_option();
    for (int64_t i = 0; OB_SUCC(ret) && i < orig_table_schema.get_partition_num(); ++i) {
      const ObPartition *partition = orig_table_schema.get_part_array()[i];
      if (OB_ISNULL(partition)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (partition->get_part_name() == partition_name) {
        found_parent = true;
        break;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (!found_parent) {
      ret = OB_UNKNOWN_PARTITION;
      LOG_USER_ERROR(OB_UNKNOWN_PARTITION, partition_name.length(), partition_name.ptr(),
          orig_table_schema.get_table_name_str().length(), orig_table_schema.get_table_name_str().ptr());
    } else if (OB_FAIL(dummy_part.set_part_name(partition_name))) {
    } else if (OB_FAIL(alter_table_schema.add_partition(dummy_part))) {
    } else if (OB_ISNULL(cur_partition = alter_table_schema.get_part_array()[0])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(resolve_stored_partition_function(orig_table_schema, true,
        alter_stmt->get_subpart_fun_exprs(), dummy_part_keys))) {
    } else if (subpart_option.is_range_part()
               && addition.elements[0].kind() == peg::NodeKind::ListPartitionElement) {
      ret = OB_ERR_SUBPARTITION_NOT_EXPECT_VALUES_IN;
    } else if (subpart_option.is_list_part()
               && addition.elements[0].kind() == peg::NodeKind::RangePartitionElement) {
      ret = OB_ERR_SUBPARTITION_EXPECT_VALUES_IN;
    } else if (subpart_option.is_range_part() || subpart_option.is_list_part()) {
      if (OB_FAIL(resolve_subpartition_elements(alter_stmt, addition.elements,
                                               alter_table_schema, cur_partition))) {
      }
    }
    if (OB_SUCC(ret)) {
      alter_table_schema.set_part_level(orig_table_schema.get_part_level());
      alter_table_schema.get_part_option() = orig_table_schema.get_part_option();
      alter_table_schema.get_part_option().set_part_num(alter_table_schema.get_partition_num());
      cur_partition->set_sub_part_num(cur_partition->get_subpartition_num());
      if ((subpart_option.is_range_part() || subpart_option.is_list_part())
          && OB_FAIL(check_and_set_individual_subpartition_names(alter_stmt, alter_table_schema))) {
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_drop_subpartition(const peg::Node &node,
                                                    const ObTableSchema &orig_table_schema)
{
  int ret = OB_SUCCESS;
  if (share::schema::PARTITION_LEVEL_ONE == orig_table_schema.get_part_level()) {
    ret = OB_ERR_NOT_COMPOSITE_PARTITION;
  } else if (orig_table_schema.is_hash_like_subpart()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Drop hash subpartition");
  } else {
    ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
    const auto names = peg::alter_partition_names(node);
    if (names.empty() || OB_ISNULL(alter_table_stmt)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      AlterTableSchema &alter_table_schema =
          alter_table_stmt->get_alter_table_arg().alter_table_schema_;
      ObPartition dummy_part;
      for (int64_t i = 0; OB_SUCC(ret) && i < names.size(); ++i) {
        ObSubPartition subpart;
        const auto partition_text = names[i].text();
        ObString partition_name(syntax_text(partition_text));
        if (OB_FAIL(subpart.set_part_name(partition_name))) {
        } else if (OB_FAIL(check_subpart_name(dummy_part, subpart))){
        } else if (OB_FAIL(dummy_part.add_partition(subpart))){
        }
      }
      if (OB_SUCC(ret)) {
        dummy_part.set_sub_part_num(names.size());
        if (OB_FAIL(alter_table_schema.add_partition(dummy_part))) {
        } else {
          alter_table_schema.set_part_level(orig_table_schema.get_part_level());
          alter_table_schema.get_part_option() = orig_table_schema.get_part_option();
          alter_table_schema.get_sub_part_option() = orig_table_schema.get_sub_part_option();
          alter_table_schema.get_part_option().set_part_num(alter_table_schema.get_partition_num());
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_exchange_partition(const peg::Node &node,
                                                     const ObTableSchema &orig_table_schema)
{
  int ret = OB_SUCCESS;
  ObPartitionLevel exchange_part_level = orig_table_schema.get_part_level();
  const ObTableSchema *exchange_table_schema = nullptr;
  ObString orig_part_name;
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(PARTITION_LEVEL_ZERO == orig_table_schema.get_part_level())) {
    ret = OB_ERR_PARTITION_MGMT_ON_NONPARTITIONED;
    LOG_USER_ERROR(OB_ERR_PARTITION_MGMT_ON_NONPARTITIONED);
  } else if (OB_FAIL(resolve_exchange_partition_stmt(node, orig_table_schema, exchange_table_schema, orig_part_name))) {
  } else if (OB_ISNULL(exchange_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (exchange_table_schema->is_partitioned_table()) {
    exchange_part_level = ObPartitionLevel::PARTITION_LEVEL_ONE;
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(params_.root_command_service_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(params_.root_command_service_->
      check_partition_exchange_schema_for_user(
      orig_table_schema, *exchange_table_schema, orig_part_name, exchange_part_level))) {
  } else if (OB_FAIL(generate_exchange_partition_arg(orig_table_schema, *exchange_table_schema, orig_part_name, exchange_part_level))) {
  }
  return ret;
}


int ObAlterTableResolver::resolve_exchange_partition_stmt(
    const peg::Node &node,
    const ObTableSchema &orig_table_schema,
    const ObTableSchema *&exchange_table_schema,
    ObString &orig_part_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
  } else {
    ObString exchange_table_name;
    ObString exchange_db_name;
    const peg::Node *second_node = &peg::alter_partition_exchange_table(node);
    ObCStringHelper helper;
    if (OB_FAIL(resolve_table_relation_node(second_node,
                                            exchange_table_name,
                                            exchange_db_name))){
    } else if (0 == exchange_db_name.compare(session_info_->get_database_name()) && 0 == exchange_table_name.compare(orig_table_schema.get_table_name())) {
      ret = OB_ERR_NONUNIQ_TABLE;
      LOG_USER_ERROR(OB_ERR_NONUNIQ_TABLE, exchange_table_name.length(), exchange_table_name.ptr());
    } else if (OB_ISNULL(schema_checker_)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(schema_checker_->get_table_schema(
                                                         exchange_db_name,
                                                         exchange_table_name,
                                                         false,
                                                         exchange_table_schema))) {
      if (OB_TABLE_NOT_EXIST == ret) {
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(exchange_db_name), helper.convert(exchange_table_name));
      }
    } else if (OB_ISNULL(exchange_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
    } else {
      const auto &name = peg::alter_partition_exchange_name(node);
      const auto name_text = name.text();
      OZ(ob_write_string(*allocator_, syntax_text(name_text), orig_part_name));
    }
  }
  return ret;
}

int ObAlterTableResolver::generate_exchange_partition_arg(
    const ObTableSchema &orig_table_schema,
    const ObTableSchema &exchange_table_schema,
    const ObString &orig_part_name,
    const ObPartitionLevel exchange_part_level)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  obcall::ObExchangePartitionArg exchange_partition_arg;
  if (OB_ISNULL(session_info_) || OB_ISNULL(alter_table_stmt)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(exchange_partition_arg.based_schema_object_infos_.assign(alter_table_stmt->get_alter_table_arg().based_schema_object_infos_))) {
  } else if (OB_FAIL(exchange_partition_arg.based_schema_object_infos_.push_back(
      ObBasedSchemaObjectInfo(exchange_table_schema.get_table_id(), TABLE_SCHEMA, exchange_table_schema.get_schema_version())))) {
  } else {
    exchange_partition_arg.session_id_ = session_info_->get_server_sid();
    
    exchange_partition_arg.exchange_partition_level_ = exchange_part_level;
    exchange_partition_arg.base_table_id_ = orig_table_schema.get_table_id();
    exchange_partition_arg.base_table_part_name_ = orig_part_name;
    exchange_partition_arg.inc_table_id_ = exchange_table_schema.get_table_id();
    exchange_partition_arg.including_indexes_ = true;
    exchange_partition_arg.without_validation_ = true;
    exchange_partition_arg.update_global_indexes_ = false;
    
    if (OB_FAIL(alter_table_stmt->set_exchange_partition_arg(exchange_partition_arg))) {
    }
  }
  return ret;
}

int ObAlterTableResolver::check_subpart_name(const ObPartition &partition,
                                             const ObSubPartition &subpartition)
{
  int ret = OB_SUCCESS;
  const ObString &subpart_name = subpartition.get_part_name();
  for (int64_t i = 0; OB_SUCC(ret) && i < partition.get_subpartition_num(); ++i) {
    if (common::ObCharset::case_insensitive_equal(subpart_name,
                                                  partition.get_subpart_array()[i]->get_part_name())) {
      ret = OB_ERR_SAME_NAME_PARTITION;
      LOG_USER_ERROR(OB_ERR_SAME_NAME_PARTITION, subpart_name.length(), subpart_name.ptr());
    }
  }
  return ret;
}





int ObAlterTableResolver::resolve_index_visibility(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  const auto text = peg::alter_index_name(node).text();
  ObString index_name;
  ObAlterIndexArg *argument = nullptr;
  void *buffer = nullptr;
  OZ(ob_write_string(*allocator_, syntax_text(text), index_name));
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(buffer = allocator_->alloc(sizeof(ObAlterIndexArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      argument = new (buffer) ObAlterIndexArg();
      argument->index_name_ = index_name;
      argument->index_visibility_ = peg::index_visible(node) ? 0 : 1;
    }
  }
  if (OB_SUCC(ret) && (table_schema_->is_parent_table() || table_schema_->is_child_table())) {
    ObString index_table_name;
    ObArenaAllocator names(ObModIds::OB_SCHEMA);
    const ObTableSchema *index_schema = nullptr;
    OZ(ObTableSchema::build_index_table_name(names, table_schema_->get_table_id(), index_name, index_table_name));
    if (OB_SUCC(ret) && OB_FAIL(schema_checker_->get_table_schema(
        get_alter_table_stmt()->get_org_database_name(), index_table_name, true, index_schema))) {
      if (ret == OB_TABLE_NOT_EXIST) {
        ObCStringHelper helper;
        LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(get_alter_table_stmt()->get_org_database_name()),
                       helper.convert(get_alter_table_stmt()->get_org_table_name()));
      }
    }
    CK(OB_NOT_NULL(index_schema));
    OZ(check_index_columns_equal_foreign_key(*table_schema_, *index_schema));
  }
  OZ(get_alter_table_stmt()->add_index_arg(argument));
  return ret;
}

int ObAlterTableResolver::resolve_index_parallel(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  const uint64_t degree = peg::alter_index_parallel(node);
  if (degree == 0 || degree > INT64_MAX) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "positive parallel degree");
  }
  const auto text = peg::alter_index_name(node).text();
  ObString index_name;
  ObAlterIndexParallelArg *argument = nullptr;
  void *buffer = nullptr;
  OZ(ob_write_string(*allocator_, syntax_text(text), index_name));
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(buffer = allocator_->alloc(sizeof(ObAlterIndexParallelArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      argument = new (buffer) ObAlterIndexParallelArg();
      argument->index_name_ = index_name;
      argument->new_parallel_ = static_cast<int64_t>(degree);
    }
  }
  OZ(get_alter_table_stmt()->add_index_arg(argument));
  return ret;
}

int ObAlterTableResolver::resolve_rename_index(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  {
    {
      const auto &old_name = peg::alter_index_name(node);
      const auto &new_name = peg::alter_index_new_name(node);
      const auto new_name_text = new_name.text();
      ObString tmp_new_index_name(syntax_text(new_name_text));
      ObCollationType cs_type = CS_TYPE_INVALID;
      ObRenameIndexArg *rename_index_arg = NULL;
      if (OB_ISNULL(session_info_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
      } else if (OB_FAIL(ObSQLUtils::check_index_name(cs_type, tmp_new_index_name))) {
      } else {
        ObString tmp_index_name;
        ObString ori_index_name;
        ObString new_index_name;
        const auto old_name_text = old_name.text();
        OZ(ob_write_string(*allocator_, syntax_text(old_name_text), ori_index_name));
        OZ(ob_write_string(*allocator_, syntax_text(new_name_text), new_index_name));
        void *tmp_ptr = NULL;

        if (OB_FAIL(ret)) {
        } else if (OB_UNLIKELY(NULL == (tmp_ptr = (ObRenameIndexArg *)allocator_->alloc(sizeof(obcall::ObRenameIndexArg))))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          SQL_RESV_LOG(ERROR, "failed to allocate memory", K(ret));
        } else {
          rename_index_arg = new (tmp_ptr)ObRenameIndexArg();

          rename_index_arg->origin_index_name_ = ori_index_name;
          rename_index_arg->new_index_name_= new_index_name;
        }
      }

      //push index arg
      if (OB_SUCC(ret)) {
        ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
        if (OB_ISNULL(alter_table_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
        } else if (OB_FAIL(alter_table_stmt->add_index_arg(rename_index_arg))) {
        }
      } //end for pushing index arg
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_alter_primary(rust::Slice<const peg::Node> actions,
                                                const peg::Node &node)
{
  int ret = OB_SUCCESS;
  bool is_exist_add_pk = false;
  bool is_exist_drop_pk = false;
  if (OB_FAIL(is_exist_item_type(actions, peg::NodeKind::PrimaryKey, is_exist_add_pk))
      || OB_FAIL(is_exist_item_type(actions, peg::NodeKind::PrimaryKeyDrop, is_exist_drop_pk))) {
    SQL_RESV_LOG(WARN, "failed to check item type!", K(ret));
  } else if (!is_exist_add_pk || !is_exist_drop_pk) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "Mysql mode, invalid parse tree!", K(ret));
  } else if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "table_schema is null", K(ret));
  } else if (table_schema_->is_table_without_pk()) {
    const ObString pk_name = "PRIMAY";
    ret = OB_ERR_CANT_DROP_FIELD_OR_KEY;
    LOG_USER_ERROR(OB_ERR_CANT_DROP_FIELD_OR_KEY, pk_name.length(), pk_name.ptr());
  } else {
    // check if table has HNSW index with extra info
    share::schema::ObSchemaGetterGuard *schema_guard = schema_checker_->get_schema_guard();
    bool has_hnsw_with_extra_info = false;
    if (OB_ISNULL(schema_guard)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(share::ObVectorIndexUtil::check_has_extra_info(*table_schema_, *schema_guard, has_hnsw_with_extra_info))) {
    } else if (has_hnsw_with_extra_info) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "dropping primary key when table has HNSW index with extra info is");
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    obcall::ObAlterPrimaryArg *alter_pk_arg = NULL;
    void *tmp_ptr = NULL;
    if (NULL == (tmp_ptr = (ObAlterPrimaryArg *)allocator_->alloc(sizeof(obcall::ObAlterPrimaryArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "failed to allocate memory", K(ret));
    } else {
      alter_pk_arg = new (tmp_ptr) ObAlterPrimaryArg();
      alter_pk_arg->set_index_action_type(ObIndexArg::ALTER_PRIMARY_KEY);
      const auto columns = peg::primary_key_columns(node);
      if (OB_SUCC(ret) && columns.size() > OB_USER_MAX_ROWKEY_COLUMN_NUMBER) {
        ret = OB_ERR_TOO_MANY_ROWKEY_COLUMNS;
        LOG_USER_ERROR(OB_ERR_TOO_MANY_ROWKEY_COLUMNS, OB_USER_MAX_ROWKEY_COLUMN_NUMBER);
      }

      obcall::ObColumnSortItem sort_item;
      const ObColumnSchemaV2 *col = NULL;
      for (int32_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
        const auto &column_node = columns[i];
        {
          sort_item.reset();
          const auto column_node_text = column_node.text();
          OZ(ob_write_string(*allocator_, syntax_text(column_node_text), sort_item.column_name_));
          sort_item.prefix_len_ = 0;
          sort_item.order_type_ = common::ObOrderType::ASC;
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(col = table_schema_->get_column_schema(sort_item.column_name_))) {
            ret = OB_ERR_KEY_DOES_NOT_EXISTS;
            SQL_RESV_LOG(WARN, "col is null", K(ret), "column_name", sort_item.column_name_);
            LOG_USER_ERROR(OB_ERR_KEY_DOES_NOT_EXISTS,
            sort_item.column_name_.length(), sort_item.column_name_.ptr(),
            table_schema_->get_table_name_str().length(), table_schema_->get_table_name_str().ptr());
          } else if (OB_FAIL(check_add_column_as_pk_allowed(*col))) {
          } else if (OB_FAIL(add_sort_column(sort_item, *alter_pk_arg))) {
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
      alter_pk_arg->index_type_ = INDEX_TYPE_PRIMARY;
      alter_pk_arg->index_name_.assign_ptr(
      common::OB_PRIMARY_INDEX_NAME, static_cast<int32_t>(strlen(common::OB_PRIMARY_INDEX_NAME)));
      
      if (OB_ISNULL(alter_table_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
      } else if (OB_FAIL(alter_table_stmt->add_index_arg(alter_pk_arg))) {
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_add_primary(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "table_schema is null", K(ret));
  } else if (table_schema_->is_table_with_pk()) {
    ret = OB_ERR_MULTIPLE_PRI_KEY;
    SQL_RESV_LOG(WARN, "multiple primary key defined", K(ret));
  } else {
    obcall::ObCreateIndexArg *create_index_arg = NULL;
    void *tmp_ptr = NULL;
    if (NULL == (tmp_ptr = (ObCreateIndexArg *)allocator_->alloc(sizeof(obcall::ObCreateIndexArg)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      SQL_RESV_LOG(ERROR, "failed to allocate memory", K(ret));
    } else {
      create_index_arg = new (tmp_ptr) ObCreateIndexArg();
      create_index_arg->set_index_action_type(ObIndexArg::ADD_PRIMARY_KEY);
    }
    const auto columns = peg::primary_key_columns(node);
      if (OB_SUCC(ret) && columns.size() > OB_USER_MAX_ROWKEY_COLUMN_NUMBER) {
      ret = OB_ERR_TOO_MANY_ROWKEY_COLUMNS;
      LOG_USER_ERROR(OB_ERR_TOO_MANY_ROWKEY_COLUMNS, OB_USER_MAX_ROWKEY_COLUMN_NUMBER);
    }

    obcall::ObColumnSortItem sort_item;
    const ObColumnSchemaV2 *col = NULL;
    for (int32_t i = 0; OB_SUCC(ret) && i < columns.size(); ++i) {
      const auto &column_node = columns[i];
        {
        sort_item.reset();
        const auto column_node_text = column_node.text();
        OZ(ob_write_string(*allocator_, syntax_text(column_node_text), sort_item.column_name_));
        sort_item.prefix_len_ = 0;
        sort_item.order_type_ = common::ObOrderType::ASC;
        if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(col = table_schema_->get_column_schema(sort_item.column_name_))) {
          ret = OB_ERR_KEY_DOES_NOT_EXISTS;
          SQL_RESV_LOG(WARN, "col is null", K(ret), "column_name", sort_item.column_name_);
          LOG_USER_ERROR(OB_ERR_KEY_DOES_NOT_EXISTS,
          sort_item.column_name_.length(), sort_item.column_name_.ptr(),
          table_schema_->get_table_name_str().length(), table_schema_->get_table_name_str().ptr());
        } else if (OB_FAIL(check_add_column_as_pk_allowed(*col))) {
        } else if (OB_FAIL(add_sort_column(sort_item, *create_index_arg))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
      create_index_arg->index_type_ = INDEX_TYPE_PRIMARY;
      create_index_arg->index_name_.assign_ptr(common::OB_PRIMARY_INDEX_NAME,
                                                static_cast<int32_t>(strlen(common::OB_PRIMARY_INDEX_NAME)));
      
      if (OB_ISNULL(alter_table_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
      } else if (OB_FAIL(alter_table_stmt->add_index_arg(create_index_arg))) {
      }
    }
  }
  return ret;
}

// to check whether to drop primary key only.
// to check whether to drop primary key legally.
int ObAlterTableResolver::check_is_drop_primary_key(rust::Slice<const peg::Node> actions,
                                                    bool &is_drop_primary_key)
{
  int ret = OB_SUCCESS;
  is_drop_primary_key = false;
  // Not supported is reported when,
  // there are multiple add pk nodes and at least one drop pk node,
  // or there are at least one other action node and at least one drop pk node,
  // or there are multiple drop pk nodes.
  // or drop pk if the table is implicit key partition table.
  int64_t drop_pk_node_cnt = 0;
  int64_t add_pk_node_cnt = 0;
  int64_t other_action_cnt = 0;
  if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObPartitionFuncType part_func_type = table_schema_->get_part_option().get_part_func_type();
    if (share::schema::PARTITION_FUNC_TYPE_KEY_IMPLICIT == part_func_type) {
      ret = OB_ERR_FIELD_NOT_FOUND_PART;
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < actions.size(); ++i) {
    const auto &action = actions[i];
    if (action.kind() == peg::NodeKind::PrimaryKey) {
      add_pk_node_cnt++;
    } else if (action.kind() == peg::NodeKind::PrimaryKeyDrop) {
      drop_pk_node_cnt++;
    } else {
      other_action_cnt++;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (drop_pk_node_cnt <= 0) {
    ret = OB_ERR_UNEXPECTED;
  } else if (drop_pk_node_cnt == 1 && add_pk_node_cnt == 1 && other_action_cnt == 0) {
    // is modify primary key operation.
    is_drop_primary_key = false;
  } else if (drop_pk_node_cnt == 1 && add_pk_node_cnt == 0 && other_action_cnt == 0) {
    // is drop primary key operation.
    is_drop_primary_key = true;
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Multiple complex DDLs about primary in single stmt");
  }
  return ret;
}

int ObAlterTableResolver::resolve_drop_primary(rust::Slice<const peg::Node> actions)
{
  int ret = OB_SUCCESS;
  void *tmp_ptr = nullptr;
  bool is_exist_drop_pk = false;
  bool is_drop_primary_key = false;
  ObAlterPrimaryArg *drop_pk_arg = nullptr;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  if (OB_ISNULL(alter_table_stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (table_schema_->is_heap_organized_table()) {
    const ObString pk_name = "PRIMAY";
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "can't DROP 'PRIMARY', DROP 'PRIMARY' in the heap organized table");
  } else if (table_schema_->is_table_without_pk()) {
    const ObString pk_name = "PRIMAY";
    ret = OB_ERR_CANT_DROP_FIELD_OR_KEY;
    LOG_USER_ERROR(OB_ERR_CANT_DROP_FIELD_OR_KEY, pk_name.length(), pk_name.ptr());
  } else if (OB_FAIL(is_exist_item_type(actions, peg::NodeKind::PrimaryKeyDrop, is_exist_drop_pk))) {
  } else if (!is_exist_drop_pk) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(check_is_drop_primary_key(actions, is_drop_primary_key))) {
  } else if (!is_drop_primary_key) {
    // modify primary key, thus skip to add drop primary key arg.
  } else if (OB_ISNULL(tmp_ptr = (ObAlterPrimaryArg *)allocator_->alloc(sizeof(ObAlterPrimaryArg)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    drop_pk_arg = new (tmp_ptr) ObAlterPrimaryArg();
    drop_pk_arg->set_index_action_type(ObIndexArg::DROP_PRIMARY_KEY);
    drop_pk_arg->index_type_ = INDEX_TYPE_PRIMARY;
    
    if (OB_FAIL(alter_table_stmt->add_index_arg(drop_pk_arg))) {
    }
  }
  if (OB_FAIL(ret)) {
    if (nullptr != drop_pk_arg) {
      drop_pk_arg->~ObAlterPrimaryArg();
      drop_pk_arg = nullptr;
    }
    if (nullptr != tmp_ptr) {
      allocator_->free(tmp_ptr);
      tmp_ptr = nullptr;
    }
  }
  return ret;
}


int ObAlterTableResolver::resolve_index_options(rust::Slice<const peg::Node> actions,
                                                const peg::Node &node,
                                                bool &is_add_index)
{
  int ret = OB_SUCCESS;
  switch (node.kind()) {
    case peg::NodeKind::IndexAdd: is_add_index = true; break;
    case peg::NodeKind::IndexDrop: OZ(resolve_drop_index(node)); break;
    case peg::NodeKind::IndexRename: OZ(resolve_rename_index(node)); break;
    case peg::NodeKind::IndexVisibility: OZ(resolve_index_visibility(node)); break;
    case peg::NodeKind::IndexParallel: OZ(resolve_index_parallel(node)); break;
    case peg::NodeKind::PrimaryKey: {
      bool has_drop = false;
      OZ(is_exist_item_type(actions, peg::NodeKind::PrimaryKeyDrop, has_drop));
      if (OB_SUCC(ret)) {
        if (has_drop) { OZ(resolve_alter_primary(actions, node)); }
        else { OZ(resolve_add_primary(node)); }
      }
      break;
    }
    case peg::NodeKind::PrimaryKeyDrop: OZ(resolve_drop_primary(actions)); break;
    default: ret = OB_ERR_UNEXPECTED; break;
  }
  return ret;
}

int ObAlterTableResolver::check_dup_foreign_keys_exist(
    share::schema::ObSchemaGetterGuard *schema_guard,
    const obcall::ObCreateForeignKeyArg &foreign_key_arg)
{
  int ret = OB_SUCCESS;
  ObSEArray<uint64_t, 4> child_column_ids;
  ObSEArray<uint64_t, 4> parent_column_ids;
  const ObTableSchema *parent_table_schema = NULL;


  if (OB_ISNULL(schema_guard)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "schema_guard is null", K(ret));
  } else if (OB_FAIL(schema_guard->get_table_schema(
             foreign_key_arg.parent_database_, foreign_key_arg.parent_table_, false, parent_table_schema))) {
  } else if (OB_ISNULL(parent_table_schema)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "parent_table_schema is null", K(ret));
  } else {
    for (int i = 0; OB_SUCC(ret) && i < foreign_key_arg.child_columns_.count(); ++i) {
      const ObColumnSchemaV2 *child_column_schema = table_schema_->get_column_schema(foreign_key_arg.child_columns_.at(i));
      const ObColumnSchemaV2 *parent_column_schema = parent_table_schema->get_column_schema(foreign_key_arg.parent_columns_.at(i));
      if (OB_ISNULL(child_column_schema)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "child_column_schema is null", K(ret));
      } else if (OB_ISNULL(parent_column_schema)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "parent_column_schema is null", K(ret));
      } else if (OB_FAIL(child_column_ids.push_back(child_column_schema->get_column_id()))) {
      } else if (OB_FAIL(parent_column_ids.push_back(parent_column_schema->get_column_id()))) {
      }
    }
    if (OB_SUCC(ret)
        && OB_FAIL(ObResolverUtils::check_dup_foreign_keys_exist(
                   table_schema_->get_foreign_key_infos(),
                   child_column_ids, parent_column_ids,
                   parent_table_schema->get_table_id(),
                   table_schema_->get_table_id()))) {
      SQL_RESV_LOG(WARN, "check dup foreign keys exist failed", K(ret));
    }
  }

  return ret;
}
// Used to parse add/drop check constraint
int ObAlterTableResolver::resolve_constraint_options(const peg::Node &node, const bool is_multi_actions)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *statement = get_alter_table_stmt();
  if (node.kind() == peg::NodeKind::DropConstraint || node.kind() == peg::NodeKind::CheckConstraintDrop) {
    OZ(resolve_drop_constraint(node));
    if (OB_SUCC(ret)) { statement->get_alter_table_arg().alter_constraint_type_ = ObAlterTableArg::DROP_CONSTRAINT; }
  } else if (is_multi_actions) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "Add/modify constraint together with other DDLs");
  } else {
    OZ(resolve_add_constraint(node));
    if (OB_SUCC(ret)) {
      statement->get_alter_table_arg().alter_constraint_type_ = ObAlterTableArg::ADD_CONSTRAINT;
      ++add_or_modify_check_cst_times_;
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_partition_options(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
    if (OB_ISNULL(alter_table_stmt)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
    }

    if (OB_SUCC(ret)) {
      const ObPartitionLevel part_level = table_schema_->get_part_level();
      if (peg::NodeKind::Repartition != node.kind()
          && PARTITION_LEVEL_ZERO == part_level) {
        ret = OB_ERR_PARTITION_MGMT_ON_NONPARTITIONED;
      }
    }
    if (OB_SUCC(ret)) {
      switch(node.kind()) {
        case peg::NodeKind::PartitionAdd: {
          if (OB_FAIL(resolve_add_partition(node, *table_schema_))) {
          } else {
            alter_table_stmt->get_alter_table_arg().alter_part_type_ =
                ObAlterTableArg::ADD_PARTITION;
          }
          break;
        }
        case peg::NodeKind::PartitionDrop: {
          if (OB_FAIL(resolve_drop_partition(node, *table_schema_))) {
          } else {
            alter_table_stmt->get_alter_table_arg().is_update_global_indexes_ = true;
            alter_table_stmt->get_alter_table_arg().alter_part_type_ = ObAlterTableArg::DROP_PARTITION;
          }
          break;
        }
        case peg::NodeKind::SubpartitionAdd: {
          if (OB_FAIL(resolve_add_subpartition(node, *table_schema_))) {
          } else {
            alter_table_stmt->get_alter_table_arg().alter_part_type_ = ObAlterTableArg::ADD_SUB_PARTITION;
          }
          break;
        }
        case peg::NodeKind::SubpartitionDrop: {
          if (OB_FAIL(resolve_drop_subpartition(node, *table_schema_))) {
          } else {
            alter_table_stmt->get_alter_table_arg().is_update_global_indexes_ = true;
            alter_table_stmt->get_alter_table_arg().alter_part_type_ = ObAlterTableArg::DROP_SUB_PARTITION;
          }
          break;
        }
        case peg::NodeKind::Repartition: {
          alter_table_stmt->get_alter_table_arg().alter_part_type_ =
            ObAlterTableArg::REPARTITION_TABLE;
          if (OB_FAIL(resolve_partitioned_partition(node, *table_schema_))) {
          }
          break;
        }
        case peg::NodeKind::PartitionTruncate: {
              if (OB_FAIL(resolve_drop_partition(node, *table_schema_))) {
          } else {
            alter_table_stmt->get_alter_table_arg().is_update_global_indexes_ = true;
            alter_table_stmt->get_alter_table_arg().alter_part_type_ = ObAlterTableArg::TRUNCATE_PARTITION;
          }
          break;
        }
        case peg::NodeKind::SubpartitionTruncate: {
          if (OB_FAIL(resolve_drop_subpartition(node, *table_schema_))) {
          } else {
            alter_table_stmt->get_alter_table_arg().is_update_global_indexes_ = true;
            alter_table_stmt->get_alter_table_arg().alter_part_type_ = ObAlterTableArg::TRUNCATE_SUB_PARTITION;
          }
          break;
        }
        case peg::NodeKind::PartitionExchange: {
          if (OB_FAIL(resolve_exchange_partition(node, *table_schema_))) {
          } else {
            alter_table_stmt->get_alter_table_arg().alter_part_type_ = ObAlterTableArg::EXCHANGE_PARTITION;
          }
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "Unknown alter partition option %d!",
                       "option type", node.kind(), K(ret));
          break;
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_partitioned_partition(const peg::Node &node,
                                                        const ObTableSchema &origin_table_schema)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  ObArenaAllocator alloc;
  ObString origin_table_name;
  ObString origin_database_name;
  if (OB_ISNULL(alter_table_stmt)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ob_write_string(alloc,
                                     alter_table_stmt->get_alter_table_arg().alter_table_schema_.get_origin_table_name(),
                                     origin_table_name))) {
  } else if (OB_FAIL(ob_write_string(alloc,
                                     alter_table_stmt->get_alter_table_arg().alter_table_schema_.get_origin_database_name(),
                                     origin_database_name))) {
  } else {
    AlterTableSchema &table_schema = alter_table_stmt->get_alter_table_arg().alter_table_schema_;
    if (OB_FAIL(table_schema.assign(origin_table_schema))) {
    } else {
      table_schema.reuse_partition_schema();
      table_schema.reset_column_part_key_info();
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(resolve_partition_node(alter_table_stmt, peg::alter_partition_definition(node), table_schema))) {
    } else if (OB_FAIL(table_schema.check_primary_key_cover_partition_column())) {
    } else if (OB_FAIL(table_schema.set_origin_table_name(origin_table_name))) {
    } else if (OB_FAIL(table_schema.set_origin_database_name(origin_database_name))) {
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_convert_to_character(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  const auto &name = peg::alter_charset_name(node);
  const auto name_text = name.text();
  ObString charset(syntax_text(name_text));
  charset = charset.trim();
  const ObCharsetType charset_type = ObCharset::charset_type(charset);
  if (CHARSET_INVALID == charset_type) {
    ret = OB_ERR_UNKNOWN_CHARSET;
    LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, charset.length(), charset.ptr());
  } else {
    charset_type_ = charset_type;
  }
  if (OB_SUCC(ret) && peg::alter_charset_has_collation(node)) {
    const auto &name = peg::alter_charset_collation(node);
    const auto name_text = name.text();
    ObString collation(syntax_text(name_text));
    collation = collation.trim();
    const ObCollationType collation_type = ObCharset::collation_type(collation);
    if (CS_TYPE_INVALID == collation_type) {
      ret = OB_ERR_UNKNOWN_COLLATION;
      LOG_USER_ERROR(OB_ERR_UNKNOWN_COLLATION, collation.length(), collation.ptr());
    } else {
      collation_type_ = collation_type;
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_foreign_key_options(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  if (node.kind() == peg::NodeKind::DropConstraint || node.kind() == peg::NodeKind::ForeignKeyDrop) {
    OZ(resolve_drop_foreign_key(node));
  } else {
    ObCreateForeignKeyArg arg;
    OZ(resolve_foreign_key_node(node, arg, true));
    OZ(get_alter_table_stmt()->get_foreign_key_arg_list().push_back(arg));
  }
  return ret;
}

int ObAlterTableResolver::resolve_alter_table_option_list(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  const auto options = peg::alter_table_options(node);
  for (size_t i = 0; OB_SUCC(ret) && i < options.size(); ++i) {
    OZ(resolve_table_option(options[i], false));
  }
  return ret;
}

int ObAlterTableResolver::process_timestamp_column(ObColumnResolveStat &stat,
                                                   AlterColumnSchema &alter_column_schema)
{
  int ret = OB_SUCCESS;
  const ObObj &cur_default_value = alter_column_schema.get_cur_default_value();
  bool explicit_value = false;
  if (NULL == session_info_) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "session info is NULL", K(ret));
  } else if (OB_FAIL(alter_column_schema.set_orig_default_value(cur_default_value))) {
  } else if (OB_FAIL(session_info_->get_explicit_defaults_for_timestamp(explicit_value))) {
  } else if (true == explicit_value || alter_column_schema.is_generated_column()) {
    //nothing to do
  } else {
    alter_column_schema.check_timestamp_column_order_ = true;
    alter_column_schema.is_no_zero_date_ = is_no_zero_date(session_info_->get_sql_mode());
    alter_column_schema.is_set_nullable_ = stat.is_set_null_;
    alter_column_schema.is_set_default_ = stat.is_set_default_value_;
  }
  return ret;
}

int ObAlterTableResolver::resolve_add_column(const peg::Node &node, ObColumnNameSet &add_column_names_set)
{
  int ret = OB_SUCCESS;
  const auto definitions = peg::alter_column_definitions(node);
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  if (OB_ISNULL(alter_table_stmt) || OB_ISNULL(table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    AlterColumnSchema alter_column_schema;
    ObColumnResolveStat stat;
    for (int i = 0; OB_SUCC(ret) && i < definitions.size(); ++i) {
      alter_column_schema.reset();
      alter_column_schema.alter_type_ = OB_DDL_ADD_COLUMN;
      alter_table_stmt->get_alter_table_arg().alter_algorithm_ = obcall::ObAlterTableArg::AlterAlgorithm::INSTANT;
      {
        //resolve column definition
        stat.reset();
        bool is_modify_column_visibility = false;
        ObSEArray<ObColumnSchemaV2 *, 8> resolved_cols;
        if (OB_FAIL(get_table_schema_all_column_schema(resolved_cols, alter_table_stmt->get_alter_table_schema()))) {
        } else if (OB_FAIL(resolve_alter_table_column_definition(alter_column_schema,
                                                          definitions[i],
                                                          stat,
                                                          is_modify_column_visibility,
                                                          resolved_cols,
                                                          false))) {
        } else if (OB_FAIL(fill_column_schema_according_stat(stat, alter_column_schema))) {
        } else if (OB_FAIL(set_column_collation(alter_column_schema))) {
        }

        //do some check
        if (OB_SUCC(ret)) {
          // resolve index (unique key only) of column
          if (OB_SUCC(ret) && alter_column_schema.is_unique_key_) {
            if (OB_FAIL(resolve_column_index(alter_column_schema.get_column_name_str()))) {
            }
          }
          if (alter_column_schema.has_not_null_constraint()) {
            add_not_null_constraint_ = true;
          }
        }
        if (OB_SUCC(ret) && alter_column_schema.is_collection() && !alter_column_schema.is_nullable()) {
          ret = OB_ER_INVALID_USE_OF_NULL;
        }
        // do duplicate column name check
        if (OB_SUCC(ret)) {
          const ObString &column_name = alter_column_schema.get_column_name_str();
          ObColumnSchemaHashWrapper col_key(column_name);
          if (OB_FAIL(add_column_names_set.set_refactored(col_key))) {
            if (OB_HASH_EXIST == ret) {
              ret = OB_ERR_COLUMN_DUPLICATE;
              LOG_USER_ERROR(OB_ERR_COLUMN_DUPLICATE, column_name.length(), column_name.ptr());
            } else {
            }
          }
        }

        //add column
        if (OB_SUCC(ret)) {
          if (OB_FAIL(alter_table_stmt->add_column(alter_column_schema))) {
          } else if (FALSE_IT(alter_table_stmt->set_sql_mode(session_info_->get_sql_mode()))) {
          }
        }
      }
    }
    add_column_cnt_++;
  }
  return ret;
}

int ObAlterTableResolver::resolve_pos_column(const peg::Node &definition,
    share::schema::AlterColumnSchema &column)
{
  int ret = OB_SUCCESS;
  const auto position = peg::column_definition(definition).position;
  if (position == peg::ColumnPosition::First) {
    column.is_first_ = true;
  } else if (position != peg::ColumnPosition::None) {
    const auto &name = peg::column_position_name(definition);
    const auto name_text = name.text();
    ObString text(syntax_text(name_text));
    if (text.empty()) { ret = OB_INVALID_ARGUMENT; }
    else if (position == peg::ColumnPosition::After) { column.set_prev_column_name(text); }
    else { column.set_next_column_name(text); }
  }
  return ret;
}

int ObAlterTableResolver::get_table_schema_all_column_schema(
                                                    ObIArray<ObColumnSchemaV2 *> &resolved_cols,
                                                    ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < table_schema.get_column_count(); ++i) {
    OZ (resolved_cols.push_back(table_schema.get_column_schema_by_idx(i)), i, table_schema);
  }
  return ret;
}

int ObAlterTableResolver::resolve_alter_table_column_definition(AlterColumnSchema &column,
                                                                const peg::Node &node,
                                                                ObColumnResolveStat &stat,
                                                                bool &is_modify_column_visibility,
                                                        ObIArray<ObColumnSchemaV2 *> &resolved_cols,
                                                                const bool unused_flag,
                                                                const bool allow_has_default)
{
  int ret = OB_SUCCESS;
  common::ObString pk_name;
  ObAlterTableStmt* alter_table_stmt = get_alter_table_stmt();
  AlterTableSchema& alter_table_schema = alter_table_stmt->get_alter_table_arg().alter_table_schema_;
  int64_t cst_cnt = alter_table_schema.get_constraint_count();
  ObSEArray<ObString, 4> gen_col_expr_arr;
  ObTableSchema tmp_table_schema; // check_default_value will change table_schema
  if (OB_FAIL(tmp_table_schema.assign(*table_schema_))) {
  } else if (OB_FAIL(resolve_column_definition(column, node, stat,
              is_modify_column_visibility, pk_name, *table_schema_,
              false,
              false,
              allow_has_default))) {
  } else { // add column level constraint
    add_or_modify_check_cst_times_ += alter_table_schema.get_constraint_count() - cst_cnt;
  }
  // else if (OB_FAIL(process_default_value(stat, column))) {
  //   SQL_RESV_LOG(WARN, "failed to set default value", K(ret));
  //   //when add new column, the default value should saved in both origin default value
  //   //and cur_default value
  //   //in resvolve_column_definition default value is saved in cur_default_value
  //   //TODO(xiyu):hanlde add column c2 int unique key; support unique key
  // }
  OZ(resolve_pos_column(node, column));
  return ret;
}

int ObAlterTableResolver::set_column_collation(AlterColumnSchema &alter_column_schema)
{
  int ret = OB_SUCCESS;
  if (alter_column_schema.get_meta_type().is_string_type()) {
    ObCharsetType charset_type = alter_column_schema.get_charset_type();
    ObCollationType collation_type = alter_column_schema.get_collation_type();
    if (CHARSET_INVALID == charset_type && CS_TYPE_INVALID == collation_type) {
      //do nothing
      // In rootserver will set the correct collation and charset according to the table's schema
    } else if (OB_FAIL(ObCharset::check_and_fill_info(charset_type, collation_type))){
    } else {
      alter_column_schema.set_charset_type(charset_type);
      alter_column_schema.set_collation_type(collation_type);
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_alter_column(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  {
    AlterColumnSchema alter_column_schema;
    if (OB_FAIL(resolve_column_definition_ref(alter_column_schema,
                                                    peg::alter_column_name(node),
                                                    true))) {
    } else {
      alter_column_schema.alter_type_ = OB_DDL_ALTER_COLUMN;
    }
    //resolve the default value
    if (OB_SUCC(ret)) {
      {
        ObObjParam default_value;
        ObDefaultValueRes resolve_res(default_value);
        if (!peg::alter_column_has_default(node)) {
          default_value.set_null();
          alter_column_schema.is_drop_default_ = true;
        } else if (ob_is_text_tc(alter_column_schema.get_data_type())) {
          ret = OB_INVALID_DEFAULT;
          SQL_RESV_LOG(WARN, "BLOB/TEXT can't set default value!", K(ret));
        } else if (ob_is_json_tc(alter_column_schema.get_data_type())) {
          ret = OB_ERR_BLOB_CANT_HAVE_DEFAULT;
          SQL_RESV_LOG(WARN, "BLOB/TEXT or JSON can't set default value!", K(ret));
        } else if (ob_is_geometry_tc(alter_column_schema.get_data_type())) {
          ret = OB_ERR_BLOB_CANT_HAVE_DEFAULT;
          SQL_RESV_LOG(WARN, "GEOMETRY can't set default value!", K(ret));
        }else if (ob_is_collection_sql_type(alter_column_schema.get_data_type())) {
          ret = OB_ERR_BLOB_CANT_HAVE_DEFAULT;
          SQL_RESV_LOG(WARN, "COLLECTION can't set default value!", K(ret));
        } else if (OB_FAIL(resolve_default_value(peg::alter_column_default(node), resolve_res))) {
        }
        if (OB_SUCC(ret) && !resolve_res.is_literal_) {
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(session_info_)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            ObString expr_str;
            if (OB_FAIL(ob_write_string(*allocator_, params_.syntax_->source(peg::column_attribute_expression(peg::alter_column_default(node))), expr_str))) {
            } else if (OB_FAIL(ObSQLUtils::convert_sql_text_to_schema_for_storing(
                                  *allocator_, session_info_->get_dtc_params(), expr_str))) {
            } else {
              default_value.set_varchar(expr_str);
              default_value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
              default_value.set_param_meta();
            }
          }
        }

        if (OB_SUCCESS == ret &&
          OB_FAIL(alter_column_schema.set_cur_default_value(default_value, !resolve_res.is_literal_))) {
            SQL_RESV_LOG(WARN, "failed to set current default to alter column schema!", K(ret));
        }
      }
    }
    //add atler alter_column schema
    if (OB_SUCC(ret)) {
      ObColumnResolveStat stat;
      ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
      if (OB_ISNULL(alter_table_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
      } else if (OB_ISNULL(session_info_)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "session_info_ should not be null", K(ret));
      } else if (OB_FAIL(process_timestamp_column(stat, alter_column_schema))) {
      } else if (OB_FAIL(alter_table_stmt->add_column(alter_column_schema))){
      } else if (FALSE_IT(alter_table_stmt->set_sql_mode(session_info_->get_sql_mode()))) {
      }
    }
  }
  return ret;
}

// To check whether modify/change column is allowed,
// should check it under main table schema and index table schemas.
int ObAlterTableResolver::check_column_in_part_key(const ObTableSchema &table_schema,
                                                   const ObColumnSchemaV2 &src_col_schema,
                                                   const ObColumnSchemaV2 &dst_col_schema)
{
  int ret = OB_SUCCESS;
  // 1. to get all check table schemas, including main table schema and its' index schemas.
  bool is_same = false;
  ObSArray<const ObTableSchema *> check_table_schemas;
  ObSEArray<ObAuxTableMetaInfo, 16> simple_index_infos;
  ObSchemaGetterGuard *schema_guard = schema_checker_->get_schema_guard();
  if (OB_ISNULL(schema_guard)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObTableSchema::check_is_exactly_same_type(src_col_schema,
                                                               dst_col_schema,
                                                               is_same))) {
  } else if (table_schema.is_partitioned_table()
             && OB_FAIL(check_table_schemas.push_back(&table_schema))) {
  } else if (OB_FAIL(table_schema.get_simple_index_infos(simple_index_infos))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); i++) {
      const ObTableSchema *index_schema = nullptr;
      if (OB_FAIL(schema_guard->get_table_schema(
                                                 simple_index_infos.at(i).table_id_,
                                                 index_schema))) {
      } else if (OB_ISNULL(index_schema)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (index_schema->is_partitioned_table()
                 && OB_FAIL(check_table_schemas.push_back(index_schema))) {
      }
    }
  }

  // 2. to check whether change/modify column for each schema is allowed.
  if (OB_SUCC(ret)) {
    const ObString &alter_column_name = src_col_schema.get_column_name_str();
    for (int64_t i = 0; OB_SUCC(ret) && i < check_table_schemas.count(); i++) {
      const ObTableSchema &cur_table_schema = *check_table_schemas.at(i);
      const ObColumnSchemaV2 *column_schema = nullptr;
      const ObPartitionOption &part_option = cur_table_schema.get_part_option();
      const ObString &part_func_str = part_option.get_part_func_expr_str();
      bool is_partition_key = false;
      bool is_subpartition_key = false;
      if (OB_ISNULL(column_schema = cur_table_schema.get_column_schema(alter_column_name))) {
        // do nothing, because the column does not exist in the schema.
      } else if (OB_FAIL(cur_table_schema.is_partition_key(*column_schema, is_partition_key))) {
      } else if (!is_partition_key && OB_FAIL(cur_table_schema.is_subpartition_key(*column_schema, is_subpartition_key))) {
      } else if (is_partition_key || is_subpartition_key) {
        if (cur_table_schema.is_global_index_table() && !is_same) {
          // FIXME YIREN (20221019), allow to alter part key of global index table by refilling part info when rebuilding it.
          ret = OB_OP_NOT_ALLOW;
          LOG_USER_ERROR(OB_OP_NOT_ALLOW, "alter part key of global index is");
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(check_alter_part_key_allowed(cur_table_schema, *column_schema, dst_col_schema, is_partition_key))) {
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::alter_column_expr_in_part_expr(
    const ObColumnSchemaV2 &src_col_schema,
    const ObColumnSchemaV2 &dst_col_schema,
    ObRawExpr *part_expr)
{
  int ret = OB_SUCCESS;
  if (part_expr->is_column_ref_expr()) {
    ObColumnRefRawExpr *column_ref = static_cast<ObColumnRefRawExpr*>(part_expr);
    if (0 == column_ref->get_column_name().case_compare(src_col_schema.get_column_name())) {
      column_ref->set_data_type(dst_col_schema.get_data_type());
      column_ref->set_accuracy(dst_col_schema.get_accuracy());
      if (OB_FAIL(ObRawExprUtils::init_column_expr_subschema(dst_col_schema, session_info_, *column_ref))) {
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < part_expr->get_param_count(); ++i) {
      ObRawExpr *sub_expr = part_expr->get_param_expr(i);
      if (OB_FAIL(alter_column_expr_in_part_expr(src_col_schema, dst_col_schema, sub_expr))) {
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::check_alter_part_key_allowed(const ObTableSchema &table_schema,
                                                       const ObColumnSchemaV2 &src_col_schema,
                                                       const ObColumnSchemaV2 &dst_col_schema,
                                                       const bool is_part_key)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = table_schema.get_table_id();
  ObResolverParams resolver_ctx;
  ObRawExprFactory expr_factory(*allocator_);
  ObStmtFactory stmt_factory(*allocator_);
  TableItem table_item;
  resolver_ctx.allocator_ = allocator_;
  resolver_ctx.schema_checker_ = schema_checker_;
  resolver_ctx.session_info_ = session_info_;
  resolver_ctx.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
  resolver_ctx.expr_factory_ = &expr_factory;
  resolver_ctx.stmt_factory_ = &stmt_factory;
  resolver_ctx.query_ctx_ = stmt_factory.get_query_ctx();
  table_item.table_id_ = table_id;
  table_item.ref_id_ = table_id;
  table_item.type_ = TableItem::BASE_TABLE;
  // This is just to use the resolver to resolve the partition expr interface.
  // The resolver of any statement has this ability. The reason for using the delete
  // resolver is that the delete resolver is the simplest
  ObRawExpr *part_expr = NULL;
  ObPartitionFuncType part_type;
  SMART_VAR (ObDeleteResolver, delete_resolver, resolver_ctx) {
    ObDeleteStmt *delete_stmt = delete_resolver.create_stmt<ObDeleteStmt>();
    CK (OB_NOT_NULL(delete_stmt));
    CK (OB_NOT_NULL(resolver_ctx.query_ctx_));
    OZ (delete_stmt->get_table_items().push_back(&table_item));
    OZ (delete_stmt->set_table_bit_index(table_id));
    if (is_part_key) {
      ObArenaAllocator alloc;
      ObString part_str;
      part_type = table_schema.get_part_option().get_part_func_type();
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(ob_write_string(alloc, table_schema.get_part_option().get_part_func_expr_str(),
                                         part_str, true /* c_style */))) {
      } else if (OB_FAIL(delete_resolver.resolve_partition_expr(table_item, table_schema,
                                                                part_type, part_str, part_expr))) {
      }
    } else {
      const ObString &part_str = table_schema.get_sub_part_option().get_part_func_expr_str();
      part_type = table_schema.get_sub_part_option().get_part_func_type();
      OZ (delete_resolver.resolve_partition_expr(table_item, table_schema,
      part_type, part_str, part_expr));
    }
    ObRawExprPartExprChecker part_expr_checker(part_type);
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(part_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(alter_column_expr_in_part_expr(src_col_schema, dst_col_schema, part_expr))) {
    }
    OZ (part_expr->formalize(session_info_));
    if (OB_FAIL(ret)) {
    } else if (PARTITION_FUNC_TYPE_RANGE_COLUMNS == part_type ||
        PARTITION_FUNC_TYPE_LIST_COLUMNS == part_type ||
        is_key_part(part_type)) {
      if (is_key_part(part_type) && part_expr->get_param_count() < 1) {
        ret = OB_ERR_UNEXPECTED;
      }
      if (0 == part_expr->get_param_count()) {
        OZ (ObResolverUtils::check_column_valid_for_partition(*part_expr, part_type, table_schema));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < part_expr->get_param_count(); ++i) {
        ObRawExpr *param_expr = part_expr->get_param_expr(i);
        CK (OB_NOT_NULL(param_expr));
        OZ (ObResolverUtils::check_column_valid_for_partition(
          *param_expr, part_type, table_schema));
      }
    } else {
      OZ (ObResolverUtils::check_column_valid_for_partition(
        *part_expr, part_type, table_schema));
    }
    if (OB_SUCC(ret) && !is_key_part(part_type)) {
      OZ (part_expr->preorder_accept(part_expr_checker));
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_change_column(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    AlterColumnSchema alter_column_schema;
    ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
    const ObColumnSchemaV2 *origin_col_schema = NULL;
    if (OB_FAIL(resolve_column_definition_ref(alter_column_schema, peg::alter_column_name(node), true))) {
    } else if (OB_ISNULL(alter_table_stmt)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
    }
    const ObString &origin_column_name = alter_column_schema.get_origin_column_name();
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(schema_checker_->get_column_schema(
               table_schema_->get_table_id(),
               origin_column_name,
               origin_col_schema,
               false))) {
      if (ret == OB_ERR_BAD_FIELD_ERROR) {
        LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, origin_column_name.length(), origin_column_name.ptr(),
                       table_schema_->get_table_name_str().length(),
                       table_schema_->get_table_name_str().ptr());
      }
    }
    //resolve new column definition
    if (OB_SUCC(ret)) {
      ObColumnResolveStat stat;
      alter_column_schema.set_column_flags(origin_col_schema->get_column_flags());
      //alter column's generated column flag should be parsed by itself,
      // So need to clear the generated column flag that was previously copied
      alter_column_schema.erase_generated_column_flags();
      alter_column_schema.drop_not_null_cst();
      
      alter_column_schema.set_table_id(origin_col_schema->get_table_id());
      alter_column_schema.set_column_id(origin_col_schema->get_column_id());
      // ALTER TABLE CHANGE COLUMN follows the MySQL syntax path.
      bool is_modify_column_visibility = false;
      alter_column_schema.alter_type_ = OB_DDL_CHANGE_COLUMN;
      ObSEArray<ObColumnSchemaV2 *, 8> resolved_cols;
      if (OB_FAIL(get_table_schema_all_column_schema(resolved_cols, alter_table_stmt->get_alter_table_schema()))) {
      } else if (OB_FAIL(resolve_alter_table_column_definition(alter_column_schema, peg::alter_column_definitions(node)[0], stat, is_modify_column_visibility, resolved_cols))) {
      } else if (!stat.is_primary_key_ && OB_FAIL(resolve_alter_column_not_null(alter_column_schema, *origin_col_schema))) {
      } else {
        //TODO(xiyu):hanlde change column c2 c3 int unique key; support unique key
        alter_column_schema.is_primary_key_ = stat.is_primary_key_;
        alter_column_schema.is_unique_key_ = stat.is_unique_key_;
        alter_column_schema.is_autoincrement_ = stat.is_autoincrement_;
        alter_column_schema.is_set_nullable_ = stat.is_set_null_;
        alter_column_schema.is_set_default_ = stat.is_set_default_value_;
        add_not_null_constraint_ |= stat.is_set_not_null_;
        if (OB_FAIL(set_column_collation(alter_column_schema))) {
        }
      }
    }

    // resolve index (unique key only) of column
    if (OB_SUCC(ret) && alter_column_schema.is_unique_key_) {
      if (OB_FAIL(resolve_column_index(alter_column_schema.get_column_name_str()))) {
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_SUCC(ret)) {
        if (0 != origin_col_schema->get_rowkey_position()
            && alter_column_schema.is_set_default_
            && alter_column_schema.get_cur_default_value().is_null()) {
          ret = OB_ERR_PRIMARY_CANT_HAVE_NULL;
          LOG_USER_ERROR(OB_ERR_PRIMARY_CANT_HAVE_NULL);
        } else if (0 != origin_col_schema->get_rowkey_position()
            && alter_column_schema.is_set_nullable_) {
          ret = OB_ERR_PRIMARY_CANT_HAVE_NULL;
        } else if (OB_FAIL(check_alter_geo_column_allowed(alter_column_schema, *origin_col_schema))) {
        }
      }
      if (OB_SUCC(ret)) {
        if ((origin_col_schema->get_data_type()) != (alter_column_schema.get_data_type())) {
          // alter table change column when, if only changing column name, not column type, then no need to check foreign key constraints, allow change to succeed
          // If the column type is changed, you need to check the foreign key constraints
          if (OB_FAIL(check_column_in_foreign_key(*table_schema_,
                                                  alter_column_schema.get_origin_column_name(),
                                                  false /* is_drop_column */))) {
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(check_column_in_part_key(*table_schema_, *origin_col_schema, alter_column_schema))) {
        }
      }
      if (OB_SUCC(ret)) {
        alter_table_stmt->set_sql_mode(session_info_->get_sql_mode());
        if (OB_FAIL(alter_table_stmt->add_column(alter_column_schema))) {
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::check_modify_column_allowed(
                                const share::schema::AlterColumnSchema &alter_column_schema,
                                const share::schema::ObColumnSchemaV2 &origin_col_schema,
                                const ObColumnResolveStat &stat)
{
  int ret = OB_SUCCESS;
  const ObObjType origin_col_type = origin_col_schema.get_data_type();
  const ObObjType alter_col_type = alter_column_schema.get_data_type();
  const ObAccuracy &origin_col_accuracy = origin_col_schema.get_accuracy();
  const ObAccuracy &alter_col_accuracy = alter_column_schema.get_accuracy();

  if (OB_SUCC(ret)) {
    if ((origin_col_type) != alter_col_type
         || (!origin_col_schema.is_autoincrement() && alter_column_schema.is_autoincrement())) {
      // when execute "alter table modify column",
      // don't need to check foreign constraint if type is not modified.
      if (OB_FAIL(check_column_in_foreign_key(*table_schema_,
                                              alter_column_schema.get_origin_column_name(),
                                              false /* is_drop_column */))) {
      }
    }
  }
  return ret;
}



int ObAlterTableResolver::check_alter_geo_column_allowed(const share::schema::AlterColumnSchema &alter_column_schema,
                                                         const share::schema::ObColumnSchemaV2 &origin_col_schema)
{
  int ret = OB_SUCCESS;
  if (ObGeometryType != origin_col_schema.get_data_type()) {
    // do nothing
  } else if (origin_col_schema.is_spatial_index_column()
            && ObGeometryType != alter_column_schema.get_data_type()) {
    ret = OB_ERR_SPATIAL_MUST_HAVE_GEOM_COL;
    LOG_USER_ERROR(OB_ERR_SPATIAL_MUST_HAVE_GEOM_COL);
  } else if (ObGeometryType == alter_column_schema.get_data_type()
              && alter_column_schema.get_geo_type() != common::ObGeoType::GEOMETRY
              && origin_col_schema.get_geo_type() != common::ObGeoType::GEOMETRY
              && origin_col_schema.get_geo_type() != alter_column_schema.get_geo_type()) {
    ret = OB_ERR_CANT_CREATE_GEOMETRY_OBJECT;
    LOG_USER_ERROR(OB_ERR_CANT_CREATE_GEOMETRY_OBJECT);
  } else if (ObGeometryType == alter_column_schema.get_data_type()
            && origin_col_schema.get_srid() != alter_column_schema.get_srid()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "alter geometry srid");
  }
  return ret;
}

int ObAlterTableResolver::resolve_modify_column(const peg::Node &node,
                                                bool &is_modify_column_visibility,
                                                ObReducedVisibleColSet &reduced_visible_col_set)
{
  int ret = OB_SUCCESS;
  {
    const auto definitions = peg::alter_column_definitions(node);
    AlterColumnSchema alter_column_schema;
    //resolve new column defintion
    ObColumnResolveStat stat;
    //TODO(xiyu):hanlde modify column c2 int unique key; support unique key
    for (int i = 0; OB_SUCC(ret) && i < definitions.size(); ++i) {
      alter_column_schema.reset();
      stat.reset();
      const ObColumnSchemaV2 *origin_col_schema = NULL;
      ObString column_name;
      ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
      if (OB_FAIL(resolve_column_definition_ref(
                 alter_column_schema, peg::column_definition(definitions[i]).name, false))) {
      } else {
        column_name = alter_column_schema.get_column_name();
        if (OB_ISNULL(alter_table_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
        } else if (OB_FAIL(schema_checker_->get_column_schema( table_schema_->get_table_id(),
                                                              column_name,
                                                              origin_col_schema,
                                                              false))) {
          if (ret == OB_ERR_BAD_FIELD_ERROR) {
            LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR, column_name.length(), column_name.ptr(),
                           table_schema_->get_table_name_str().length(),
                           table_schema_->get_table_name_str().ptr());
          }
        } else {
          //alter column's generated column flag should be parsed by itself,
          // So need to clear the generated column flag that was previously copied
          alter_column_schema.set_column_flags(origin_col_schema->get_column_flags());
          alter_column_schema.erase_generated_column_flags();
          alter_column_schema.erase_string_lob_flag();
          alter_column_schema.drop_not_null_cst();
          alter_table_stmt->set_sql_mode(session_info_->get_sql_mode());
          alter_column_schema.alter_type_ = OB_DDL_MODIFY_COLUMN;
          
          alter_column_schema.set_table_id(origin_col_schema->get_table_id());
          alter_column_schema.set_column_id(origin_col_schema->get_column_id());
        }
      }

      if (OB_SUCC(ret)) {
        ObSEArray<ObColumnSchemaV2 *, 8> resolved_cols;
        bool allow_has_default = true;
        if (OB_FAIL(get_table_schema_all_column_schema(resolved_cols, alter_table_stmt->get_alter_table_schema()))) {
        } else if (OB_FAIL(resolve_alter_table_column_definition(
                    alter_column_schema, definitions[i], stat, is_modify_column_visibility,
                    resolved_cols,
                    false,
                    allow_has_default))) {
        } else if (!stat.is_primary_key_ &&
                   OB_FAIL(resolve_alter_column_not_null(alter_column_schema, *origin_col_schema))) {
        }
        if (OB_SUCC(ret)) {
          alter_column_schema.is_primary_key_ = stat.is_primary_key_;
          alter_column_schema.is_unique_key_ = stat.is_unique_key_;
          alter_column_schema.is_autoincrement_ = stat.is_autoincrement_;
          alter_column_schema.is_set_nullable_ = stat.is_set_null_;
          alter_column_schema.is_set_default_ = stat.is_set_default_value_;
          add_not_null_constraint_ |= stat.is_set_not_null_;
          if (OB_FAIL(alter_column_schema.set_origin_column_name(column_name))) {
          } else if (OB_FAIL(set_column_collation(alter_column_schema))) {
          }
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(check_modify_column_allowed(alter_column_schema,
                                                *origin_col_schema, stat))) {
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(check_column_in_part_key(*table_schema_, *origin_col_schema, alter_column_schema))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(alter_table_stmt->add_column(alter_column_schema))) {
          }
        }
        if (OB_SUCC(ret)) {
          if (0 != origin_col_schema->get_rowkey_position()
              && alter_column_schema.is_set_default_
              && alter_column_schema.get_cur_default_value().is_null()) {
            ret = OB_ERR_PRIMARY_CANT_HAVE_NULL;
            LOG_USER_ERROR(OB_ERR_PRIMARY_CANT_HAVE_NULL);
          } else if (0 != origin_col_schema->get_rowkey_position()
              && alter_column_schema.is_set_nullable_) {
            ret = OB_ERR_PRIMARY_CANT_HAVE_NULL;
          } else if (OB_FAIL(check_alter_geo_column_allowed(alter_column_schema, *origin_col_schema))) {
          } else if (ObGeometryType == origin_col_schema->get_data_type()
                     && ObGeometryType == alter_column_schema.get_data_type()
                     && alter_column_schema.get_geo_type() != common::ObGeoType::GEOMETRY
                     && origin_col_schema->get_geo_type() != common::ObGeoType::GEOMETRY
                     && origin_col_schema->get_geo_type() != alter_column_schema.get_geo_type()) {
            ret = OB_ERR_CANT_CREATE_GEOMETRY_OBJECT;
            LOG_USER_ERROR(OB_ERR_CANT_CREATE_GEOMETRY_OBJECT);
          } else if (ObGeometryType == origin_col_schema->get_data_type()
                     && ObGeometryType == alter_column_schema.get_data_type()
                     && origin_col_schema->get_srid() != alter_column_schema.get_srid()) {
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "Modify geometry srid");
          }
        }
      }

      // resolve index (unique key only) of column
      if (OB_SUCC(ret) && alter_column_schema.is_unique_key_) {
        if (OB_FAIL(resolve_column_index(alter_column_schema.get_column_name_str()))) {
        }
      }
      if (OB_SUCC(ret) && alter_column_schema.is_invisible_column()) {
        ObString name(column_name);
        ObColumnSchemaHashWrapper col_key(name);
        if (OB_FAIL(reduced_visible_col_set.set_refactored(col_key))) {
        }
      }
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_alter_column_not_null(share::schema::AlterColumnSchema &column,
                                                        const ObColumnSchemaV2 &ori_column)
{
  int ret = OB_SUCCESS;
  if (ori_column.has_not_null_constraint()) {
    if (column.is_nullable()) {
      if (OB_FAIL(drop_not_null_constraint(column))) {
      }
    } else {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "modify column that is being validating");
    }
  } else if (ori_column.is_nullable() && !column.is_nullable() && !column.is_autoincrement()) {
    column.set_nullable(true);
    if (OB_FAIL(resolve_not_null_constraint_node(column))) {
    }
  }
  return ret;
}

int ObAlterTableResolver::resolve_column_index(const ObString &column_name)
{
  int ret = OB_SUCCESS;
  sort_column_array_.reset();
  storing_column_set_.reset();
  obcall::ObCreateIndexArg *create_index_arg = NULL;
  void *tmp_ptr = NULL;
  if (OB_ISNULL(allocator_)) {
      ret = OB_NOT_INIT;
      SQL_RESV_LOG(WARN, "allocator is null");
  } else if (NULL == (tmp_ptr = (ObCreateIndexArg *)allocator_->alloc(sizeof(obcall::ObCreateIndexArg)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    SQL_RESV_LOG(ERROR, "failed to allocate memory", K(ret));
  } else {
    create_index_arg = new (tmp_ptr) ObCreateIndexArg();
    obcall::ObColumnSortItem sort_item;
    if (OB_FAIL(ob_write_string(*allocator_, column_name, sort_item.column_name_))) {
    } else if (OB_FAIL(add_sort_column(sort_item, *create_index_arg))) {
    } else {
      ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
      bool is_unique_key = true;
      if (OB_ISNULL(alter_table_stmt)) {
        ret = OB_ERR_UNEXPECTED;
        SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
      } else if (OB_FAIL(generate_index_arg(*create_index_arg, is_unique_key))) {
      } else if (OB_FAIL(alter_table_stmt->add_index_arg(create_index_arg))) {
      } else {
        alter_table_stmt->set_alter_table_index();
        storing_column_set_.reset();  //storing column for each index
        sort_column_array_.reset();   //column for each index
      }
    }
  }

  return ret;
}

int ObAlterTableResolver::resolve_drop_column(
    const peg::Node &node,
    ObReducedVisibleColSet &reduced_visible_col_set,
    ObReducedVisibleColSet &drop_column_names_set)
{
  int ret = OB_SUCCESS;
  ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
  if (OB_UNLIKELY(nullptr == alter_table_stmt || nullptr == table_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "unexpected null", KR(ret), KP(alter_table_stmt), KP(table_schema_));
  } else {
    AlterColumnSchema alter_column_schema;
    {
      alter_column_schema.reset();
      if (OB_FAIL(resolve_column_definition_ref(alter_column_schema,
                                                        peg::alter_column_name(node), true))) {
      } else if (OB_FAIL(alter_column_schema.set_column_name(alter_column_schema.get_origin_column_name()))) {
      } else {
        if (OB_SUCC(ret)) {
          alter_column_schema.alter_type_ = OB_DDL_DROP_COLUMN;
          ObAlterTableArg::AlterAlgorithm algorithm = ObAlterTableArg::AlterAlgorithm::INPLACE;
          if (check_can_drop_column_instant()) {
            algorithm = ObAlterTableArg::AlterAlgorithm::INSTANT;
          } else {
            algorithm = ObAlterTableArg::AlterAlgorithm::INPLACE;
          }
          alter_table_stmt->get_alter_table_arg().alter_algorithm_ = algorithm;
        }
      }
      if (OB_SUCC(ret)) {
        ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
        if (OB_ISNULL(alter_table_stmt)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
        } else if (OB_FAIL(check_column_in_foreign_key(
                              *table_schema_,
                              alter_column_schema.get_origin_column_name(),
                              true /* is_drop_column */))) {
        } else if (OB_FAIL(check_drop_column_is_partition_key(*table_schema_,
                                                              alter_column_schema.get_origin_column_name()))) {
        } else if (OB_FAIL(alter_table_stmt->add_column(alter_column_schema))){
        }
      }
      if (OB_SUCC(ret)) {
        const ObString &column_name = alter_column_schema.get_origin_column_name();
        ObColumnSchemaHashWrapper col_key(column_name);
        if (OB_FAIL(drop_column_names_set.set_refactored(col_key))) {
          if (OB_HASH_EXIST == ret) {
            //In mysql mode, OB will check whether a column is dropped twice on rootserver
            //So don't return error here
            ret = OB_SUCCESS;
          } else {
          }
        }

        if (FAILEDx(reduced_visible_col_set.set_refactored(col_key))) {
          if (OB_HASH_EXIST == ret) {
            //In mysql mode, OB will check whether a column is dropped twice on rootserver
            //So don't return error here
            ret = OB_SUCCESS;
          }
          if (OB_FAIL(ret)) {
          }
        }
      }
    }
    if (FAILEDx(check_column_in_check_constraint(*table_schema_, drop_column_names_set, alter_table_stmt))) {
    }
  }
  return ret;
}

int ObAlterTableResolver::fill_column_schema_according_stat(const ObColumnResolveStat &stat,
                                                            AlterColumnSchema &alter_column_schema)
{
  int ret = OB_SUCCESS;
  bool explicit_value = false;
  if (OB_UNLIKELY(NULL == session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_RESV_LOG(WARN, "session info is NULL", K(ret));
  } else if (ObTimestampType == alter_column_schema.get_data_type()) {
    if (OB_FAIL(session_info_->get_explicit_defaults_for_timestamp(explicit_value))) {
    } else if (!explicit_value && !alter_column_schema.is_generated_column()) {
      alter_column_schema.check_timestamp_column_order_ = true;
    }
  }

  if (OB_SUCC(ret)) {
    alter_column_schema.is_no_zero_date_ = is_no_zero_date(session_info_->get_sql_mode());
    alter_column_schema.is_set_nullable_ = stat.is_set_null_;
    alter_column_schema.is_set_default_ = stat.is_set_default_value_;
    alter_column_schema.is_primary_key_ = stat.is_primary_key_;
    alter_column_schema.is_unique_key_ = stat.is_unique_key_;
    alter_column_schema.is_autoincrement_ = stat.is_autoincrement_;
    alter_column_schema.alter_type_ = OB_DDL_ADD_COLUMN;
  }

  return ret;
}


int ObAlterTableResolver::is_exist_item_type(rust::Slice<const peg::Node> actions,
                                             const peg::NodeKind kind,
                                             bool &is_exist)
{
  is_exist = false;
  for (const auto &action : actions) {
    is_exist |= action.kind() == kind;
  }
  return OB_SUCCESS;
}

int ObAlterTableResolver::resolve_rename_column(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  {
    ObString new_column_name;
    AlterColumnSchema alter_column_schema;
    if (OB_FAIL(resolve_column_definition_ref(alter_column_schema, peg::alter_column_name(node), true))) {
    } else if (OB_FAIL(resolve_column_name(new_column_name, &peg::alter_column_new_name(node)))) {
    }
    const ObString origin_column_name = alter_column_schema.get_origin_column_name();
    const ObColumnSchemaV2 *origin_col_schema = NULL;
    ObAlterTableStmt *alter_table_stmt = get_alter_table_stmt();
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_ISNULL(alter_table_stmt)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_RESV_LOG(WARN, "alter table stmt should not be null", K(ret));
    } else if (OB_FAIL(schema_checker_->get_column_schema( table_schema_->get_table_id(),
                                                          origin_column_name,
                                                          origin_col_schema,
                                                          false))) {
      if (ret == OB_ERR_BAD_FIELD_ERROR) {
        LOG_USER_ERROR(OB_ERR_BAD_FIELD_ERROR,
                       origin_column_name.length(), origin_column_name.ptr(),
                       table_schema_->get_table_name_str().length(),
                       table_schema_->get_table_name_str().ptr());
      }
      SQL_RESV_LOG(WARN, "fail to get origin column schema", K(ret));
    } else if (OB_FAIL(alter_column_schema.assign(*origin_col_schema))) {
    } else if (OB_FAIL(alter_column_schema.set_origin_column_name(origin_column_name))) {
    } else if (OB_FAIL(alter_column_schema.set_column_name(new_column_name))) {
    } else if (OB_FAIL(check_mysql_rename_column(alter_column_schema, *table_schema_,
                                                 *alter_table_stmt))) {
    } else {
      //rs end reuse ddl_change_column
      alter_column_schema.alter_type_ = OB_DDL_CHANGE_COLUMN;
      if (OB_FAIL(alter_table_stmt->add_column(alter_column_schema))) {
      }
    }
  }
  return ret;
}


int ObAlterTableResolver::check_mysql_rename_column(const AlterColumnSchema &alter_column_schema,
                                                    const ObTableSchema &origin_table_schema,
                                                    ObAlterTableStmt &alter_table_stmt)
{
  int ret = OB_SUCCESS;
  ObColumnSchemaV2 *column = nullptr;
  const ObPartitionOption &part_opt = origin_table_schema.get_part_option();
  // check table part key deps
  if (alter_column_schema.is_tbl_part_key_column()
      && part_opt.get_part_func_type() != PARTITION_FUNC_TYPE_KEY_IMPLICIT) {
    ret = OB_ERR_DEPENDENT_BY_PARTITION_FUNC;
    LOG_USER_ERROR(OB_ERR_DEPENDENT_BY_PARTITION_FUNC,
                   alter_column_schema.get_origin_column_name().length(),
                   alter_column_schema.get_origin_column_name().ptr());
  }
  // generated column deps
  for (auto col_iter = origin_table_schema.column_begin();
       OB_SUCC(ret) && (col_iter != origin_table_schema.column_end()); col_iter++) {
    if (OB_ISNULL(column = *col_iter)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (column->has_cascaded_column_id(alter_column_schema.get_column_id())
               && column->is_generated_column()) {
      if (!column->is_hidden()) {
        ret = OB_ERR_DEPENDENT_BY_GENERATED_COLUMN;
        LOG_USER_ERROR(OB_ERR_DEPENDENT_BY_GENERATED_COLUMN,
                       alter_column_schema.get_origin_column_name().length(),
                       alter_column_schema.get_origin_column_name().ptr());
      } else if (column->is_func_idx_column()) { // renname column with func index deps is forbidden
        ret = OB_ERR_DEPENDENT_BY_FUNCTIONAL_INDEX;
        LOG_USER_ERROR(OB_ERR_DEPENDENT_BY_FUNCTIONAL_INDEX,
                       alter_column_schema.get_origin_column_name().length(),
                       alter_column_schema.get_origin_column_name().ptr());
      }
    }
  }

  // constraints deps
  for (auto cst_iter = origin_table_schema.constraint_begin();
       OB_SUCC(ret) && (cst_iter != origin_table_schema.constraint_end()); cst_iter++) {
    if ((*cst_iter)->get_column_cnt() == 0
        || (*cst_iter)->get_constraint_type() != CONSTRAINT_TYPE_CHECK) {
      continue;
    } else {
      bool dropped_cst = false;
      AlterTableSchema &alter_table_schema = alter_table_stmt.get_alter_table_arg().alter_table_schema_;
      for (auto it = alter_table_schema.constraint_begin();
           OB_SUCC(ret)
           && !dropped_cst
           && (alter_table_stmt.get_alter_table_arg().alter_constraint_type_
                == ObAlterTableArg::DROP_CONSTRAINT)
           && it != alter_table_schema.constraint_end();
           it++) {
        if ((*it)->get_constraint_id() == (*cst_iter)->get_constraint_id()) {
          dropped_cst = true;
        }
      }
      for (auto it = (*cst_iter)->cst_col_begin();
           !dropped_cst && OB_SUCC(ret) && it != (*cst_iter)->cst_col_end(); it++) {
        if (*it == alter_column_schema.get_column_id()) {
          ret = OB_ERR_DROP_COL_REFERENCED_MULTI_COLS_CONSTRAINT;
          LOG_USER_ERROR(OB_ERR_DROP_COL_REFERENCED_MULTI_COLS_CONSTRAINT,
                         (*cst_iter)->get_constraint_name_str().length(),
                         (*cst_iter)->get_constraint_name_str().ptr(),
                         alter_column_schema.get_origin_column_name().length(),
                         alter_column_schema.get_origin_column_name().ptr());
        }
      }
    }
  }
  return ret;
}

/*
* chech the droped column is partition key,
* if ture, forbid the action
*/
int ObAlterTableResolver::check_drop_column_is_partition_key(const ObTableSchema &table_schema, const ObString &column_name)
{
  int ret=OB_SUCCESS;
  if (!table_schema.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    SQL_RESV_LOG(WARN, "invalid arguemnt", K(ret), K(table_schema));
  } else {
    const ObColumnSchemaV2 *origin_column = table_schema.get_column_schema(column_name);
    if (OB_ISNULL(origin_column)) {
      // do nothing
      // According to the column name, the column cannot be found because it does not exist in the table. Later, it will be checked again on the RS side whether the column exists in the table, and different errors will be reported on the RS side according to different operations
    } else if (origin_column->is_tbl_part_key_column()){
      ret = OB_ERR_DEPENDENT_BY_PARTITION_FUNC;
      LOG_USER_ERROR(OB_ERR_DEPENDENT_BY_PARTITION_FUNC,
                     column_name.length(),
                     column_name.ptr());
    }
  }
  return ret;
}

int ObAlterTableResolver::check_semistruct_encoding_type(const ObTableSchema &origin_schema, const ObTableSchema &alter_schema)
{
  int ret = OB_SUCCESS;
  // skip check if not modify semistruct encoding options and store format
  if (! alter_table_bitset_.has_member(obcall::ObAlterTableArg::SEMISTRUCT_ENCODING_TYPE)
      && ! alter_table_bitset_.has_member(obcall::ObAlterTableArg::STORE_FORMAT)) { 
  // skip check if semistruct_encoding is disable 
  } else if ((alter_table_bitset_.has_member(obcall::ObAlterTableArg::SEMISTRUCT_ENCODING_TYPE) && ! alter_schema.get_semistruct_encoding_type().is_enable_semistruct_encoding())
    || (! alter_table_bitset_.has_member(obcall::ObAlterTableArg::SEMISTRUCT_ENCODING_TYPE) && ! origin_schema.get_semistruct_encoding_type().is_enable_semistruct_encoding())) {
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "semistruct encoding is not supported");
  }
  return ret;
}

} //namespace common
} //namespace oceanbase
