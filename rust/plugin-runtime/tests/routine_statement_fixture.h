// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual single-statement resolver/privilege routines with a controlled in-memory
// schema view and session. No authentication, server SQL execution or Root txn.
#ifndef SEEKDB_TEST_ROUTINE_STATEMENT_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_STATEMENT_FIXTURE_H_
#include "sql/pl/ob_pl_package_guard.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/resolver/ddl/extension_statement_diagnostics.h"

namespace routine_statement_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
struct DiagnosticEnvironment {
  ObWarningBuffer *saved_ = ob_get_tsi_warning_buffer();
  bool logging_ = ObWarningBuffer::is_warn_log_on();
  ~DiagnosticEnvironment() {
    ob_setup_tsi_warning_buffer(saved_);
    ObWarningBuffer::set_warn_log_on(logging_);
  }
};

inline void diagnostics_tests()
{
  DiagnosticEnvironment restore;
  ObWarningBuffer parent, source;
  ob_setup_tsi_warning_buffer(&parent);
  parent.append_warning("outer warning", 11, "01000");
  parent.set_error("outer error", 12);
  parent.set_error_line_column(13, 14);
  parent.set_sql_state("HY001");
  int status = OB_SUCCESS;
  {
    ExtensionStatementDiagnostics isolated(status);
    auto *local = ob_get_tsi_warning_buffer();
    CHECK(local && local != &parent && local->get_total_warning_count() == 0);
    CHECK(local->get_err_code() == OB_MAX_ERROR_CODE);
    local->append_note("first note", 21);
    {
      ExtensionStatementDiagnostics nested(status);
      CHECK(ob_get_tsi_warning_buffer() != local);
      CHECK(ob_get_tsi_warning_buffer()->get_total_warning_count() == 0);
      ob_get_tsi_warning_buffer()->append_warning("nested warning", 22, "01001");
    }
    CHECK(ob_get_tsi_warning_buffer() == local && local->get_total_warning_count() == 2);
    CHECK(parent.get_total_warning_count() == 1); // merged only on outer scope exit
  }
  CHECK(status == OB_SUCCESS && ob_get_tsi_warning_buffer() == &parent);
  CHECK(parent.get_total_warning_count() == 3 && parent.get_warning_item(0)->get_code() == 11);
  CHECK(parent.get_warning_item(1)->log_level_ == ObLogger::USER_NOTE);
  CHECK(parent.get_warning_item(2)->get_code() == 22);
  CHECK(std::string(parent.get_warning_item(2)->get_sql_state()) == "01001");
  CHECK(parent.get_err_code() == 12 && parent.get_error_line() == 13 && parent.get_error_column() == 14);
  CHECK(std::string(parent.get_sql_state()) == "HY001");
  try {
    ExtensionStatementDiagnostics isolated(status);
    auto *local = ob_get_tsi_warning_buffer();
    local->append_warning("before exception", 31, "01002");
    local->set_error("new error", 32);
    local->set_error_line_column(33, 34);
    local->set_sql_state("HY002");
    throw 1;
  } catch (int) {}
  CHECK(ob_get_tsi_warning_buffer() == &parent && parent.get_total_warning_count() == 4);
  CHECK(parent.get_err_code() == 32 && std::string(parent.get_err_msg()) == "new error");
  CHECK(parent.get_error_line() == 33 && parent.get_error_column() == 34);
  CHECK(std::string(parent.get_sql_state()) == "HY002");
  // A full source ring retains only 64 items but contributes its full count.
  for (int i = 0; i < 100; ++i) source.append_warning("ring", 1000 + i, "01003");
  auto *last = const_cast<ObWarningBuffer::WarningItem *>(source.get_warning_item(63));
  last->timestamp_ = 12345;
  last->set_line_no(51);
  last->set_column_no(52);
  CHECK(parent.append_warnings(source) == OB_SUCCESS);
  CHECK(parent.get_total_warning_count() == 104 && parent.get_readable_warning_count() == 64);
  for (int i = 0; i < 64; ++i) CHECK(parent.get_warning_item(i)->get_code() == 1036 + i);
  CHECK(parent.get_warning_item(63)->timestamp_ == 12345);
  CHECK(parent.get_warning_item(63)->get_line_no() == 51 && parent.get_warning_item(63)->get_column_no() == 52);
  CHECK(parent.get_err_code() == 32);
  CHECK(parent.append_warnings(parent) == OB_INVALID_ARGUMENT && parent.get_total_warning_count() == 104);
  source.reset();
  CHECK(parent.append_warnings(source) == OB_SUCCESS && parent.get_total_warning_count() == 104);
  parent.append_note("after merge", 2000);
  CHECK(parent.get_total_warning_count() == 105 && parent.get_warning_item(0)->get_code() == 1037);
  CHECK(parent.get_warning_item(63)->get_code() == 2000);
  source.append_warning("small source", 2001, "01004");
  source.append_note("small note", 2002);
  source.set_error("must not be copied by append_warnings", 2003);
  CHECK(parent.append_warnings(source) == OB_SUCCESS && parent.get_total_warning_count() == 107);
  CHECK(parent.get_warning_item(0)->get_code() == 1039);
  CHECK(parent.get_warning_item(62)->get_code() == 2001);
  CHECK(std::string(parent.get_warning_item(62)->get_sql_state()) == "01004");
  CHECK(parent.get_warning_item(63)->get_code() == 2002);
  CHECK(parent.get_warning_item(63)->log_level_ == ObLogger::USER_NOTE && parent.get_err_code() == 32);
  ob_setup_tsi_warning_buffer(nullptr);
  {
    ExtensionStatementDiagnostics isolated(status);
    CHECK(ob_get_tsi_warning_buffer() != nullptr);
    ob_get_tsi_warning_buffer()->append_warning("no parent", 41);
  }
  CHECK(ob_get_tsi_warning_buffer() == nullptr && status == OB_SUCCESS);
}

