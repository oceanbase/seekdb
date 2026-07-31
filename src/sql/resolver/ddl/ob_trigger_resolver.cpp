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
#include "sql/resolver/ddl/ob_trigger_resolver.h"
#include "sql/resolver/ddl/ob_trigger_source_builder.h"
#include "sql/resolver/ddl/ob_create_routine_resolver.h"
#include "sql/pl/parser/parse_stmt_item_type.h"
#include "sql/pl/ob_pl_package.h"
#include "sql/pl/ob_pl_build.h"
#include "share/schema/ob_trigger_info.h"

namespace oceanbase
{
namespace sql
{
using namespace common;
using namespace obcall;
using namespace share::schema;
using namespace pl;

int ObTriggerResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObItemType stmt_type = parse_tree.type_;
  switch (stmt_type) {
  case T_TG_CREATE: {
    ObCreateTriggerStmt *stmt = create_stmt<ObCreateTriggerStmt>();
    OV (OB_NOT_NULL(stmt), OB_ALLOCATE_MEMORY_FAILED);
    OZ (resolve_create_trigger_stmt(parse_tree, stmt->get_trigger_arg()));
    break;
  }
  case T_TG_DROP: {
    ObDropTriggerStmt *stmt = create_stmt<ObDropTriggerStmt>();
    OV (OB_NOT_NULL(stmt), OB_ALLOCATE_MEMORY_FAILED);
    OZ (resolve_drop_trigger_stmt(parse_tree, stmt->get_trigger_arg()));
    OZ (get_drop_trigger_stmt_table_name(stmt));
    break;
  }
  case T_TG_ALTER: {
    ObAlterTriggerStmt *stmt = create_stmt<ObAlterTriggerStmt>();
    OV (OB_NOT_NULL(stmt), OB_ALLOCATE_MEMORY_FAILED);
    OZ (resolve_alter_trigger_stmt(parse_tree, stmt->get_trigger_arg()));
    break;
  }
  default:
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid stmt type", K(ret), K(stmt_type));
  }
  return ret;
}

int ObTriggerResolver::get_drop_trigger_stmt_table_name(ObDropTriggerStmt *stmt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(stmt)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("drop trigger stmt is NULL", K(ret));
  } else {
    const obcall::ObDropTriggerArg &arg = stmt->get_trigger_arg();

    const ObString &trigger_database = arg.trigger_database_;
    const ObString &trigger_name = arg.trigger_name_;
    ObSchemaGetterGuard *schema_guard = NULL;
    const ObDatabaseSchema *db_schema = NULL;
    uint64_t trigger_database_id = OB_INVALID_ID;
    const ObTriggerInfo *trigger_info = NULL;
    const ObTableSchema *table = NULL;

    CK (OB_NOT_NULL(schema_checker_));
    CK (OB_NOT_NULL(schema_checker_->get_schema_guard()));
    OX (schema_guard = schema_checker_->get_schema_guard());
    if (OB_SUCC(ret)) {
      if(OB_FAIL(schema_guard->get_database_schema( trigger_database, db_schema))) {
        LOG_WARN("get database schema failed", K(ret));
      } else if (NULL == db_schema) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_USER_ERROR(OB_ERR_BAD_DATABASE, trigger_database.length(), trigger_database.ptr());
      } else if (db_schema->is_or_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("Can't not operate db in recyclebin",
                 K(trigger_database), K(trigger_database_id), K(*db_schema), K(ret));
      } else if (OB_INVALID_ID == (trigger_database_id = db_schema->get_database_id())) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_WARN("database id is invalid",
                 K(trigger_database), K(trigger_database_id), K(*db_schema), K(ret));
      } else if (OB_FAIL(schema_guard->get_trigger_info( trigger_database_id,
                                                       trigger_name, trigger_info))) {
        LOG_WARN("get trigger info failed", K(ret), K(trigger_database), K(trigger_name));
      } else if (OB_ISNULL(trigger_info)) {
        ret = OB_ERR_TRIGGER_NOT_EXIST;
      } else if (trigger_info->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("trigger is in recyclebin", K(ret),
                 K(trigger_info->get_trigger_id()), K(trigger_info->get_trigger_name()));
      } else if (OB_FAIL(schema_guard->get_table_schema(
                                                  trigger_info->get_base_object_id(),
                                                  table))) {
       LOG_WARN("Failed to get table schema",
                   K(trigger_info->get_base_object_id()), K(ret));
      } else if (OB_ISNULL(table)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Table schema should not be NULL", K(ret));
      } else {
        stmt->trigger_table_name_ = table->get_table_name_str();
      }
      if (OB_ERR_TRIGGER_NOT_EXIST == ret || OB_ERR_BAD_DATABASE == ret) {
        ret = OB_ERR_TRIGGER_NOT_EXIST;
        stmt->is_exist = false;
        if (arg.if_exist_) {
          ret = OB_SUCCESS;
        } else {
          LOG_MYSQL_USER_ERROR(OB_ERR_TRIGGER_NOT_EXIST);
        }
        LOG_WARN("trigger not exist", K(arg.trigger_database_), K(arg.trigger_name_), K(ret));
      }
    }
  }
  return ret;
}

