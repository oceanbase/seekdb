/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/resolver/cmd/drop_extension_resolver.h"
#include "sql/resolver/cmd/drop_extension_stmt.h"
#include "sql/session/ob_sql_session_info.h"
namespace oceanbase { namespace sql {
using namespace common;
int DropExtensionResolver::resolve(const ParseNode &tree)
{
  int ret = OB_SUCCESS;
  stmt_ = nullptr;
  if (tree.type_ != T_DROP_EXTENSION || tree.num_child_ != 2 || nullptr == tree.children_ ||
      nullptr == tree.children_[0] || tree.children_[0]->str_len_ <= 0 ||
      nullptr == tree.children_[0]->str_value_ || nullptr == tree.children_[1] ||
      tree.children_[1]->type_ != T_INT ||
      (tree.children_[1]->value_ != 0 && tree.children_[1]->value_ != 1)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr == params_.allocator_ || nullptr == params_.session_info_) {
    ret = OB_NOT_INIT;
  } else if (params_.disable_privilege_check_) {
    ret = OB_ERR_NO_PRIVILEGE;
  } else if (params_.is_prepare_protocol_ || params_.session_info_->is_in_transaction()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "DROP EXTENSION in a prepared statement or an active transaction");
  } else if (params_.session_info_->get_database_id() == OB_INVALID_ID ||
             params_.session_info_->get_database_name().empty()) {
    ret = OB_ERR_NO_DB_SELECTED;
  } else if (nullptr == params_.stmt_factory_ || nullptr == params_.query_ctx_) {
    ret = OB_NOT_INIT;
  } else {
    auto *statement = create_stmt<DropExtensionStmt>();
    ObString name;
    ObString database;
    if (nullptr == statement) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(ob_write_string(*params_.allocator_,
          ObString(tree.children_[0]->str_len_, tree.children_[0]->str_value_), name))) {
    } else if (OB_FAIL(ob_write_string(*params_.allocator_, params_.session_info_->get_database_name(), database))) {
    } else {
      statement->set_name(name);
      statement->set_database(database, params_.session_info_->get_database_id());
      statement->set_cascade(tree.children_[1]->value_ == 1);
    }
  }
  if (OB_FAIL(ret)) stmt_ = nullptr;
  return ret;
}
} }
