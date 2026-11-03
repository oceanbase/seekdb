/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/resolver/cmd/alter_extension_resolver.h"
#include "sql/resolver/cmd/alter_extension_stmt.h"
#include "sql/session/ob_sql_session_info.h"
namespace oceanbase { namespace sql {
using namespace common;
int AlterExtensionResolver::resolve(const ParseNode &tree)
{
  int ret = OB_SUCCESS;
  stmt_ = nullptr;
  if (tree.type_ != T_ALTER_EXTENSION || tree.num_child_ != 2 || tree.children_ == nullptr ||
      tree.children_[0] == nullptr || tree.children_[0]->str_len_ <= 0 || tree.children_[0]->str_value_ == nullptr ||
      (tree.children_[1] != nullptr && (tree.children_[1]->str_len_ <= 0 || tree.children_[1]->str_value_ == nullptr))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (params_.allocator_ == nullptr || params_.session_info_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (params_.disable_privilege_check_) {
    ret = OB_ERR_NO_PRIVILEGE;
  } else if (params_.is_prepare_protocol_ || params_.session_info_->is_in_transaction() ||
             params_.session_info_->is_inner() || params_.session_info_->is_nested_session()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "ALTER EXTENSION outside a top-level, unprepared, autocommit management statement");
  } else if (params_.session_info_->get_database_id() == 0 ||
             params_.session_info_->get_database_id() == OB_INVALID_ID || params_.session_info_->get_database_name().empty()) {
    ret = OB_ERR_NO_DB_SELECTED;
  } else if (params_.stmt_factory_ == nullptr || params_.query_ctx_ == nullptr) {
    ret = OB_NOT_INIT;
  } else {
    auto *statement = create_stmt<AlterExtensionStmt>();
    ObString name, version, database;
    if (statement == nullptr) ret = OB_ALLOCATE_MEMORY_FAILED;
    else if (OB_FAIL(ob_write_string(*params_.allocator_,
        ObString(tree.children_[0]->str_len_, tree.children_[0]->str_value_), name))) {}
    else if (tree.children_[1] != nullptr && OB_FAIL(ob_write_string(*params_.allocator_,
        ObString(tree.children_[1]->str_len_, tree.children_[1]->str_value_), version))) {}
    else if (OB_FAIL(ob_write_string(*params_.allocator_, params_.session_info_->get_database_name(), database))) {}
    else {
      statement->set_name(name);
      statement->set_version(version);
      statement->set_database(database, params_.session_info_->get_database_id());
    }
  }
  if (OB_FAIL(ret)) stmt_ = nullptr;
  return ret;
}
} }