int ObTriggerResolver::resolve_sp_definer(const ParseNode *parse_node,
                                          ObCreateTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(schema_checker_));
  CK(OB_NOT_NULL(schema_checker_->get_schema_guard()));
  CK(OB_NOT_NULL(session_info_));
  CK(OB_NOT_NULL(allocator_));
  ObString user_name, host_name;
  ObString cur_user_name, cur_host_name;
  cur_user_name = session_info_->get_user_name();
  cur_host_name = session_info_->get_host_name();
  if (OB_NOT_NULL(parse_node)) {
    CK(T_USER_WITH_HOST_NAME == parse_node->type_);
    if (OB_SUCC(ret)) {
      const ParseNode *user_node = parse_node->children_[0];
      const ParseNode *host_node = parse_node->children_[1];

      if (OB_ISNULL(user_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("user must be specified", K(ret));
      } else {
        // Need to check if the current user has superuser permissions or set user ID permissions
        if (!session_info_->has_user_super_privilege()) {
          ret = OB_ERR_NO_PRIVILEGE;
          LOG_WARN("no privilege", K(ret));
        } else {
          user_name.assign_ptr(user_node->str_value_, static_cast<int32_t>(user_node->str_len_));
          // Need to distinguish between current_user and "current_user", the former needs to obtain the current user and host, the latter exists as a username
          if (0 == user_name.case_compare("current_user") && T_IDENT == user_node->type_) {
            user_name = cur_user_name;
            host_name = cur_host_name;
          } else if (OB_ISNULL(host_node)) {
            host_name.assign_ptr("%", 1);
          } else {
            host_name.assign_ptr(host_node->str_value_, static_cast<int32_t>(host_node->str_len_));
          }
        }
        if (OB_SUCC(ret)) {
          // Check if user@host is in the mysql.user table
          const ObUserInfo* user_info = nullptr;
          if (OB_FAIL(schema_checker_->get_schema_guard()->get_user_info(user_name,
                                                                         host_name,
                                                                         user_info))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("fail to get_user_info", K(ret));
          } else if (OB_ISNULL(user_info)) {
            LOG_USER_WARN(OB_ERR_USER_NOT_EXIST, user_name.length(), user_name.ptr());
            pl::ObPL::insert_error_msg(OB_ERR_USER_NOT_EXIST);
            ret = OB_SUCCESS;
          }
        }
      }
    }
  } else {
    // When definer is not specified, it defaults to the current user and host
    user_name = cur_user_name;
    host_name = cur_host_name;
  }
  if (OB_SUCC(ret)) {
    // user@host as a whole is stored in the priv_user field
    char tmp_buf[common::OB_MAX_USER_NAME_LENGTH + common::OB_MAX_HOST_NAME_LENGTH + 2] = {};
    snprintf(tmp_buf, sizeof(tmp_buf), "%.*s@%.*s", user_name.length(), user_name.ptr(),
                                                    host_name.length(), host_name.ptr());

    ObString priv_user(tmp_buf);
    if (OB_FAIL(ObSQLUtils::convert_sql_text_to_schema_for_storing(
              *allocator_, session_info_->get_dtc_params(), priv_user))) {
      LOG_WARN("fail to convert charset", K(ret));
    } else if (OB_FAIL(trigger_arg.trigger_info_.set_trigger_priv_user(priv_user))) {
      LOG_WARN("failed to set priv user", K(ret));
    }
  }

  return ret;
}

int ObTriggerResolver::resolve_create_trigger_stmt(const ParseNode &parse_node,
                                                   ObCreateTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  OV (parse_node.type_ == T_TG_CREATE, OB_ERR_UNEXPECTED, parse_node.type_);
  OV (parse_node.num_child_ == (2), OB_ERR_UNEXPECTED, parse_node.num_child_);
  OV (OB_NOT_NULL(parse_node.children_));
  OV (OB_NOT_NULL(parse_node.children_[1]));    // trigger source.
  OV (OB_NOT_NULL(session_info_));
  if (OB_SUCC(ret) && parse_node.int32_values_[1] == 1) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "editionable in create trigger");
  }
  OX (trigger_arg.with_replace_ = (parse_node.int32_values_[0] != 0));
  OX (trigger_arg.trigger_info_.set_owner_id(session_info_->get_user_id()));
  OZ (resolve_sp_definer(parse_node.children_[0], trigger_arg));
  OZ (resolve_trigger_source(*parse_node.children_[1], trigger_arg));
  if (OB_SUCC(ret)) {
    const ObTableSchema *table_schema = NULL;
    CK (OB_NOT_NULL(schema_checker_));
    CK (OB_NOT_NULL(schema_checker_->get_schema_guard()));
    OZ (schema_checker_->get_schema_guard()->get_table_schema(
                                                              trigger_arg.trigger_info_.get_base_object_id(),
                                                              table_schema));
    CK (OB_NOT_NULL(table_schema));
    OZ (trigger_arg.based_schema_object_infos_.push_back(ObBasedSchemaObjectInfo(table_schema->get_table_id(),
                                                                                TABLE_SCHEMA,
                                                                                table_schema->get_schema_version())));
  }
  if (OB_SUCC(ret)) {
    ObErrorInfo &error_info = trigger_arg.error_info_;
    error_info.collect_error_info(&(trigger_arg.trigger_info_));
  }
  return ret;
}

int ObTriggerResolver::resolve_drop_trigger_stmt(const ParseNode &parse_node,
                                                 ObDropTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  OV (parse_node.type_ == T_TG_DROP, OB_ERR_UNEXPECTED, parse_node.type_);
  OV (parse_node.num_child_ == 1, OB_ERR_UNEXPECTED, parse_node.num_child_);
  OV (OB_NOT_NULL(parse_node.children_));
  OV (OB_NOT_NULL(parse_node.children_[0]));    // trigger name.
  OV (OB_NOT_NULL(session_info_));
  OX ();
  OZ (resolve_schema_name(*parse_node.children_[0], trigger_arg.trigger_database_, trigger_arg.trigger_name_));
  OV (OB_NOT_NULL(schema_checker_));
  OX (trigger_arg.if_exist_ = parse_node.value_);
  return ret;
}

int ObTriggerResolver::resolve_alter_trigger_stmt(const ParseNode &parse_node,
                                                  ObAlterTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  ObString trigger_db_name;
  ObString trigger_name;
  const ObTriggerInfo *old_tg_info = NULL;
  ObTriggerInfo new_tg_info;
  OV (parse_node.type_ == T_TG_ALTER, OB_ERR_UNEXPECTED, parse_node.type_);
  OV (parse_node.num_child_ == 2, OB_ERR_UNEXPECTED, parse_node.num_child_);
  OV (OB_NOT_NULL(parse_node.children_));
  OV (OB_NOT_NULL(parse_node.children_[0]));  //trigger name.
  OV (OB_NOT_NULL(parse_node.children_[1]));  //alter clause.
  OV (OB_NOT_NULL(session_info_) && OB_NOT_NULL(schema_checker_));
  OZ (resolve_schema_name(*parse_node.children_[0], trigger_db_name, trigger_name));
  OZ (schema_checker_->get_trigger_info( trigger_db_name,
                                        trigger_name, old_tg_info));
  if (OB_SUCC(ret) && OB_ISNULL(old_tg_info)) {
    ret = OB_ERR_TRIGGER_NOT_EXIST;
    LOG_USER_ERROR(OB_ERR_TRIGGER_NOT_EXIST);
  }
  OZ (ObDDLResolver::ob_add_ddl_dependency(old_tg_info->get_trigger_id(),
                                           TRIGGER_SCHEMA,
                                           old_tg_info->get_schema_version(),
                                           trigger_arg));
  OZ (new_tg_info.deep_copy(*old_tg_info));
  OZ (resolve_alter_clause(*parse_node.children_[1], new_tg_info, trigger_arg.is_set_status_));
  OZ (trigger_arg.trigger_infos_.push_back(new_tg_info));
  return ret;
}

