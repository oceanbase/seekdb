// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Normal CASE parsing/copy/preprocessing/CG, executing the actual Rust DSO.
#ifndef SEEKDB_TEST_RUST_SIMPLE_CASE_FIXTURE_H_
#define SEEKDB_TEST_RUST_SIMPLE_CASE_FIXTURE_H_
#include "sql/rewrite/ob_transform_pre_process.h"
namespace rust_simple_case_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;

template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
         ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  struct Case {
    const char *sql; int result, comparisons, functions, casts;
    bool plugin_match = true; int status = OB_SUCCESS, bind_status = OB_SUCCESS;
    const char *text = nullptr, *type = nullptr;
  };
  bool tested_failures = false;
  for (const Case &test : {
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN 11 WHEN CAST('z' AS rust_utf8) THEN 22 ELSE 33 END", 22, 2, 1, 2},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('z' AS rust_utf8) THEN 11 WHEN CAST(X'FF' AS rust_utf8) THEN 22 ELSE 33 END", 11, 1, 1, 1},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN 11 WHEN CAST('b' AS rust_utf8) THEN 22 ELSE 33 END", 33, 2, 1, 2},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN 11 END", -1, 1, 1, 1},
      Case{"CASE seekdb_rust_text('z') WHEN NULL THEN 11 WHEN CAST('z' AS rust_utf8) THEN 22 END", 22, 1, 1, 1},
      Case{"CASE CAST(NULL AS rust_utf8) WHEN NULL THEN 11 WHEN seekdb_rust_text('a') THEN 22 ELSE 33 END", 33, 0, 1, 0},
      Case{"CASE NULL WHEN seekdb_rust_text('a') THEN 11 ELSE 33 END", 33, 0, 1, 0},
      Case{"CASE seekdb_rust_text('z') WHEN CAST(X'FF' AS rust_utf8) THEN 11 ELSE 33 END", 0, 0, 1, 1, true, OB_INVALID_ARGUMENT},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN 11 WHEN CAST(X'FF' AS rust_utf8) THEN 22 END", 0, 1, 1, 2, true, OB_INVALID_ARGUMENT},
      Case{"CASE CAST(X'FF' AS rust_utf8) WHEN NULL THEN 11 ELSE 33 END", 0, 0, 0, 1, true, OB_INVALID_ARGUMENT},
      Case{"CASE CAST(NULL AS rust_utf8) WHEN CAST(X'FF' AS rust_utf8) THEN 11 ELSE 33 END", 0, 0, 0, 1, true, OB_INVALID_ARGUMENT},
      Case{"CASE seekdb_rust_text('z') WHEN 'a' THEN 11 WHEN CAST('z' AS rust_utf8) THEN 22 END", 22, 1, 1, 2},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN 11 WHEN 'z' THEN 22 END", 22, 1, 1, 2},
      Case{"CASE 'z' WHEN 'a' THEN 11 WHEN seekdb_rust_text('z') THEN 22 END", 22, 0, 1, 1},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN seekdb_rust_char_count('unreached') WHEN CAST('z' AS rust_utf8) THEN 22 ELSE seekdb_rust_char_count('unreached') END", 22, 2, 1, 2},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('z' AS rust_utf8) THEN seekdb_rust_char_count('abc') ELSE 33 END", 3, 1, 2, 1},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN 11 ELSE seekdb_rust_char_count('abcd') END", 4, 1, 2, 1},
      Case{"CASE CAST('' AS rust_utf8) WHEN CAST('' AS rust_utf8) THEN 11 END", 11, 1, 0, 2},
      Case{"CASE CAST('a\\0b' AS rust_utf8) WHEN CAST('aaa' AS rust_utf8) THEN 11 WHEN CAST('a\\0b' AS rust_utf8) THEN 22 END", 22, 2, 0, 3},
      Case{"CASE seekdb_rust_text('中🙂') WHEN CAST('中' AS rust_utf8) THEN 11 WHEN CAST('中🙂' AS rust_utf8) THEN 22 END", 22, 2, 1, 2},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('z' AS rust_utf8) THEN seekdb_rust_text('chosen') ELSE seekdb_rust_text('unreached') END", 0, 1, 2, 1, true, OB_SUCCESS, OB_SUCCESS, "chosen", "org.seekdb.rust-text.utf8"},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('a' AS rust_utf8) THEN seekdb_rust_text('unreached') ELSE 'fallback' END", 0, 1, 1, 1, true, OB_SUCCESS, OB_SUCCESS, "fallback", "core.type.bytes"},
      Case{"CASE seekdb_rust_text('z') WHEN 7 THEN 11 ELSE 33 END", 0, 0, 0, 0, true, OB_SUCCESS, OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"CASE seekdb_rust_text('z') WHEN CAST('z' AS rust_utf8) THEN seekdb_rust_text('x') ELSE 7 END", 0, 0, 0, 0, true, OB_SUCCESS, OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"CASE 7 WHEN 2 THEN 11 WHEN 7 THEN 22 ELSE 33 END", 22, 0, 0, 0, false},
      Case{"CASE 'z' WHEN 'a' THEN 11 WHEN 'z' THEN 22 END", 22, 0, 0, 0, false},
      Case{"CASE NULL WHEN NULL THEN 11 ELSE 33 END", 33, 0, 0, 0, false}}) {
    ObSqlCtx sql_context; sql_context.session_info_ = &session;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(&sql_context);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    const ParseNode *node = nullptr;
    CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString::make_string(test.sql), session.get_charsets4parser(),
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
    if (bound != test.bind_status) std::cerr << "Rust simple CASE bind=" << bound << " sql=" << test.sql << std::endl;
    CHECK(bound == test.bind_status);
    CHECK(provider.comparisons_ == comparisons && provider.functions_ == functions && provider.casts_ == casts);
    if (bound != OB_SUCCESS) {
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
      continue;
    }
    CHECK(raw && columns.empty() && raw->get_expr_type() == (test.plugin_match ? T_OP_CASE : T_OP_ARG_CASE));
    if (test.plugin_match) CHECK(raw->has_flag(CNT_STATE_FUNC));
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    bool transformed = false;
    CHECK(ObTransformPreProcess::transform_expr(factory, session, copy, transformed) == OB_SUCCESS);
    CHECK(copy->get_expr_type() == T_OP_CASE && transformed == !test.plugin_match);
    if (test.type) CHECK(copy->get_plugin_type() && copy->get_plugin_type()->logical_id_ == ObString::make_string(test.type));
    // Inspect both the original and copied DAG: custom comparisons must share
    // the selector, not clone a volatile function once for every WHEN.
    for (const auto *tree : {raw, copy}) if (test.plugin_match) {
      const auto *case_expr = static_cast<const ObCaseOpRawExpr *>(tree);
      const ObRawExpr *selector = nullptr;
      for (int64_t i = 0; i < case_expr->get_when_expr_size(); ++i) {
        const auto *condition = case_expr->get_when_param_expr(i);
        if (condition->get_param_expr(0)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_COMPARE) {
          const auto *value = condition->get_param_expr(0)->get_param_expr(0);
          if (!selector) selector = value;
          else CHECK(value == selector);
        }
      }
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
    if (evaluated != test.status) std::cerr << "Rust simple CASE eval=" << evaluated << " sql=" << test.sql << std::endl;
    CHECK(evaluated == test.status);
    if (evaluated == OB_SUCCESS) {
      CHECK(result);
      if (test.result < 0) CHECK(result->is_null());
      else if (test.text) CHECK(!result->is_null() && result->get_string() == ObString::make_string(test.text));
      else CHECK(!result->is_null() && result->get_int() == test.result);
    }
    if (provider.comparisons_ != comparisons + test.comparisons || provider.functions_ != functions + test.functions ||
        provider.casts_ != casts + test.casts) std::cerr << "Rust simple CASE calls=" << provider.comparisons_ - comparisons << ","
          << provider.functions_ - functions << "," << provider.casts_ - casts << " sql=" << test.sql << std::endl;
    CHECK(provider.comparisons_ == comparisons + test.comparisons && provider.functions_ == functions + test.functions &&
        provider.casts_ == casts + test.casts && provider.resolves_ == resolves);
    if (test.plugin_match && test.comparisons == 2 && !tested_failures) {
      tested_failures = true;
      // Reevaluate control/comparison nodes with cached operands. A stale
      // second comparison must fail the CASE, not become false/fall through.
      auto *case_expr = static_cast<ObCaseOpRawExpr *>(copy);
      ObExpr *comparison = nullptr;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*case_expr->get_when_param_expr(1)->get_param_expr(0),
          outputs, comparison) == OB_SUCCESS && comparison);
      auto *info = dynamic_cast<PluginTypeComparisonExtraInfo *>(comparison->extra_info_);
      CHECK(info && info->valid());
      const auto clear = [&] {
        for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_OP_CASE || expr.type_ == T_OP_EQ ||
            expr.type_ == T_FUN_SYS_PLUGIN_TYPE_COMPARE) expr.get_eval_info(eval).evaluated_ = false;
      };
      ++info->binding_.catalog_epoch;
      clear(); CHECK(root->eval(eval, result) == OB_STATE_NOT_MATCH);
      --info->binding_.catalog_epoch;
      class Cancelled final : public ObIExtraStatusCheck {
      public:
        const char *name() const override { return "plugin-simple-case-cancel"; }
        int check() const override { return OB_TIMEOUT; }
      } cancelled;
      const int called = provider.comparisons_;
      {
        ObIExtraStatusCheck::Guard cancellation(execution, cancelled);
        clear(); CHECK(root->eval(eval, result) == OB_TIMEOUT && provider.comparisons_ == called);
      }
      clear(); CHECK(root->eval(eval, result) == OB_SUCCESS && result && result->get_int() == test.result);
      CHECK(provider.functions_ == functions + test.functions && provider.casts_ == casts + test.casts);
      CHECK(provider.resolves_ == resolves);
    }
    ObPluginStatusSnapshot module;
    CHECK(loader.get_status("org.seekdb.rust-text", module) == OB_SUCCESS && module.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
} // namespace rust_simple_case_test
#endif
