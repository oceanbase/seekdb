// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_RUST_ROW_COMPARISON_FIXTURE_H_
#define SEEKDB_TEST_RUST_ROW_COMPARISON_FIXTURE_H_
#include <array>
#include "sql/rewrite/ob_transform_pre_process.h"
namespace rust_row_comparison_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;

template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
         ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  using Counts = std::array<int, 7>;
  const auto all = [](int value) { Counts result; result.fill(value); return result; };
  const Counts less{0, 1, 1, 1, 0, 0, 0}, greater{0, 1, 0, 0, 1, 1, 0}, equal{1, 0, 0, 1, 0, 1, 1};
  const auto nested = [](std::string value, int depth) {
    while (depth-- > 0) value = "(" + value + ",0)";
    return value;
  };
  const std::string deep_left = nested("seekdb_rust_text('z')", 63);
  const std::string deep_right = nested("seekdb_rust_text('aa')", 63);
  const std::string oversized_left = nested("seekdb_rust_text('z')", 64);
  const std::string oversized_right = nested("seekdb_rust_text('aa')", 64);
  struct Case {
    const char *left, *right; Counts result, comparisons, functions;
    int casts = 0, status = OB_SUCCESS, bind_status = OB_SUCCESS;
    bool custom = true;
  };
  const char *operators[] = {"=", "<>", "<", "<=", ">", ">=", "<=>"};
  for (int op = 0; op < 7; ++op) for (const Case &test : {
      Case{"(seekdb_rust_text('z'),1)", "(seekdb_rust_text('aa'),2)", less, all(1), all(2)},
      Case{"(seekdb_rust_text('z'),1)", "(seekdb_rust_text('z'),2)", less, all(1), all(2)},
      Case{"(seekdb_rust_text('z'),2)", "(seekdb_rust_text('z'),1)", greater, all(1), all(2)},
      Case{"(seekdb_rust_text('z'),1)", "(seekdb_rust_text('z'),1)", equal, all(1), all(2)},
      Case{"(1,seekdb_rust_text('z'))", "(2,seekdb_rust_text('aa'))", less, all(0), all(0)},
      Case{"(1,seekdb_rust_text('z'))", "(1,seekdb_rust_text('aa'))", less, all(1), all(2)},
      Case{"(NULL,seekdb_rust_text('z'))", "(NULL,seekdb_rust_text('aa'))",
           {0,1,-1,-1,-1,-1,0}, {1,1,0,0,0,0,1}, {2,2,0,0,0,0,2}},
      Case{"(NULL,seekdb_rust_text('z'))", "(NULL,seekdb_rust_text('z'))",
           {-1,-1,-1,-1,-1,-1,1}, {1,1,0,0,0,0,1}, {2,2,0,0,0,0,2}},
      Case{"(CAST(NULL AS rust_utf8),1)", "(CAST(NULL AS rust_utf8),2)",
           {0,1,-1,-1,-1,-1,0}, all(0), all(0)},
      Case{"(CAST(NULL AS rust_utf8),1)", "(seekdb_rust_text('z'),1)",
           {-1,-1,-1,-1,-1,-1,0}, all(0), all(1)},
      Case{"(CAST(NULL AS rust_utf8),1)", "(seekdb_rust_text('z'),2)",
           {0,1,-1,-1,-1,-1,0}, all(0), all(1)},
      Case{"(seekdb_rust_text('z'),1)", "('a',2)", greater, all(0), all(1), 1},
      Case{"(seekdb_rust_text('z'),seekdb_rust_text('z'))", "(seekdb_rust_text('z'),seekdb_rust_text('aa'))",
           less, all(2), all(4)},
      Case{"(seekdb_rust_text('z'),NULL,1)", "(seekdb_rust_text('z'),NULL,2)",
           {0,1,-1,-1,-1,-1,0}, all(1), all(2)},
      Case{"(1,CAST(X'FF' AS rust_utf8))", "(2,CAST('z' AS rust_utf8))", less, all(0), all(0)},
      Case{"(1,CAST(X'FF' AS rust_utf8))", "(1,CAST('z' AS rust_utf8))", all(0), all(0), all(0), 1, OB_INVALID_ARGUMENT},
      Case{"(seekdb_rust_text('z'),1)", "(7,2)", all(0), all(0), all(0), 0, OB_SUCCESS, OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"(seekdb_rust_text('z'),1)", "(seekdb_rust_text('z'),1,2)", all(0), all(0), all(0), 0, OB_SUCCESS, OB_ERR_INVALID_COLUMN_NUM},
      Case{"((seekdb_rust_text('z'),1),2)", "((seekdb_rust_text('aa'),1),2)", less, all(1), all(2)},
      Case{"((seekdb_rust_text('z'),1),2)", "((seekdb_rust_text('z'),1),2)", equal, all(1), all(2)},
      Case{"((seekdb_rust_text('z'),2),0)", "((seekdb_rust_text('z'),1),9)", greater, all(1), all(2)},
      Case{"(0,(seekdb_rust_text('z'),1))", "(0,(seekdb_rust_text('aa'),2))", less, all(1), all(2)},
      Case{"((NULL,seekdb_rust_text('z')),1)", "((NULL,seekdb_rust_text('aa')),2)",
           {0,1,-1,-1,-1,-1,0}, {1,1,0,0,0,0,1}, {2,2,0,0,0,0,2}},
      Case{"((NULL,seekdb_rust_text('z')),1)", "((NULL,seekdb_rust_text('z')),1)",
           {-1,-1,-1,-1,-1,-1,1}, {1,1,0,0,0,0,1}, {2,2,0,0,0,0,2}},
      Case{"((CAST(NULL AS rust_utf8),1),1)", "((CAST(NULL AS rust_utf8),1),2)",
           {0,1,-1,-1,-1,-1,0}, all(0), all(0)},
      Case{"(((1,2),3),seekdb_rust_text('z'))", "(((1,2),4),seekdb_rust_text('aa'))", less, all(0), all(0)},
      Case{"((1,CAST(X'FF' AS rust_utf8)),0)", "((2,CAST('z' AS rust_utf8)),0)", less, all(0), all(0)},
      Case{"((1,CAST(X'FF' AS rust_utf8)),0)", "((1,CAST('z' AS rust_utf8)),0)", all(0), all(0), all(0), 1, OB_INVALID_ARGUMENT},
      Case{"((seekdb_rust_text('z'),1),2)", "(('a',1),2)", greater, all(0), all(1), 1},
      Case{"((seekdb_rust_text('z'),1),2)", "((7,1),2)", all(0), all(0), all(0), 0, OB_SUCCESS, OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"((seekdb_rust_text('z'),1),2)", "(seekdb_rust_text('z'),(1,2))", all(0), all(0), all(0), 0, OB_SUCCESS, OB_ERR_INVALID_COLUMN_NUM},
      Case{"(((1,2),3),seekdb_rust_text('z'))", "((1,(2,3)),seekdb_rust_text('z'))", all(0), all(0), all(0), 0, OB_SUCCESS, OB_ERR_INVALID_COLUMN_NUM},
      Case{"((1,2),3)", "((1,2),4)", all(0), all(0), all(0), 0, OB_SUCCESS, OB_ERR_INVALID_COLUMN_NUM, false},
      Case{"((1,2),(3,4))", "((1,2),(3,5))", all(0), all(0), all(0), 0, OB_SUCCESS, OB_NOT_SUPPORTED, false},
      Case{deep_left.c_str(), deep_right.c_str(), less, all(1), all(2)},
      Case{oversized_left.c_str(), oversized_right.c_str(), all(0), all(0), all(0), 0, OB_SUCCESS, OB_SIZE_OVERFLOW},
      Case{"(1,2)", "(1,3)", less, all(0), all(0), 0, OB_SUCCESS, OB_SUCCESS, false}}) {
    const std::string sql = std::string(test.left) + " " + operators[op] + " " + test.right;
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
    ObSEArray<ObOpRawExpr *, 1> raw_operators;
    ObRawExpr *raw = nullptr;
    const int comparisons = provider.comparisons_, functions = provider.functions_, casts = provider.casts_;
    int bound = ObRawExprUtils::build_raw_expr(factory, session, *node, raw, columns,
        variables, aggregates, windows, subqueries, udfs, raw_operators);
    if (bound == OB_SUCCESS && raw) bound = raw->formalize(&session);
    if (bound != test.bind_status) std::cerr << "Rust row bind=" << bound << " sql=" << sql << std::endl;
    CHECK(bound == test.bind_status);
    CHECK(provider.comparisons_ == comparisons && provider.functions_ == functions && provider.casts_ == casts);
    if (bound != OB_SUCCESS) {
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
      continue;
    }
    CHECK(raw && columns.empty() && !raw->get_plugin_type());
    if (test.custom) CHECK(raw->get_expr_type() == T_OP_EQ && raw->has_flag(CNT_STATE_FUNC));
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    bool transformed = false;
    CHECK(ObTransformPreProcess::transform_expr(factory, session, copy, transformed) == OB_SUCCESS);
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
    if (evaluated != test.status) std::cerr << "Rust row eval=" << evaluated << " sql=" << sql << std::endl;
    CHECK(evaluated == test.status);
    if (evaluated == OB_SUCCESS) {
      CHECK(result);
      if (test.result[op] < 0) CHECK(result->is_null());
      else CHECK(!result->is_null() && result->get_int() == test.result[op]);
    }
    if (provider.comparisons_ != comparisons + test.comparisons[op] || provider.functions_ != functions + test.functions[op] ||
        provider.casts_ != casts + test.casts) std::cerr << "Rust row calls=" << provider.comparisons_ - comparisons << ","
          << provider.functions_ - functions << "," << provider.casts_ - casts << " sql=" << sql << std::endl;
    CHECK(provider.comparisons_ == comparisons + test.comparisons[op] && provider.functions_ == functions + test.functions[op] &&
        provider.casts_ == casts + test.casts && provider.resolves_ == resolves);
    ObPluginStatusSnapshot module;
    CHECK(loader.get_status("org.seekdb.rust-text", module) == OB_SUCCESS && module.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
} // namespace rust_row_comparison_test
#endif