int ObTriggerResolver::resolve_trigger_source(const ParseNode &parse_node,
                                              ObCreateTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  ObString trigger_name;
  ObString trigger_body;
  OV (parse_node.type_ == T_TG_SOURCE, OB_ERR_UNEXPECTED, parse_node.type_);
  OV (parse_node.num_child_ == 2, OB_ERR_UNEXPECTED, parse_node.num_child_);
  OV (OB_NOT_NULL(parse_node.children_));
  OV (OB_NOT_NULL(parse_node.children_[0]));    // trigger name.
  OV (OB_NOT_NULL(parse_node.children_[1]));    // trigger definition.
  OZ (resolve_schema_name(*parse_node.children_[0], trigger_arg.trigger_database_, trigger_name));
  OV (OB_NOT_NULL(session_info_));
  OV (OB_NOT_NULL(schema_checker_));
  OZ (trigger_arg.trigger_info_.set_trigger_name(trigger_name), trigger_name);
  trigger_body = ObString(parse_node.children_[1]->str_len_, parse_node.children_[1]->str_value_);
  OZ (ObSQLUtils::convert_sql_text_to_schema_for_storing(
        *allocator_, session_info_->get_dtc_params(), trigger_body));
  OZ (trigger_arg.trigger_info_.set_trigger_body(trigger_body));
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (T_TG_SIMPLE_DML != parse_node.children_[1]->type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected trigger definition", K(ret), K(parse_node.children_[1]->type_));
  } else {
    OX (trigger_arg.trigger_info_.set_simple_dml_type());
    OZ (resolve_simple_dml_trigger(*parse_node.children_[1], trigger_arg));
  }
  if (OB_SUCC(ret) && parse_node.value_ != 0) {
    OX (trigger_arg.with_if_not_exist_ = parse_node.value_);
  }
  return ret;
}

int ObTriggerResolver::resolve_simple_dml_trigger(const ParseNode &parse_node,
                                                  ObCreateTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  OV (parse_node.type_ == T_TG_SIMPLE_DML, OB_ERR_UNEXPECTED, parse_node.type_);
  OV (parse_node.num_child_ == 4, OB_ERR_UNEXPECTED, parse_node.num_child_);
  OV (OB_NOT_NULL(parse_node.children_));
  OV (OB_NOT_NULL(parse_node.children_[0]));    // dml event.
  OV (OB_NOT_NULL(parse_node.children_[3]));    // simple trigger body.
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    OX (LOG_DEBUG("TRIGGER", K(parse_node.int16_values_[0])));
    OV (OB_NOT_NULL(parse_node.children_[1])); // mysql mode, trigger_name
    if (OB_SUCC(ret)) {
      if (T_BEFORE == parse_node.int16_values_[0]) {
        trigger_arg.trigger_info_.add_before_row();
      } else if (T_AFTER == parse_node.int16_values_[0]) {
        trigger_arg.trigger_info_.add_after_row();
      } else {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("parse_node timing points is invalid", K(parse_node.int16_values_[0]), K(ret));
      }
      if (OB_SUCC(ret)) {
        switch (parse_node.children_[0]->type_)
        {
        case T_INSERT:
          trigger_arg.trigger_info_.add_insert_event();
          break;
        case T_UPDATE:
          trigger_arg.trigger_info_.add_update_event();
          break;
        case T_DELETE:
          trigger_arg.trigger_info_.add_delete_event();
          break;
        default:
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("parse_node type is invalid", K(ret), K(parse_node.children_[0]->type_));
          break;
        }
      }
      OX (trigger_arg.trigger_info_.set_enable());
      OZ (trigger_arg.trigger_info_.set_ref_old_name(REF_OLD));
      OZ (trigger_arg.trigger_info_.set_ref_new_name(REF_NEW));
      OZ (trigger_arg.trigger_info_.set_ref_parent_name(REF_PARENT));
    }
    OZ (resolve_schema_name(*parse_node.children_[1],
                            trigger_arg.base_object_database_, trigger_arg.base_object_name_));
    OZ (resolve_base_object(trigger_arg, false));
  }
  OZ (resolve_order_clause(parse_node.children_[2], trigger_arg));
  OZ (resolve_trigger_body(*parse_node.children_[3], trigger_arg));
  OZ (fill_package_info(trigger_arg.trigger_info_));
  return ret;
}




int ObTriggerResolver::resolve_trigger_body(const ParseNode &parse_node,
                                            ObCreateTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  ObTriggerInfo &trigger_info = trigger_arg.trigger_info_;
  ObString tg_body;
  CK (OB_NOT_NULL(session_info_));
  OV (OB_NOT_NULL(parse_node.str_value_) && parse_node.str_len_ > 0);
  OX (tg_body.assign_ptr(parse_node.str_value_,
                         static_cast<int32_t>(parse_node.str_len_)));
  OX (LOG_DEBUG("TRIGGER", K(tg_body)));
  OZ (ObTriggerSourceBuilder::build_package_source(
      trigger_info,
      trigger_arg.base_object_database_,
      trigger_arg.base_object_name_,
      parse_node,
      session_info_->get_dtc_params()));
  if (OB_SUCC(ret)) {
    ObString procedure_source;
    pl::ObPLParser parser(*allocator_, session_info_->get_charsets4parser(), session_info_->get_sql_mode());
    ObStmtNodeTree *parse_tree = NULL;
    OZ (ObTriggerSourceBuilder::build_procedure_source(
        trigger_info,
        *allocator_,
        trigger_arg.base_object_database_,
        trigger_arg.base_object_name_,
        parse_node,
        session_info_->get_dtc_params(),
        procedure_source));
    OZ (parser.parse_package(procedure_source, parse_tree, session_info_->get_dtc_params(), NULL, true));
    if (OB_SUCC(ret)) {
      params_.tg_timing_event_ = static_cast<int64_t>(trigger_info.get_timing_event());
      HEAP_VAR(ObCreateProcedureResolver, resolver, params_) {
        bool saved_trigger_flag = session_info_->is_for_trigger_package();
        session_info_->set_for_trigger_package(true);
        if (OB_FAIL(resolver.resolve(*parse_tree->children_[0]))) {
          LOG_WARN("resolve trigger procedure failed", K(parse_tree->children_[0]->type_), K(ret));
        }
        // Regardless of whether the execution is successful, restore the original value of this variable
        session_info_->set_for_trigger_package(saved_trigger_flag);
      }
    }
  }
  return ret;
}