inline void run(const char *root)
{
  diagnostics_tests();
  using Kind = oceanbase::share::plugin::ExtensionRoutineUpdateOperation::Kind;
  ObArenaAllocator arena;
  auto session = std::make_unique<ObSQLSessionInfo>();
  CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
  const int variables_status = session->load_default_sys_variable(false, false);
  if (variables_status != OB_SUCCESS) std::cerr << "session variables status: " << variables_status << std::endl;
  CHECK(variables_status == OB_SUCCESS);
  CHECK(session->set_user(ObString::make_string("fixture"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
  session->set_priv_user_id(123);
  session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE | OB_PRIV_ALTER_ROUTINE);
  session->set_database_id(OB_SYS_DATABASE_ID);
  CHECK(session->set_default_database(ObString::make_string(OB_SYS_DATABASE_NAME)) == OB_SUCCESS);
  auto service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  auto overlay = std::make_shared<RoutineSchemaOverlay>();
  ObRoutineInfo original;
  original.set_database_id(OB_SYS_DATABASE_ID);
  original.set_routine_id(311234);
  original.set_owner_id(123);
  original.set_package_id(OB_INVALID_ID);
  original.set_overload(0);
  original.set_subprogram_id(0);
  original.set_routine_type(ROUTINE_FUNCTION_TYPE);
  original.set_schema_version(43);
  CHECK(original.set_routine_name(ObString::make_string("ext_value")) == OB_SUCCESS);
  CHECK(original.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
  CHECK(overlay->stage(original) == OB_SUCCESS);
  CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS);
  ObSqlCtx context;
  context.session_info_ = session.get();
  context.schema_guard_ = &guard;
  context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
  context.stmt_type_ = stmt::T_CREATE_EXTENSION;
  context.is_prepare_protocol_ = true; // outer flags must not contaminate scripts
  context.is_dynamic_sql_ = true;
  context.cur_sql_ = ObString::make_string("outer sentinel");
  ObMySQLProxy proxy;
  ObResolverParams services;
  services.session_info_ = session.get();
  services.sql_proxy_ = &proxy;
  services.is_prepare_protocol_ = true;
  auto exec = std::make_unique<ObExecContext>(arena);
  exec->set_my_session(session.get());
  exec->set_sql_ctx(&context);
  auto *outer_statements = exec->get_stmt_factory();
  auto *outer_expressions = exec->get_expr_factory();
  CHECK(outer_statements && outer_expressions);
  auto *outer_query = outer_statements->get_query_ctx();
  CHECK(outer_query);
  services.query_ctx_ = outer_query;
  CHECK(exec->peek_package_guard() == nullptr);
  LinkExecCtxGuard link(*session, *exec);
  session->set_stmt_type(stmt::T_CREATE_EXTENSION);
  std::string error;
  ExtensionScript script;
  CHECK(script.load_update(root, "sequence_state", "1", "2", session->get_sql_mode(), error) == OB_SUCCESS);
  ExtensionRoutineUpdateBatch first, second;
  ObWarningBuffer outer_diagnostics;
  DiagnosticEnvironment diagnostic_environment;
  ob_setup_tsi_warning_buffer(&outer_diagnostics);
  ObWarningBuffer::set_warn_log_on(true);
  outer_diagnostics.append_warning("previous statement warning", OB_ERR_UNEXPECTED, "01000");
  outer_diagnostics.set_error("previous statement error", OB_ERR_UNEXPECTED);
  ObErrorInfo contaminated;
  CHECK(contaminated.collect_error_info(&original) == OB_SUCCESS);
  CHECK(contaminated.get_error_status() == ERROR_STATUS_HAS_ERROR); // demonstrate the ordinary collector's behavior
  const auto restored = [&]() {
    CHECK(ob_get_tsi_warning_buffer() == &outer_diagnostics);
    CHECK(session->get_cur_exec_ctx() == exec.get());
    CHECK(exec->get_sql_ctx() == &context && context.schema_guard_ == &guard);
    CHECK(exec->peek_package_guard() == nullptr);
    CHECK(context.stmt_type_ == stmt::T_CREATE_EXTENSION && session->get_stmt_type() == stmt::T_CREATE_EXTENSION);
    CHECK(context.cur_sql_ == "outer sentinel" && context.is_prepare_protocol_ && context.is_dynamic_sql_);
    CHECK(services.query_ctx_ == outer_query && !outer_query->calculable_expr_results_.created());
    CHECK(exec->get_stmt_factory() == outer_statements && exec->get_expr_factory() == outer_expressions);
  };
  const int first_status = ExtensionRoutineResolver::resolve_statement(script, 0, services, context, OB_SYS_DATABASE_ID, first, error);
  if (first_status != OB_SUCCESS) std::cerr << "first statement status: " << first_status << ", " << error << std::endl;
  CHECK(first_status == OB_SUCCESS);
  restored();
  CHECK(first.operations().count() == 1 && first.operations().at(0).kind_ == Kind::ALTER);
  CHECK(first.operations().at(0).create_arg_->error_info_.get_error_status() == ERROR_STATUS_NO_ERROR);
  CHECK(outer_diagnostics.get_total_warning_count() == 1);
  CHECK(outer_diagnostics.get_err_code() == OB_ERR_UNEXPECTED);
  const auto &one = first.operations().at(0).create_arg_->routine_info_;
  CHECK(one.get_comment() == "step one" && one.get_routine_id() == 311234);
  CHECK(original.get_comment().empty()); // resolving alone never stages or writes
  CHECK(overlay->stage(one) == OB_SUCCESS);
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 1, services, context, OB_SYS_DATABASE_ID, second, error) == OB_SUCCESS);
  restored();
  CHECK(second.operations().at(0).create_arg_->routine_info_.is_invoker_right());
  CHECK(second.operations().at(0).create_arg_->error_info_.get_error_status() == ERROR_STATUS_NO_ERROR);
  CHECK(second.operations().at(0).create_arg_->routine_info_.get_comment() == "step one");
  first.reset(); // second's inherited metadata is independently wire-owned
  CHECK(second.operations().at(0).create_arg_->routine_info_.get_comment() == "step one");
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 2, services, context, OB_SYS_DATABASE_ID, first, error) == OB_SUCCESS);
  restored();
  CHECK(first.operations().at(0).kind_ == Kind::DROP);
  CHECK(first.operations().at(0).drop_arg_->routine_name_ == "ext_value");
  CHECK(first.operations().at(0).drop_arg_->db_name_ == OB_SYS_DATABASE_NAME);
  CHECK(!first.operations().at(0).drop_arg_->if_exist_);
  CHECK(overlay->erase(OB_SYS_DATABASE_ID, original.get_routine_name(), ROUTINE_FUNCTION_TYPE, 311234) == OB_SUCCESS);
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 4, services, context, OB_SYS_DATABASE_ID, first, error) == OB_ERR_SP_DOES_NOT_EXIST);
  restored();
  CHECK(first.operations().empty() && !error.empty());
  CHECK(outer_diagnostics.get_err_code() == OB_ERR_SP_DOES_NOT_EXIST);
  CHECK(std::string(outer_diagnostics.get_err_msg()).find("ext_value") != std::string::npos);
  // A failure's diagnostics remain visible to the command, but cannot mark a
  // subsequent successful routine as invalid in its persistent error_info.
  ObRoutineInfo recreated;
  CHECK(recreated.assign(original) == OB_SUCCESS);
  recreated.set_routine_id(311235); // a deleted ID must never be reused
  CHECK(overlay->stage(recreated) == OB_SUCCESS);
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 0, services, context, OB_SYS_DATABASE_ID, first, error) == OB_SUCCESS);
  CHECK(first.operations().at(0).create_arg_->error_info_.get_error_status() == ERROR_STATUS_NO_ERROR);
  CHECK(outer_diagnostics.get_err_code() == OB_ERR_SP_DOES_NOT_EXIST);
  restored();
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 3, services, context, OB_SYS_DATABASE_ID, first, error) == OB_SUCCESS);
  CHECK(first.operations().at(0).drop_arg_->if_exist_ && first.operations().at(0).drop_arg_->routine_type_ == ROUTINE_PROCEDURE_TYPE);
  CHECK(ExtensionRoutineResolver::resolve_statement(script, INT64_MAX, services, context, OB_SYS_DATABASE_ID, first, error) == OB_INVALID_ARGUMENT);
  CHECK(first.operations().empty());
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 5, services, context, OB_SYS_DATABASE_ID, first, error) == OB_NOT_SUPPORTED);
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 6, services, context, OB_SYS_DATABASE_ID, first, error) == OB_NOT_INIT);
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 7, services, context, OB_SYS_DATABASE_ID, first, error) == OB_ERR_BAD_DATABASE);
  restored();
  services.disable_privilege_check_ = true;
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 3, services, context, OB_SYS_DATABASE_ID, first, error) == OB_ERR_NO_PRIVILEGE);
  services.disable_privilege_check_ = false;
  session->set_password_expired(true);
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 3, services, context, OB_SYS_DATABASE_ID, first, error) == OB_ERR_MUST_CHANGE_PASSWORD);
  session->set_password_expired(false);
  restored();
  // Also preserve a pre-existing outer package guard, without owning/destroying it.
  auto *outer_packages = exec->get_package_guard();
  CHECK(outer_packages != nullptr && outer_packages->is_inited());
  CHECK(ExtensionRoutineResolver::resolve_statement(script, 3, services, context, OB_SYS_DATABASE_ID, first, error) == OB_SUCCESS);
  CHECK(exec->peek_package_guard() == outer_packages && outer_packages->is_inited());
  routine_sequence_test::run(root, services, context, original);
  routine_privilege_test::run(root, services, context, original);
  routine_create_test::run(root, services, context);
  CHECK(exec->get_sql_ctx() == &context && exec->peek_package_guard() == outer_packages);
  CHECK(session->set_default_database(ObString::make_string("changed")) == OB_SUCCESS);
  script.reset();
  guard.reset();
  overlay.reset();
  CHECK(first.operations().at(0).drop_arg_->db_name_ == OB_SYS_DATABASE_NAME);
  CHECK(second.operations().at(0).create_arg_->routine_info_.get_comment() == "step one");
  exec->set_sql_ctx(nullptr);
}
} // namespace routine_statement_test
#endif
