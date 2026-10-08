// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// SELECT optimizer/codegen and real operators over a Rust table cursor.
// Schema/session services are fixtures, not a running database server.
#ifndef SEEKDB_TEST_RUST_AGGREGATE_PLAN_FIXTURE_H_
#define SEEKDB_TEST_RUST_AGGREGATE_PLAN_FIXTURE_H_
#include "sql/code_generator/ob_code_generator.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h"
#include "sql/engine/aggregate/ob_scalar_aggregate_op.h"
#include "sql/rewrite/ob_transformer_impl.h"
#include "sql/resolver/ob_resolver.h"
namespace rust_aggregate_plan_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
template <typename Provider>
void run_case(Provider &provider, oceanbase::share::plugin::ObPluginLoader &loader,
    ObArenaAllocator &arena, const char *argument, const char *input,
    const char *minimum, const char *maximum, int nonnull, int batch_size, bool native = false,
    bool distinct = false)
{
  auto session = std::make_unique<ObSQLSessionInfo>();
  CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
  session->set_inner_session();
  auto schema_service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *schema_service, *manager) == OB_SUCCESS);
  ObSchemaChecker checker; CHECK(checker.init(guard) == OB_SUCCESS);
  ObSqlSchemaGuard sql_guard; sql_guard.set_schema_guard(&guard);
  ObSqlCtx sql; sql.session_info_ = session.get(); sql.schema_guard_ = &guard;
  // Operator kits are owned by execution and refer to the physical specs.
  ObPhysicalPlan physical;
  ObExecContext execution(arena); execution.set_my_session(session.get()); execution.set_sql_ctx(&sql);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  auto &factory = *execution.get_expr_factory();
  auto &statements = *execution.get_stmt_factory();
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(*session, &execution);
  ObResolverParams params;
  params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
  params.query_ctx_ = statements.get_query_ctx(); params.session_info_ = session.get(); params.schema_checker_ = &checker;
  const std::string aggregate_argument = std::string(distinct ? "DISTINCT " : "") + argument;
  const std::string query_text = "SELECT /*+ OPT_PARAM('rowsets_max_rows', " + std::to_string(batch_size) + ") */ MIN(" +
      aggregate_argument + "), MAX(" + aggregate_argument + ") FROM TABLE(seekdb_rust_words_bytes('" + input + "'))";
  const char *query = query_text.c_str();
  params.query_ctx_->set_sql_stmt(ObString::make_string(query));
  params.query_ctx_->set_sql_stmt_coll_type(session->get_local_collation_connection());
  params.query_ctx_->set_literal_stmt_type(oceanbase::sql::stmt::T_SELECT);
  ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
  CHECK(parser.parse(ObString::make_string(query), parsed) == OB_SUCCESS);
  // Top-level resolution also initializes/distributes hints needed by rewrite.
  ObResolver resolver(params); ObStmt *resolved_stmt = nullptr;
  const int resolved = resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT,
      *parsed.result_tree_->children_[0], resolved_stmt);
  if (resolved != OB_SUCCESS) std::cerr << "aggregate SELECT resolve=" << resolved << std::endl;
  CHECK(resolved == OB_SUCCESS);
  CHECK(resolved_stmt && resolved_stmt->is_select_stmt());
  ObDMLStmt *stmt = static_cast<ObDMLStmt *>(resolved_stmt);
  CHECK(stmt->get_stmt_hint().query_hint_);
  CHECK(stmt->formalize_stmt_expr_reference(&factory, session.get()) == OB_SUCCESS);
  ObAddr address; CHECK(address.set_ip_addr("127.0.0.1", 2882));
  ObGlobalHint hint;
  ObOptStatManager statistics;
  ObTransformerCtx rewrite;
  rewrite.allocator_ = &arena; rewrite.schema_checker_ = &checker; rewrite.session_info_ = session.get();
  rewrite.exec_ctx_ = &execution; rewrite.expr_factory_ = &factory; rewrite.stmt_factory_ = &statements;
  rewrite.opt_stat_mgr_ = &statistics; rewrite.sql_schema_guard_ = &sql_guard;
  rewrite.self_addr_ = &address; rewrite.phy_plan_ = &physical;
  ObTransformerImpl transformer(&rewrite);
  const int rewritten = transformer.transform(stmt);
  if (rewritten != OB_SUCCESS) std::cerr << "aggregate SELECT rewrite=" << rewritten << std::endl;
  CHECK(rewritten == OB_SUCCESS);
  ObOptimizerContext context(session.get(), &execution, &sql_guard, &statistics, arena,
      &execution.get_physical_plan_ctx()->get_param_store(), address, hint, factory, stmt, false, params.query_ctx_);
  ObOptimizer optimizer(context); ObLogPlan *plan = nullptr;
  const int optimized = optimizer.optimize(*stmt, plan);
  if (optimized != OB_SUCCESS) std::cerr << "aggregate SELECT optimize=" << optimized << std::endl;
  CHECK(optimized == OB_SUCCESS && plan && plan->get_plan_root());
  ObCodeGenerator generator(&execution.get_physical_plan_ctx()->get_datum_param_store());
  const int generated = generator.generate(*plan, physical);
  if (generated != OB_SUCCESS) std::cerr << "aggregate SELECT codegen=" << generated << std::endl;
  CHECK(generated == OB_SUCCESS && physical.get_root_op_spec());
  CHECK(physical.get_batch_size() == batch_size);
  const auto *root = physical.get_root_op_spec();
  CHECK(root->get_type() == PHY_SCALAR_AGGREGATE && root->get_child_cnt() == 1);
  CHECK(root->get_child(0)->get_type() == PHY_FUNCTION_TABLE);
  const auto *aggregate = static_cast<const ObScalarAggregateSpec *>(root);
  CHECK(aggregate->aggr_infos_.count() == 2 && root->output_.count() == 2);
  for (const auto &info : aggregate->aggr_infos_) {
    CHECK(info.param_exprs_.count() == 1 && !info.has_distinct_);
    const auto *ordered = dynamic_cast<const PluginTypeValueExtraInfo *>(info.param_exprs_.at(0)->extra_info_);
    if (native) CHECK(!ordered);
    else {
      CHECK(ordered && ordered->mode_ == PluginTypeValueExtraInfo::ORDERED && ordered->valid());
      CHECK(std::strcmp(ordered->ordering_->binding_.object_id, "org.seekdb.rust-text.utf8") == 0);
    }
  }
  execution.get_physical_plan_ctx()->set_phy_plan(&physical);
  CHECK(execution.init_phy_op(physical.get_phy_operator_size()) == OB_SUCCESS);
  CHECK(execution.init_expr_op(physical.get_expr_operator_size()) == OB_SUCCESS);
  CHECK(physical.get_expr_frame_info().pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObOperator *op = nullptr;
  CHECK(root->create_op_input(execution) == OB_SUCCESS);
  CHECK(root->create_operator(execution, op) == OB_SUCCESS && op);
  const int resolves = provider.resolves_, comparisons = provider.comparisons_, opens = provider.table_opens_;
  const int opened = op->open();
  if (opened != OB_SUCCESS) std::cerr << "aggregate SELECT open=" << opened << std::endl;
  CHECK(opened == OB_SUCCESS);
  // Function-table cursors are lazy: opening the operator must not open a DSO cursor.
  CHECK(provider.table_opens_ == opens);
  for (int pass = 0; pass < 2; ++pass) {
    const int fetched = op->get_next_row();
    if (fetched != OB_SUCCESS) std::cerr << "aggregate SELECT fetch=" << fetched << " sql=" << query << std::endl;
    CHECK(fetched == OB_SUCCESS);
    if (!pass) CHECK(provider.table_opens_ == opens + 1);
    auto &eval = op->get_eval_ctx();
    for (int i = 0; i < 2; ++i) {
      ObDatum *value = nullptr;
      CHECK(root->output_.at(i)->eval(eval, value) == OB_SUCCESS && value);
      const char *expected = i == 0 ? minimum : maximum;
      if (!expected) CHECK(value->is_null());
      else CHECK(!value->is_null() && value->get_string() == ObString::make_string(expected));
    }
    CHECK(op->get_next_row() == OB_ITER_END);
    if (!pass) CHECK(op->rescan() == OB_SUCCESS);
  }
  CHECK(provider.resolves_ == resolves);
  CHECK(provider.comparisons_ == comparisons + (native || !nonnull ? 0 : 4 * (nonnull - 1)));
  CHECK(op->close() == OB_SUCCESS);
  oceanbase::share::plugin::ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
}
template <typename Provider>
void run(Provider &provider, oceanbase::share::plugin::ObPluginLoader &loader, ObArenaAllocator &arena)
{
  for (int batch_size : {0, 3}) {
    for (bool distinct : {false, true})
      run_case(provider, loader, arena, "token", "z aa 🙂 bbb", "z", "bbb", 4, batch_size, false, distinct);
    run_case(provider, loader, arena, "token", "", nullptr, nullptr, 0, batch_size);
    run_case(provider, loader, arena, "CASE WHEN ordinal > 0 THEN CAST(NULL AS rust_utf8) ELSE CAST(token AS rust_utf8) END",
        "z aa 🙂 bbb", nullptr, nullptr, 0, batch_size);
    run_case(provider, loader, arena, "CASE WHEN ordinal IN (2,4) THEN CAST(NULL AS rust_utf8) ELSE CAST(token AS rust_utf8) END",
        "z aa 🙂 bbb", "z", "🙂", 2, batch_size);
    run_case(provider, loader, arena, "seekdb_rust_identity(CAST(token AS rust_utf8))",
        "z aa 🙂 bbb", "z", "bbb", 4, batch_size);
    // "words_bytes" takes bytes; its token output is the plugin's rust_utf8.
    // An explicit cast is required for a genuine native-order control.
    run_case(provider, loader, arena, "CAST(token AS BINARY)", "z aa 🙂 bbb", "aa", "🙂", 4, batch_size, true);
  }
}
} // namespace rust_aggregate_plan_test
#endif