int ObTriggerResolver::resolve_schema_name(const ParseNode &parse_node,
                                           ObString &database_name,
                                           ObString &schema_name)
{
  int ret = OB_SUCCESS;
  OV (OB_NOT_NULL(session_info_));
  OZ (ObResolverUtils::resolve_sp_name(*session_info_, parse_node, database_name, schema_name));
  return ret;
}

int ObTriggerResolver::resolve_alter_clause(const ParseNode &alter_clause,
                                            ObTriggerInfo &tg_info,
                                            bool &is_set_status)
{
  int ret = OB_SUCCESS;
  CK (OB_LIKELY(OB_NOT_NULL(schema_checker_)));
  CK (OB_LIKELY(OB_NOT_NULL(schema_checker_->get_schema_guard())));
  CK (OB_LIKELY(OB_NOT_NULL(allocator_)));
  CK (OB_LIKELY(T_TG_ALTER_OPTIONS == alter_clause.type_));
  if (FAILEDx(TRIGGER_ALTER_IF_EDITIONABLE == alter_clause.int16_values_[0])) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("alter editionable is not supported yet!", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "alter editionable");
  } else if (TRIGGER_ALTER_IF_ENABLE == alter_clause.int16_values_[0]) {
    is_set_status = true;
    if (T_ENABLE == static_cast<ObItemType>(alter_clause.int16_values_[1])) {
      tg_info.set_enable();
    } else {
      tg_info.set_disable();
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected alter trigger option", K(ret), K(alter_clause.int16_values_[0]));
  }
  return ret;
}

int ObTriggerResolver::fill_package_info(ObTriggerInfo &trigger_info)

{
  int ret = OB_SUCCESS;
  char buf[OB_MAX_PROC_ENV_LENGTH];
  int64_t pos = 0;
  ObString pl_exec_env;
  OV (OB_NOT_NULL(session_info_));
  OZ (ObExecEnv::gen_exec_env(*session_info_, buf, OB_MAX_PROC_ENV_LENGTH, pos));
  OX (pl_exec_env.assign_ptr(buf, static_cast<int32_t>(pos)));
  OX (trigger_info.set_package_flag(0));
  OZ (trigger_info.set_package_exec_env(pl_exec_env));
  OX (trigger_info.set_sql_mode(session_info_->get_sql_mode()));
  return ret;
}

int ObTriggerResolver::resolve_base_object(ObCreateTriggerArg &tg_arg,
                                           bool search_public_schema) {
  int ret = OB_SUCCESS;
  uint64_t tg_db_id = OB_INVALID_ID;
  ObTriggerInfo &tg_info = tg_arg.trigger_info_;

  ObSchemaGetterGuard *schema_guard = schema_checker_->get_schema_guard();
  const ObTableSchema *table_schema = NULL;
  OV (OB_NOT_NULL(schema_guard));
  OZ (schema_checker_->get_database_id(tg_arg.trigger_database_, tg_db_id));
  OZ (schema_guard->get_table_schema( tg_arg.base_object_database_,
                                     tg_arg.base_object_name_,
                                     false/*is_index*/, table_schema));
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table or view does not exist", K(tg_db_id),
              K(tg_arg.base_object_name_), K(ret));
    LOG_MYSQL_USER_ERROR(OB_TABLE_NOT_EXIST, tg_arg.base_object_database_.ptr(),
                          tg_arg.base_object_name_.ptr());
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_BAD_TABLE;
    LOG_WARN("table schema is invalid", K(ret));
  } else if (table_schema->is_in_recyclebin()) {
    ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    LOG_WARN("table is in recyclebin", K(ret));
  } else if (!table_schema->is_user_table()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("simple dml trigger only support on user table", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "simple dml trigger isn't used on user table");
  } else {
    uint64_t trigger_id = OB_INVALID_ID;
    const ObTriggerInfo *trigger_info = NULL;

    const ObIArray<uint64_t> &trigger_list = table_schema->get_trigger_list();
    if (tg_db_id != table_schema->get_database_id()) {
      ret = OB_ERR_TRIGGER_IN_WRONG_SCHEMA;
      LOG_WARN("trigger database must same as table database", K(tg_db_id),
               K(table_schema->get_database_id()), K(ret));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
      OX (trigger_id = trigger_list.at(i));
      OZ (schema_guard->get_trigger_info( trigger_id, trigger_info), trigger_id);
      OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_id);
    }
  }
  if (OB_SUCC(ret)) {
    tg_info.set_database_id(tg_db_id);
    tg_info.set_base_object_type(TABLE_SCHEMA);
    tg_info.set_base_object_id(table_schema->get_table_id());
  }
  return ret;
}

int ObTriggerResolver::resolve_order_clause(const ParseNode *parse_node, ObCreateTriggerArg &trigger_arg)
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("resolve trigger order clause start", K(ret));
  if (OB_NOT_NULL(parse_node)) {
    ObTriggerInfo &trg_info = trigger_arg.trigger_info_;
    OV (T_TG_ORDER == parse_node->type_ && 1 == parse_node->num_child_ && NULL != parse_node->children_[0]);
    if (OB_SUCC(ret)) {
      ObString ref_trg_db_name;
      ObString ref_trg_name;
      trg_info.set_order_type(static_cast<ObTriggerInfo::OrderType>(parse_node->value_));
      OZ (resolve_schema_name(*parse_node->children_[0], ref_trg_db_name, ref_trg_name), ref_trg_db_name, ref_trg_name);
      OZ (trg_info.set_ref_trg_db_name(ref_trg_db_name));
      OZ (trg_info.set_ref_trg_name(ref_trg_name));
      if (OB_SUCC(ret) && !ref_trg_db_name.empty()) {
        const ObTriggerInfo *ref_trg_info = NULL;
        OV (OB_NOT_NULL(schema_checker_));
        OV (!ref_trg_name.empty(), OB_ERR_UNEXPECTED, ref_trg_db_name, ref_trg_name);
        OZ (schema_checker_->get_trigger_info( ref_trg_db_name, ref_trg_name, ref_trg_info));
        if (OB_FAIL(ret)) {
        } else if (NULL == ref_trg_info) {
          ret = OB_ERR_TRG_ORDER;
          LOG_WARN("ref_trg_info is NULL", K(ref_trg_db_name), K(ref_trg_name), K(ret));
          LOG_MYSQL_USER_ERROR(OB_ERR_TRG_ORDER, ref_trg_name.ptr());
        } else {
          if (!ObTriggerInfo::is_same_timing_event(trg_info, *ref_trg_info)
              || trg_info.get_base_object_id() != ref_trg_info->get_base_object_id()) {
            ret = OB_ERR_TRG_ORDER;
            LOG_WARN("trigger order invalid", K(ref_trg_db_name), K(ref_trg_name), K(ret));
            LOG_MYSQL_USER_ERROR(OB_ERR_TRG_ORDER, ref_trg_name.ptr());
          }
        }
      }
    }
  }
  LOG_DEBUG("resolve trigger order clause end", K(ret));
  return ret;
}

