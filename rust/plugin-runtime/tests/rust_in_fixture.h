// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual Rust DSO and expression execution; not a subquery or storage scan.
#ifndef SEEKDB_TEST_RUST_IN_FIXTURE_H_
#define SEEKDB_TEST_RUST_IN_FIXTURE_H_
namespace rust_in_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;

template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
         ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  const std::string long_value = "seekdb_rust_text('" + std::string(9000, 'x') + "')";
  const std::string long_list = "CAST('" + std::string(9000, 'y') + "' AS rust_utf8),CAST('" + std::string(9000, 'x') + "' AS rust_utf8)";
  std::string maximum_list = "NULL";
  for (uint32_t i = 1; i < SEEKDB_PLUGIN_MAX_ARGUMENTS - 1; ++i) maximum_list += ",NULL";
  const std::string oversize_list = maximum_list + ",NULL";
  struct Case { const char *value, *list; int result, comparisons, functions, casts;
    bool custom = true; int status = OB_SUCCESS; int bind_status = OB_SUCCESS; };
  bool tested_failures = false;
  for (bool negate : {false, true}) for (const Case &test : {
      Case{"seekdb_rust_text('z')", "CAST('a' AS rust_utf8),CAST('z' AS rust_utf8),CAST(X'FF' AS rust_utf8)", 1, 2, 1, 2},
      Case{"seekdb_rust_text('z')", "CAST('z' AS rust_utf8),CAST(X'FF' AS rust_utf8)", 1, 1, 1, 1},
      Case{"seekdb_rust_text('z')", "CAST('a' AS rust_utf8),CAST('b' AS rust_utf8)", 0, 2, 1, 2},
      Case{"seekdb_rust_text('z')", "NULL,CAST('z' AS rust_utf8)", 1, 1, 1, 1},
      Case{"seekdb_rust_text('z')", "CAST('a' AS rust_utf8),NULL", -1, 1, 1, 1},
      Case{"seekdb_rust_text('z')", "NULL,NULL", -1, 0, 1, 0},
      Case{"CAST(NULL AS rust_utf8)", "CAST(X'FF' AS rust_utf8),seekdb_rust_text('unreached')", -1, 0, 0, 0},
      Case{"NULL", "CAST(X'FF' AS rust_utf8),seekdb_rust_text('unreached')", -1, 0, 0, 0},
      Case{"seekdb_rust_text('z')", "CAST('z' AS rust_utf8)", 1, 1, 1, 1},
      Case{"CAST(NULL AS rust_utf8)", "CAST(X'FF' AS rust_utf8)", -1, 0, 0, 0},
      Case{"seekdb_rust_text('z')", "seekdb_rust_text('z'),seekdb_rust_text('unreached')", 1, 1, 2, 0},
      Case{"CAST('' AS rust_utf8)", "CAST('' AS rust_utf8),CAST('z' AS rust_utf8)", 1, 1, 0, 2},
      Case{"CAST('a\\0b' AS rust_utf8)", "CAST('aaa' AS rust_utf8),CAST('a\\0b' AS rust_utf8)", 1, 2, 0, 3},
      Case{"seekdb_rust_text('中')", "CAST('中' AS rust_utf8),CAST('zz' AS rust_utf8)", 1, 1, 1, 1},
      Case{"seekdb_rust_text('z')", "CAST(X'FF' AS rust_utf8),CAST('z' AS rust_utf8)", 0, 0, 1, 1, true, OB_INVALID_ARGUMENT},
      Case{"seekdb_rust_text('z')", "CAST('a' AS rust_utf8),CAST(X'FF' AS rust_utf8),CAST('z' AS rust_utf8)", 0, 1, 1, 2, true, OB_INVALID_ARGUMENT},
      Case{"seekdb_rust_text('z')", "'a','z'", 1, 0, 1, 1, false},
      Case{"'z'", "'a',seekdb_rust_text('z')", 1, 0, 1, 1, false},
      Case{"7", "2,7,9", 1, 0, 0, 0, false},
      Case{"'z'", "'a','b'", 0, 0, 0, 0, false},
      Case{"(1,2)", "(1,2),(3,4)", 1, 0, 0, 0, false},
      Case{long_value.c_str(), long_list.c_str(), 1, 2, 1, 2},
      Case{"seekdb_rust_text('z')", maximum_list.c_str(), -1, 0, 1, 0},
      Case{"seekdb_rust_text('z')", oversize_list.c_str(), 0, 0, 0, 0, true, OB_SUCCESS, OB_SIZE_OVERFLOW},
      Case{"7", oversize_list.c_str(), -1, 0, 0, 0, false},
      Case{"seekdb_rust_text('z')", "1,2", 0, 0, 0, 0, true, OB_SUCCESS, OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"(CAST('a' AS rust_utf8),1)", "(CAST('a' AS rust_utf8),1),(CAST('b' AS rust_utf8),2)", 1, 1, 0, 2, false}}) {
    const std::string sql = std::string(test.value) + (negate ? " NOT IN (" : " IN (") + test.list + ")";
    ObSqlCtx sql_context; sql_context.session_info_ = &session;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(&sql_context);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    const ParseNode *node = nullptr;
    CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString(sql.size(), sql.data()), session.get_charsets4parser(),
        arena, node, session.get_sql_mode()) == OB_SUCCESS && node);
    ObSEArray<ObQualifiedName, 1> columns;
    ObSEArray<ObVarInfo, 1> variables;
    ObSEArray<ObAggFunRawExpr *, 1> aggregates;
    ObSEArray<ObWinFunRawExpr *, 1> windows;
    ObSEArray<ObSubQueryInfo, 1> subqueries;
    ObSEArray<ObUDFInfo, 1> udfs;
    ObSEArray<ObOpRawExpr *, 1> operators;
    ObRawExpr *raw = nullptr;
    const int comparisons = provider.comparisons_, functions = provider.functions_, casts = provider.casts_;
    int bound = ObRawExprUtils::build_raw_expr(factory, session, *node, raw, columns,
        variables, aggregates, windows, subqueries, udfs, operators);
    if (bound == OB_SUCCESS && raw) bound = raw->formalize(&session);
    if (bound != test.bind_status) std::cerr << "Rust IN bind=" << bound << " sql=" << sql << std::endl;
    CHECK(bound == test.bind_status);
    CHECK(provider.comparisons_ == comparisons && provider.functions_ == functions && provider.casts_ == casts);
    if (bound != OB_SUCCESS) {
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
      continue;
    }
    CHECK(raw && columns.empty() && !raw->get_plugin_type());
    CHECK((raw->get_param_expr(0)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_IN) == test.custom);
    if (test.custom) CHECK(raw->has_flag(CNT_STATE_FUNC) && raw->get_expr_type() == (negate ? T_OP_NE : T_OP_EQ));
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    if (test.custom) {
      PluginTypeComparisonExtraInfo info(arena, T_FUN_SYS_PLUGIN_TYPE_IN);
      CHECK(PluginTypeInExpr::read_binding(*copy->get_param_expr(0), info) == OB_SUCCESS && info.valid() && !info.null_safe_);
      ObIExprExtraInfo *extra = nullptr;
      CHECK(info.deep_copy(arena, T_FUN_SYS_PLUGIN_TYPE_IN, extra) == OB_SUCCESS && extra);
      info.null_safe_ = 1;
      ObIExprExtraInfo *invalid = nullptr;
      CHECK(info.deep_copy(arena, T_FUN_SYS_PLUGIN_TYPE_IN, invalid) == OB_INVALID_ARGUMENT && !invalid);
    }
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution); ObExpr *root = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root);
    ObDatum *result = nullptr;
    const int evaluated = root->eval(eval, result);
    if (evaluated != test.status) std::cerr << "Rust IN eval=" << evaluated << " sql=" << sql << std::endl;
    CHECK(evaluated == test.status);
    if (evaluated == OB_SUCCESS) {
      CHECK(result);
      if (test.result < 0) CHECK(result->is_null());
      else CHECK(!result->is_null() && result->get_int() == (negate ? !test.result : test.result));
    }
    if (provider.comparisons_ != comparisons + test.comparisons || provider.functions_ != functions + test.functions ||
        provider.casts_ != casts + test.casts) std::cerr << "Rust IN calls=" << provider.comparisons_ - comparisons << ","
          << provider.functions_ - functions << "," << provider.casts_ - casts << " sql=" << sql << std::endl;
    CHECK(provider.comparisons_ == comparisons + test.comparisons && provider.functions_ == functions + test.functions &&
        provider.casts_ == casts + test.casts && provider.resolves_ == resolves);
    if (test.custom && test.result == 1 && !tested_failures) {
      tested_failures = true;
      ObExpr *membership = nullptr;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy->get_param_expr(0), outputs, membership) == OB_SUCCESS && membership);
      auto *info = dynamic_cast<PluginTypeComparisonExtraInfo *>(membership->extra_info_);
      CHECK(info && info->valid());
      const auto clear = [&] {
        membership->get_eval_info(eval).evaluated_ = false;
        root->get_eval_info(eval).evaluated_ = false;
      };
      ++info->binding_.catalog_epoch;
      clear(); CHECK(root->eval(eval, result) == OB_STATE_NOT_MATCH);
      --info->binding_.catalog_epoch;
      const auto saved = membership->args_[0]->locate_expr_datum(eval);
      const char invalid = char(0xff);
      membership->args_[0]->locate_expr_datum(eval).set_string(ObString(1, &invalid));
      clear(); CHECK(root->eval(eval, result) == OB_INVALID_ARGUMENT);
      membership->args_[0]->locate_expr_datum(eval) = saved;
      class Cancelled final : public ObIExtraStatusCheck {
      public:
        const char *name() const override { return "plugin-in-cancel"; }
        int check() const override { return OB_TIMEOUT; }
      } cancelled;
      const int called = provider.comparisons_;
      {
        ObIExtraStatusCheck::Guard cancellation(execution, cancelled);
        clear(); CHECK(root->eval(eval, result) == OB_TIMEOUT && provider.comparisons_ == called);
      }
      clear(); CHECK(root->eval(eval, result) == OB_SUCCESS && result && result->get_int() == 1);
      CHECK(provider.functions_ == functions + test.functions && provider.casts_ == casts + test.casts);
    }
    ObPluginStatusSnapshot module;
    CHECK(loader.get_status("org.seekdb.rust-text", module) == OB_SUCCESS && module.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
} // namespace rust_in_test
#endif