int ObTriggerResolver::analyze_trigger(ObSchemaGetterGuard &schema_guard,
                                       ObSQLSessionInfo *session_info,
                                       ObMySQLProxy *sql_proxy,
                                       ObIAllocator &allocator,
                                       const ObTriggerInfo &trigger_info,
                                       const ObString &db_name,
                                       ObIArray<ObDependencyInfo> &dep_infos)
{
  int ret = OB_SUCCESS;
  CK (OB_LIKELY(OB_NOT_NULL(session_info)));
  CK (OB_LIKELY(OB_NOT_NULL(sql_proxy)));
  if (OB_SUCC(ret)) {
    HEAP_VARS_2((ObPLPackageAST, package_spec_ast, allocator),
                  (ObPLPackageAST, package_body_ast, allocator)) {
      ObPLPackageGuard package_guard{};
      const ObString &pkg_name = trigger_info.get_package_body_info().get_package_name();
      ObString source;
      ObPLBuilder builder(allocator, *session_info, schema_guard, package_guard, *sql_proxy);
      const ObPackageInfo &package_spec_info = trigger_info.get_package_spec_info();
      OZ (package_spec_ast.init(db_name,
                                package_spec_info.get_package_name(),
                                PL_PACKAGE_SPEC,
                                package_spec_info.get_database_id(),
                                package_spec_info.get_package_id(),
                                package_spec_info.get_schema_version(),
                                NULL));
      OZ (ObTriggerSourceBuilder::generate_package_source(
          trigger_info.get_trigger_spec_package_id(trigger_info.get_trigger_id()),
          source, true, schema_guard, allocator));
      OZ (builder.analyze_package(source, NULL, package_spec_ast, true));
      OZ (package_body_ast.init(db_name,
                                pkg_name,
                                PL_PACKAGE_BODY,
                                trigger_info.get_package_body_info().get_database_id(),
                                trigger_info.get_package_body_info().get_package_id(),
                                trigger_info.get_package_body_info().get_schema_version(),
                                &package_spec_ast));
      OZ (ObTriggerSourceBuilder::generate_package_source(
          trigger_info.get_trigger_body_package_id(trigger_info.get_trigger_id()),
          source, false, schema_guard, allocator));
      OZ (builder.analyze_package(source,
                                   &(package_spec_ast.get_body()->get_namespace()),
                                   package_body_ast,
                                   true));
      if (OB_SUCC(ret)) {
        OX (const_cast<ObTriggerInfo&>(trigger_info).set_analyze_flag(package_body_ast.get_analyze_flag()));
      }
    }
  }
  return ret;
}


const ObString ObTriggerResolver::REF_OLD = "OLD";
const ObString ObTriggerResolver::REF_NEW = "NEW";
const ObString ObTriggerResolver::REF_PARENT = "PARENT";

} // namespace sql
} // namespace oceanbase


// ===== trigger package source macro DSL (moved together with the function family from trigger_info.cpp) =====
#define MODE_DELIMITER  ('`')

/************************* mysql mode package *************************/
#define SPEC_BEGIN_MYSQL \
  "PACKAGE %c%.*s%c \n"
#define SPEC_BEFORE_ROW_MYSQL \
  "PROCEDURE before_row(IN OLD %c%.*s%c.%c%.*s%c%%ROWTYPE, INOUT NEW %c%.*s%c.%c%.*s%c%%ROWTYPE);\n"
#define SPEC_AFTER_ROW_MYSQL \
  "PROCEDURE after_row(IN OLD %c%.*s%c.%c%.*s%c%%ROWTYPE, IN NEW %c%.*s%c.%c%.*s%c%%ROWTYPE);\n"
#define SPEC_END_MYSQL \
  "END;\n"

#define PACKAGE_SPEC_FMT_MYSQL \
  SPEC_BEGIN_MYSQL \
  SPEC_BEFORE_ROW_MYSQL \
  SPEC_AFTER_ROW_MYSQL \
  SPEC_END_MYSQL

#define BODY_BEGIN_MYSQL \
  "PACKAGE BODY %c%.*s%c \n"
#define BODY_BEFORE_ROW_MYSQL \
  "PROCEDURE before_row(IN OLD %c%.*s%c.%c%.*s%c%%ROWTYPE, INOUT NEW %c%.*s%c.%c%.*s%c%%ROWTYPE) \n" \
  "BEGIN\n" \
  "%.*s" \
  "END;\n"
#define BODY_AFTER_ROW_MYSQL \
  "PROCEDURE after_row(IN OLD %c%.*s%c.%c%.*s%c%%ROWTYPE, IN NEW %c%.*s%c.%c%.*s%c%%ROWTYPE) \n" \
  "BEGIN\n" \
  "%.*s" \
  "END;\n"
#define BODY_END_MYSQL \
  "END;\n"

#define PACKAGE_BODY_FMT_MYSQL \
  BODY_BEGIN_MYSQL \
  BODY_BEFORE_ROW_MYSQL \
  BODY_AFTER_ROW_MYSQL \
  BODY_END_MYSQL  \
/************************* mysql mode package *************************/

/************************* mysql mode procedure *************************/
#define TRIGGER_PROCEDURE_MYSQL \
  "CREATE PROCEDURE %c%.*s%c(IN OLD %c%.*s%c.%c%.*s%c%%ROWTYPE, %.*s NEW %c%.*s%c.%c%.*s%c%%ROWTYPE) \n" \
  "%.*s \n"
/************************* mysql mode procedure *************************/

namespace oceanbase
{
namespace sql
{

void ObTriggerSourceBuilder::calculate_source_sizes(const ObTriggerInfo &trigger_info,
                                             const ObString &base_object_database,
                                             const ObString &base_object_name,
                                             int64_t &spec_size, int64_t &body_size)
{
  const int64_t spec_params_size = trigger_info.get_trigger_name().length() +
                                   base_object_database.length() * 4 +
                                   base_object_name.length() * 4;
  const int64_t body_params_size = spec_params_size + trigger_info.get_trigger_body().length();
  spec_size = STRLEN(PACKAGE_SPEC_FMT_MYSQL) + spec_params_size;
  body_size = STRLEN(PACKAGE_BODY_FMT_MYSQL) + body_params_size;
  return;
}

int ObTriggerSourceBuilder::fill_package_spec(const ObTriggerInfo &trigger_info,
                                            const ObString &base_object_database,
                                            const ObString &base_object_name,
                                            const int64_t spec_size,
                                            ObString &spec_source,
                                            ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  const ObString &trigger_name = trigger_info.get_trigger_name();
  char delimiter = MODE_DELIMITER;
  char *buf = static_cast<char *>(alloc.alloc(spec_size));
  int64_t buf_len = spec_size;
  int64_t pos = 0;
  OV (OB_NOT_NULL(buf), OB_ALLOCATE_MEMORY_FAILED);
  OZ (BUF_PRINTF(SPEC_BEGIN_MYSQL,
                 delimiter, trigger_name.length(), trigger_name.ptr(), delimiter));
  if (OB_SUCC(ret)) {
    OZ (fill_row_routine_spec(SPEC_BEFORE_ROW_MYSQL,
                              trigger_info, base_object_database,
                              base_object_name, buf, buf_len, pos, true));
    OZ (fill_row_routine_spec(SPEC_AFTER_ROW_MYSQL,
                              trigger_info, base_object_database,
                              base_object_name, buf, buf_len, pos, false));
  }
  OZ (BUF_PRINTF(SPEC_END_MYSQL));
  OX (spec_source.assign_ptr(buf, static_cast<int32_t>(pos)));
  OX (LOG_DEBUG("TRIGGER", K(spec_source)));
  return ret;
}

int ObTriggerSourceBuilder::fill_package_body(const ObTriggerInfo &trigger_info,
                                            const ObString &base_object_database,
                                            const ObString &base_object_name,
                                            const int64_t body_size,
                                            const TriggerContext &trigger_ctx,
                                            ObString &body_source,
                                            ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  const ObString &trigger_name = trigger_info.get_trigger_name();
  char delimiter = MODE_DELIMITER;
  char *buf = static_cast<char *>(alloc.alloc(body_size));
  int64_t buf_len = body_size;
  int64_t pos = 0;
  OV (OB_NOT_NULL(buf), OB_ALLOCATE_MEMORY_FAILED);
  OZ (BUF_PRINTF(BODY_BEGIN_MYSQL,
                 delimiter, trigger_name.length(), trigger_name.ptr(), delimiter));
  if (OB_SUCC(ret)) {
    OZ (fill_row_routine_body(trigger_info, base_object_database, base_object_name,
                              trigger_ctx, buf, buf_len, pos, true));
    OZ (fill_row_routine_body(trigger_info, base_object_database, base_object_name,
                              trigger_ctx, buf, buf_len, pos, false));
  }
  OZ (BUF_PRINTF(BODY_END_MYSQL));
  OX (body_source.assign_ptr(buf, static_cast<int32_t>(pos)));
  LOG_DEBUG("TRIGGER", K(body_source));
  return ret;
}

}  // namespace sql
}  // namespace oceanbase

namespace oceanbase
{
namespace sql
{

int ObTriggerSourceBuilder::generate_simple(const ObTriggerInfo &trigger_info,
                                             const ObString &base_object_database,
                                             const ObString &base_object_name,
                                             const ParseNode &parse_node,
                                             const ObDataTypeCastParams &dtc_params,
                                             ObString &spec_source,
                                             ObString &body_source,
                                             ObIAllocator &alloc,
                                             const PackageSourceType type)
{
  int ret = OB_SUCCESS;
  const ParseNode *block_node = NULL;
  const ParseNode *declare_node = NULL;
  const ParseNode *execute_node = NULL;
  ObString *declare_str = NULL;
  ObString *execute_str = NULL;
  ObString *tg_body = NULL;
  TriggerContext trigger_ctx;
  int64_t spec_size = 0;
  int64_t body_size = 0;

  block_node = &parse_node;
  OV (OB_NOT_NULL(block_node->children_));
  OX (trigger_ctx.select_simple_sections(trigger_info, declare_str, execute_str, tg_body));
  OX (LOG_DEBUG("TRIGGER", K(*declare_str), K(*execute_str)));
  OV (OB_NOT_NULL(declare_str) && OB_NOT_NULL(execute_str) && OB_NOT_NULL(tg_body));

  // trigger body
  OV (OB_NOT_NULL(block_node->str_value_) && block_node->str_len_ > 0);
  OX (tg_body->assign_ptr(block_node->str_value_, static_cast<int32_t>(block_node->str_len_)));
  OZ (ObSQLUtils::convert_sql_text_to_schema_for_storing(alloc, dtc_params, *tg_body));
  OX (LOG_DEBUG("TRIGGER", K(*tg_body)));

  // declare node is optional.
  if (declare_node != NULL) {
    OV (OB_NOT_NULL(declare_node->str_value_) && declare_node->str_len_ > 0);
    OX (declare_str->assign_ptr(declare_node->str_value_,
                                static_cast<int32_t>(declare_node->str_len_)));
    OZ (ObSQLUtils::convert_sql_text_to_schema_for_storing(alloc, dtc_params, *declare_str));
    OX (LOG_DEBUG("TRIGGER", K(*declare_str)));
  }
  //execute node is optional.
  if (execute_node != NULL) {
    OV (OB_NOT_NULL(execute_node->str_value_) && execute_node->str_len_ > 0);
    OX (execute_str->assign_ptr(execute_node->str_value_,
                                static_cast<int32_t>(execute_node->str_len_)));
    OZ (ObSQLUtils::convert_sql_text_to_schema_for_storing(alloc, dtc_params, *execute_str));
    OX (LOG_DEBUG("TRIGGER", K(*execute_str)));
  }
  OX (calculate_source_sizes(trigger_info, base_object_database, base_object_name, spec_size, body_size));
  if (BODY_ONLY != type) {
    OZ (fill_package_spec(trigger_info, base_object_database, base_object_name,
                          spec_size, spec_source, alloc));
  }
  if (SPEC_ONLY != type) {
    OZ (fill_package_body(trigger_info, base_object_database, base_object_name,
                          body_size, trigger_ctx, body_source, alloc));
  }
  OX (LOG_INFO("TRIGGER", K(spec_source), K(body_source)));
  return ret;
}

}  // namespace sql
}  // namespace oceanbase

namespace oceanbase
{
namespace sql
{

int ObTriggerSourceBuilder::generate_package_source(const uint64_t tg_package_id,
                                      common::ObString &source,
                                      bool is_header,
                                      share::schema::ObSchemaGetterGuard &schema_guard,
                                      common::ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  ParseResult parse_result;
  ParseNode *stmt_list_node = NULL;
  const ParseNode *trigger_source_node = NULL;
  const ParseNode *trigger_define_node = NULL;
  const ParseNode *trigger_body_node = NULL;
  const ObTriggerInfo *trigger_info = NULL;
  OZ (schema_guard.get_trigger_info(
      ObTriggerInfo::get_package_trigger_id(tg_package_id), trigger_info));
  CK (OB_NOT_NULL(trigger_info));
  if (OB_SUCC(ret)) {
    ObParser parser(alloc, trigger_info->get_sql_mode());
    OZ (parser.parse(trigger_info->get_trigger_body(), parse_result,
                     TRIGGER_MODE, false, false, true),
        trigger_info->get_trigger_body());
    // stmt list node.
    OV (OB_NOT_NULL(stmt_list_node = parse_result.result_tree_));
    OV (stmt_list_node->type_ == T_STMT_LIST, OB_ERR_UNEXPECTED, stmt_list_node->type_);
    OV (stmt_list_node->num_child_ == 1, OB_ERR_UNEXPECTED, stmt_list_node->num_child_);
    OV (OB_NOT_NULL(stmt_list_node->children_));
    // trigger source node.
    OV (OB_NOT_NULL(trigger_source_node = stmt_list_node->children_[0]));
    if (OB_FAIL(ret)){
      // do nothing
    } else {
      const ObSimpleTableSchemaV2 *table_schema = NULL;
      const ObDatabaseSchema *base_db_schema = NULL;
      ObString spec_source;
      ObString body_source;
      OV (T_TG_SOURCE == trigger_source_node->type_, trigger_source_node->type_);
      OV (OB_NOT_NULL(trigger_define_node = trigger_source_node->children_[1]));
      if (OB_SUCC(ret)) {
        OV (4 == trigger_define_node->num_child_);
        OV (OB_NOT_NULL(trigger_body_node = trigger_define_node->children_[3]));
        OZ (schema_guard.get_simple_table_schema( trigger_info->get_base_object_id(), table_schema));
        CK (OB_NOT_NULL(table_schema));
        OZ (schema_guard.get_database_schema( table_schema->get_database_id(), base_db_schema));
        CK (OB_NOT_NULL(base_db_schema));
      }
      if (OB_SUCC(ret)) {
        OZ (generate_simple(*trigger_info, base_db_schema->get_database_name_str(),
                            table_schema->get_table_name_str(), *trigger_body_node,
                            ObDataTypeCastParams(), spec_source, body_source,
                            alloc, is_header ? SPEC_ONLY : BODY_ONLY));
      }
      OX (source = is_header ? spec_source : body_source);
    }
  }
  LOG_INFO("generate trigger package end", K(source), K(ret));
  return ret;
}

int ObTriggerSourceBuilder::replace_table_name_in_body(ObTriggerInfo &trigger_info,
                                              common::ObIAllocator &alloc,
                                              const common::ObString &base_object_database,
                                              const common::ObString &base_object_name)
{
  UNUSED(base_object_database);
  int ret = OB_SUCCESS;
  char *buf = NULL;
  int64_t buf_len = 0;
  int64_t pos = 0;
  ObParser parser(alloc, trigger_info.get_sql_mode());
  ParseResult parse_result;
  ParseNode *stmt_list_node = NULL;
  const ParseNode *trg_source_node = NULL;

  const ParseNode *trg_def_node = NULL;
  const ParseNode *dml_event_node = NULL;
  const ParseNode *base_schema_node = NULL;
  const ParseNode *base_object_node = NULL;

  OZ (parser.parse(trigger_info.get_trigger_body(), parse_result, TRIGGER_MODE,
                   false, false, true),
      trigger_info.get_trigger_body());
  // stmt list node
  OV (OB_NOT_NULL(stmt_list_node = parse_result.result_tree_));
  OV (stmt_list_node->type_ == T_STMT_LIST, OB_ERR_UNEXPECTED, stmt_list_node->type_);
  OV (stmt_list_node->num_child_ == 1, OB_ERR_UNEXPECTED, stmt_list_node->num_child_);
  OV (OB_NOT_NULL(stmt_list_node->children_));
  // trigger source node
  OV (OB_NOT_NULL(trg_source_node = stmt_list_node->children_[0]));

  OV (2 == trg_source_node->num_child_);
  OV (OB_NOT_NULL(trg_def_node = trg_source_node->children_[1]));
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    OV (4 == trg_def_node->num_child_);
    OV (OB_NOT_NULL(base_schema_node = trg_def_node->children_[1]));
  }
  OV (2 == base_schema_node->num_child_);
  OV (OB_NOT_NULL(base_object_node = base_schema_node->children_[1]));

  if (OB_SUCC(ret)) {
    buf_len = trg_def_node->str_len_ - base_object_node->str_len_ + base_object_name.length() + 3;
    buf = static_cast<char*>(alloc.alloc(buf_len));
    bool has_delimiter_already = false;
    int trg_header_len = (int)base_object_node->pl_str_off_;
    const char *trg_tail_str = (trg_def_node->str_value_ + base_object_node->pl_str_off_ + base_object_node->str_len_);
    has_delimiter_already = ('`' == trg_def_node->str_value_[base_object_node->pl_str_off_]);
    if (has_delimiter_already) {
      // base object database
      trg_tail_str = trg_tail_str + 2;
    }
    OV (OB_NOT_NULL(buf), OB_ALLOCATE_MEMORY_FAILED);
    OZ (BUF_PRINTF("%.*s`%.*s`%.*s",
                   trg_header_len,
                   trg_def_node->str_value_,
                   base_object_name.length(),
                   base_object_name.ptr(),
                   int(trg_def_node->str_len_ - (base_object_node->pl_str_off_ + base_object_node->str_len_)),
                   trg_tail_str));
    OZ (trigger_info.set_trigger_body(ObString(buf)));
  }
  LOG_INFO("rebuild trigger body end", K(trigger_info), K(base_object_name), K(lbt()), K(ret));
  return ret;
}

}  // namespace sql
}  // namespace oceanbase

namespace oceanbase
{
namespace sql
{

int ObTriggerSourceBuilder::build_package_source(
    ObTriggerInfo &trigger_info,
    const ObString &base_object_database,
    const ObString &base_object_name,
    const ParseNode &parse_node,
    const ObDataTypeCastParams &dtc_params)
{

  int ret = OB_SUCCESS;
  ObString spec_source;
  ObString body_source;
  ObIAllocator *allocator = trigger_info.get_allocator();
  OV (OB_NOT_NULL(allocator));
  if (OB_SUCC(ret)) {
    OZ (generate_simple(trigger_info, base_object_database, base_object_name,
                        parse_node, dtc_params,
                        spec_source, body_source, *allocator));
  }
  OZ (trigger_info.set_package_spec_source(spec_source));
  OZ (trigger_info.set_package_body_source(body_source));
  return ret;
}

int ObTriggerSourceBuilder::build_procedure_source(
    const ObTriggerInfo &trigger_info,
    ObIAllocator &allocator,
    const common::ObString &base_object_database,
    const common::ObString &base_object_name,
    const ParseNode &parse_node,
    const ObDataTypeCastParams &dtc_params,
    ObString &procedure_source)
{
  int ret = OB_SUCCESS;
  ObString proc_source;
  int64_t proc_size = 0;
  int64_t proc_params_size;
  ObString tg_body;
  char *buf = NULL;
  int64_t buf_len = 0;
  int64_t pos = 0;
  char delimiter = MODE_DELIMITER;
  int32_t param_new_inout_len = trigger_info.has_after_row_point() ? 2 : 5; // IN or INOUT
  OV (OB_NOT_NULL(parse_node.str_value_) && parse_node.str_len_ > 0);
  OX (tg_body.assign_ptr(parse_node.str_value_, static_cast<int32_t>(parse_node.str_len_)));
  // OZ (ObSQLUtils::convert_sql_text_to_schema_for_storing(*alloc, dtc_params, tg_body));
  if (OB_SUCC(ret)) {
    proc_params_size = trigger_info.get_trigger_name().length() +
                       base_object_database.length() * 2 +
                       base_object_name.length() * 2 +
                       param_new_inout_len;
    proc_size = proc_params_size + tg_body.length() + STRLEN(TRIGGER_PROCEDURE_MYSQL);
    buf = static_cast<char *>(allocator.alloc(proc_size));
    buf_len = proc_size;
    OV (OB_NOT_NULL(buf), OB_ALLOCATE_MEMORY_FAILED);
    OZ (BUF_PRINTF(TRIGGER_PROCEDURE_MYSQL,
                   delimiter, trigger_info.get_trigger_name().length(),
                   trigger_info.get_trigger_name().ptr(), delimiter,
                   delimiter, base_object_database.length(), base_object_database.ptr(), delimiter,
                   delimiter, base_object_name.length(), base_object_name.ptr(), delimiter,
                   param_new_inout_len, trigger_info.has_after_row_point() ? "IN" : "INOUT",
                   delimiter, base_object_database.length(), base_object_database.ptr(), delimiter,
                   delimiter, base_object_name.length(), base_object_name.ptr(), delimiter,
                   tg_body.length(), tg_body.ptr()));
    OX (procedure_source.assign_ptr(buf, static_cast<int32_t>(pos)));
    LOG_DEBUG("TRIGGER PROCEDURE", K(procedure_source));
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase

namespace oceanbase
{
namespace sql
{

void ObTriggerSourceBuilder::TriggerContext::select_simple_sections(
    const ObTriggerInfo &trigger_info,
    ObString *&declaration,
    ObString *&execution,
    ObString *&body)
{
  if (trigger_info.has_before_row_point()) {
    declaration = &before_row_declare_;
    execution = &before_row_execute_;
  } else if (trigger_info.has_after_row_point()) {
    declaration = &after_row_declare_;
    execution = &after_row_execute_;
  }
  body = &trigger_body_;
}

int ObTriggerSourceBuilder::fill_row_routine_body(const ObTriggerInfo &trigger_info,
                                         const ObString &base_object_database,
                                         const ObString &base_object_name,
                                         const TriggerContext &trigger_ctx,
                                         char *buf, int64_t buf_len, int64_t &pos,
                                         const bool is_before_row)
{
  int ret = OB_SUCCESS;
  char delimiter = MODE_DELIMITER;
  const char *body_fmt = is_before_row ? BODY_BEFORE_ROW_MYSQL : BODY_AFTER_ROW_MYSQL;
  OV (OB_NOT_NULL(body_fmt));
  OV (OB_NOT_NULL(buf));
  OV (!base_object_database.empty());
  OV (!base_object_name.empty());
  if (OB_FAIL(ret)) {

  } else {
    const ObString &tg_body = is_before_row ? (trigger_info.has_before_row_point() ? trigger_ctx.trigger_body_ : "")
                                            : (trigger_info.has_after_row_point() ? trigger_ctx.trigger_body_ : "");
    OZ (BUF_PRINTF(body_fmt,
                   delimiter, base_object_database.length(), base_object_database.ptr(), delimiter,
                   delimiter, base_object_name.length(), base_object_name.ptr(), delimiter,
                   delimiter, base_object_database.length(), base_object_database.ptr(), delimiter,
                   delimiter, base_object_name.length(), base_object_name.ptr(), delimiter,
                   tg_body.length(), tg_body.ptr()));
  }
  return ret;
}

int ObTriggerSourceBuilder::fill_row_routine_spec(const char *spec_fmt,
                                         const ObTriggerInfo &trigger_info,
                                         const ObString &base_object_database,
                                         const ObString &base_object_name,
                                         char *buf, int64_t buf_len, int64_t &pos,
                                         const bool is_before_row)
{
  int ret = OB_SUCCESS;
  char delimiter = MODE_DELIMITER;
  OV (OB_NOT_NULL(spec_fmt));
  OV (OB_NOT_NULL(buf));
  OV (!base_object_database.empty());
  OV (!base_object_name.empty());
  OZ (BUF_PRINTF(spec_fmt,
                 delimiter, base_object_database.length(), base_object_database.ptr(), delimiter,
                 delimiter, base_object_name.length(), base_object_name.ptr(), delimiter,
                 delimiter, base_object_database.length(), base_object_database.ptr(), delimiter,
                 delimiter, base_object_name.length(), base_object_name.ptr(), delimiter));
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
